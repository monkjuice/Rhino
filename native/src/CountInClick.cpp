#include "CountInClick.h"
#include <cmath>

namespace rhino
{
namespace
{
// A click is a transient, not a note: short enough that the next beat always
// finds the voice silent, and started from phase zero so it opens on a zero
// crossing rather than a step.
constexpr double clickDecaySeconds = 0.035;
constexpr double clickFrequency = 1000.0, accentFrequency = 1600.0;
}

CountInClick::~CountInClick()
{
    cancel();
    cancelPendingUpdate();
}

void CountInClick::start(const Settings& settings, double sampleRateToUse, std::function<void()> whenFinished)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    cancel();
    cancelPendingUpdate();
    if (settings.bars <= 0 || !(settings.tempoBpm > 0.0) || !(settings.beatsPerBar > 0.0) || !(sampleRateToUse > 0.0))
    {
        // Nothing to count. The caller still gets its callback, so a count-in
        // of zero bars and a count-in that has finished take the same path.
        if (whenFinished) whenFinished();
        return;
    }
    sampleRate = sampleRateToUse;
    samplesPerBeat = sampleRate * 60.0 / settings.tempoBpm;
    beatsPerBar = std::max(1, juce::roundToInt(settings.beatsPerBar));
    totalBeats = beatsPerBar * settings.bars;
    totalSamples = static_cast<juce::int64>(std::llround(samplesPerBeat * totalBeats));
    emphasise = settings.emphasiseBars;
    gain = juce::Decibels::decibelsToGain(settings.gainDb, -60.0f);
    finished = std::move(whenFinished);
    position = 0;
    nextBeat = 0;
    level = 0.0;
    phase = 0.0;
    samplesPlayed.store(0, std::memory_order_release);
    running.store(true, std::memory_order_release);
}

void CountInClick::cancel()
{
    running.store(false, std::memory_order_release);
    samplesPlayed.store(0, std::memory_order_release);
    level = 0.0;
    finished = {};
}

int CountInClick::beatsRemaining() const
{
    if (!isRunning() || samplesPerBeat <= 0.0)
        return 0;
    const auto played = static_cast<double>(samplesPlayed.load(std::memory_order_acquire));
    const auto beatsGone = static_cast<int>(played / samplesPerBeat);
    return std::max(0, totalBeats - beatsGone);
}

int CountInClick::barsRemaining() const
{
    const auto beats = beatsRemaining();
    return beats <= 0 ? 0 : (beats + beatsPerBar - 1) / beatsPerBar;
}

void CountInClick::triggerBeat(int beat)
{
    const auto accent = emphasise && beat % beatsPerBar == 0;
    phase = 0.0;
    phaseDelta = juce::MathConstants<double>::twoPi * (accent ? accentFrequency : clickFrequency) / sampleRate;
    level = 1.0;
    decay = std::exp(-1.0 / (clickDecaySeconds * sampleRate));
    voiceGain = gain * (accent ? 1.0f : 0.72f);
}

void CountInClick::renderBlock(float* const* channels, int numChannels, int numSamples)
{
    if (!running.load(std::memory_order_acquire) || numSamples <= 0 || numChannels <= 0)
        return;
    for (int i = 0; i < numSamples; ++i)
    {
        if (nextBeat < totalBeats && static_cast<double>(position) >= nextBeat * samplesPerBeat)
            triggerBeat(nextBeat++);
        if (level > 1.0e-5)
        {
            const auto sample = static_cast<float>(std::sin(phase) * level) * voiceGain;
            for (int channel = 0; channel < numChannels; ++channel)
                if (channels[channel] != nullptr)
                    channels[channel][i] += sample;
            phase += phaseDelta;
            level *= decay;
        }
        ++position;
        if (position >= totalSamples)
        {
            // The count is over on the sample the downbeat lands on. The
            // transport is started from the message thread, so it follows
            // within a block rather than exactly on that sample.
            running.store(false, std::memory_order_release);
            samplesPlayed.store(totalSamples, std::memory_order_release);
            triggerAsyncUpdate();
            return;
        }
    }
    samplesPlayed.store(position, std::memory_order_release);
}

void CountInClick::audioDeviceIOCallbackWithContext(const float* const*, int, float* const* outputChannelData,
                                                    int numOutputChannels, int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    // The device manager sums its callbacks, so this adds to whatever the
    // transport has already put in the buffer rather than clearing it.
    renderBlock(outputChannelData, numOutputChannels, numSamples);
}

void CountInClick::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (device != nullptr && device->getCurrentSampleRate() > 0.0)
        sampleRate = device->getCurrentSampleRate();
}

void CountInClick::audioDeviceStopped()
{
    // A count cannot finish once the device it was counting on has gone, so it
    // stops here - but this runs on the device's own thread, and the completion
    // callback belongs to the message thread, so it is deliberately left alone.
    // The next start() clears it; until then the count simply reads as idle and
    // the record button falls back to saying the tracks are armed.
    running.store(false, std::memory_order_release);
    samplesPlayed.store(0, std::memory_order_release);
}

void CountInClick::handleAsyncUpdate()
{
    auto callback = std::move(finished);
    finished = {};
    if (callback) callback();
}
}
