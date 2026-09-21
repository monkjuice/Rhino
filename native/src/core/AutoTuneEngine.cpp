#include "AutoTuneEngine.h"
#include <algorithm>
#include <cmath>

namespace rhino
{
namespace
{
// The control rate.  Everything except the grain scheduling runs here rather
// than per sample: at 48 kHz this is a millisecond and a third, which is finer
// than the fastest retune anyone asks for and far cheaper than smoothing a
// dozen parameters a sample at a time.
constexpr int controlChunk = 64;

// How long a note has to hold still before Humanize treats it as sustained.
constexpr float sustainSeconds = 0.25f;

// The fade across a range or latency change.  The shifter's rings are cleared
// by that switch, so without it the change is a click.
constexpr float settleSeconds = 0.02f;

int nextPowerOfTwo(int value)
{
    int result = 1;
    while (result < value) result *= 2;
    return result;
}

int rangeIndex(PitchTracker::Range range)
{
    return range == PitchTracker::Range::High ? 0 : range == PitchTracker::Range::Mid ? 1 : 2;
}

int longestPeriodFor(PitchTracker::Range range, double sampleRate)
{
    return static_cast<int>(std::ceil(sampleRate / PitchTracker::lowestFrequency(range)));
}

// Which toggle's LED should be lit for a detected frequency.  Auto Shift
// flashes the band the incoming audio falls in, which is the only hint the
// device can give that the range is set wrong.
int bandForFrequency(float frequency)
{
    if (frequency <= 0.0f) return -1;
    if (frequency >= PitchTracker::lowestFrequency(PitchTracker::Range::High)) return 0;
    if (frequency >= PitchTracker::lowestFrequency(PitchTracker::Range::Mid)) return 1;
    return 2;
}
}

int AutoTuneEngine::latencyFor(PitchTracker::Range range, bool liveMode, double sampleRate)
{
    const auto frame = PitchTracker::frameSizeFor(range, sampleRate);
    const auto period = longestPeriodFor(range, sampleRate);
    // Half the analysis frame puts the estimate at the middle of the window it
    // was measured over, which is where it is most nearly true.  Live Mode
    // trades that centring away: the pitch applied to a grain is then the
    // average of a window that ended well before it, which is audible as a
    // slip at a note onset and is exactly the glitch Ableton warns about.
    const auto analysis = liveMode ? frame / 8 : frame / 2;
    return analysis + PsolaShifter::minimumLatencyFor(period);
}

void AutoTuneEngine::prepare(double rate, int channels, int maxBlockSize)
{
    sampleRate = rate > 0.0 ? rate : 48000.0;
    channelCount = std::max(1, channels);
    preparedBlock = std::max(64, maxBlockSize);

    for (int i = 0; i < PitchTracker::rangeCount; ++i)
        trackers[static_cast<size_t>(i)].prepare(sampleRate,
            i == 0 ? PitchTracker::Range::High : i == 1 ? PitchTracker::Range::Mid : PitchTracker::Range::Bass);

    // Sized for Bass, which is the longest period and therefore the largest
    // grain the device can ever be asked for.  Changing the range afterwards
    // only moves numbers around inside this allocation.
    shifter.prepare(sampleRate, channelCount, preparedBlock, longestPeriodFor(PitchTracker::Range::Bass, sampleRate));

    const auto worstLatency = latencyFor(PitchTracker::Range::Bass, false, sampleRate);
    const auto drySize = nextPowerOfTwo(worstLatency + preparedBlock + 64);
    dryMask = drySize - 1;
    dry.assign(static_cast<size_t>(channelCount), std::vector<float>(static_cast<size_t>(drySize), 0.0f));
    monoScratch.assign(static_cast<size_t>(preparedBlock), 0.0f);

    configured = false;
    applyRange(settings.range, settings.liveMode);
    reset();
}

void AutoTuneEngine::applyRange(PitchTracker::Range range, bool liveMode)
{
    activeRange = range;
    activeLiveMode = liveMode;
    latency = latencyFor(range, liveMode, sampleRate);
    shifter.configure(latency, longestPeriodFor(range, sampleRate));
    latency = shifter.latencySamples();
    trackers[static_cast<size_t>(rangeIndex(range))].reset();
    lastPeriod = static_cast<float>(sampleRate) / 200.0f;
    // Not a full reset: the dry line keeps its contents, which are still the
    // right samples, only now read from a different distance back.
    settleGain = configured ? 0.0f : 1.0f;
    configured = true;
}

void AutoTuneEngine::reset()
{
    for (auto& tracker : trackers) tracker.reset();
    shifter.reset();
    for (auto& line : dry) std::fill(line.begin(), line.end(), 0.0f);
    dryWritten = 0;
    smoothedCorrection = 0.0f;
    stability = 0.0f;
    lastNote = 0.0f;
    vibratoPhase = 0.0f;
    vibratoDrift = 0.0f;
    vibratoDriftTarget = 0.0f;
    onsetSeconds = 0.0f;
    settleGain = 1.0f;
    wasVoiced = false;
    readDetected.store(0.0f, std::memory_order_relaxed);
    readTarget.store(0.0f, std::memory_order_relaxed);
    readCents.store(0.0f, std::memory_order_relaxed);
    readClarity.store(0.0f, std::memory_order_relaxed);
    readVoiced.store(0, std::memory_order_relaxed);
    readBand.store(-1, std::memory_order_relaxed);
}

void AutoTuneEngine::setSettings(const Settings& next)
{
    settings = next;
    if (next.range != activeRange || next.liveMode != activeLiveMode)
        applyRange(next.range, next.liveMode);
}

AutoTuneEngine::Readout AutoTuneEngine::readout() const
{
    Readout out;
    out.detectedNote = readDetected.load(std::memory_order_relaxed);
    out.targetNote = readTarget.load(std::memory_order_relaxed);
    out.correctionCents = readCents.load(std::memory_order_relaxed);
    out.clarity = readClarity.load(std::memory_order_relaxed);
    out.voiced = readVoiced.load(std::memory_order_relaxed) != 0;
    out.band = readBand.load(std::memory_order_relaxed);
    out.latencyMs = sampleRate > 0.0 ? static_cast<float>(1000.0 * latency / sampleRate) : 0.0f;
    return out;
}

void AutoTuneEngine::updateControl(int samplesInChunk)
{
    const auto dt = static_cast<float>(samplesInChunk) / static_cast<float>(sampleRate);
    const auto& estimate = trackers[static_cast<size_t>(rangeIndex(activeRange))].latest();

    if (estimate.voiced && !wasVoiced)
    {
        // A new note: the vibrato starts over, which is Auto Shift's LFO
        // Reset, and the sustain clock goes back to zero.
        onsetSeconds = 0.0f;
        vibratoPhase = 0.0f;
        stability = 0.0f;
        lastNote = estimate.midiNote;
    }
    wasVoiced = estimate.voiced;
    onsetSeconds += dt;

    auto target = estimate.midiNote;
    auto rawCorrection = 0.0f;
    if (estimate.voiced)
    {
        target = shiftByScaleDegrees(nearestAllowedNote(estimate.midiNote, settings.mask),
                                     settings.mask, settings.scaleDegreeShift);
        const auto error = target - estimate.midiNote;

        // Flex: full pull at the target, nothing left by the time the note is
        // a semitone away at maximum tolerance.  At zero it is switched off
        // and every note is dragged the whole way, which is the hard sound.
        const auto flexSemitones = std::clamp(settings.flex, 0.0f, 1.0f);
        const auto flexGain = flexSemitones <= 1.0e-4f
            ? 1.0f
            : std::clamp(1.0f - std::abs(error) / flexSemitones, 0.0f, 1.0f);
        rawCorrection = error * std::clamp(settings.strength, 0.0f, 1.0f) * flexGain;

        const auto movement = std::abs(estimate.midiNote - lastNote);
        stability = movement > 0.5f ? 0.0f : std::min(1.0f, stability + dt / sustainSeconds);
        lastNote = estimate.midiNote;
        lastPeriod = static_cast<float>(sampleRate) / std::max(estimate.frequency, 1.0f);
    }
    else
    {
        stability = 0.0f;
    }

    // Humanize stretches the retune on a held note; a phrase still arrives in
    // tune, but the middle of a long note is left where the singer put it.
    const auto retuneMs = std::max(0.0f, settings.retuneMs)
        * (1.0f + 3.0f * std::clamp(settings.humanize, 0.0f, 1.0f) * stability);
    const auto alpha = retuneMs <= 0.1f ? 1.0f : 1.0f - std::exp(-dt / (retuneMs * 0.001f));
    smoothedCorrection += (rawCorrection - smoothedCorrection) * alpha;

    auto vibratoSemitones = 0.0f;
    if (settings.vibratoCents > 0.01f)
    {
        auto rate = std::clamp(settings.vibratoRate, 0.5f, 20.0f);
        auto depth = settings.vibratoCents / 100.0f;
        if (settings.naturalVibrato)
        {
            // A real vibrato wanders. A slow random walk on both rate and
            // depth is what separates it from a siren.
            noise = noise * 1664525u + 1013904223u;
            const auto wander = static_cast<float>(noise >> 8) / 8388608.0f - 1.0f;
            vibratoDriftTarget += (wander - vibratoDriftTarget) * std::min(1.0f, dt * 2.0f);
            vibratoDrift += (vibratoDriftTarget - vibratoDrift) * std::min(1.0f, dt * 6.0f);
            rate *= 1.0f + 0.18f * vibratoDrift;
            depth *= 1.0f + 0.25f * vibratoDrift;
        }
        vibratoPhase += rate * dt;
        vibratoPhase -= std::floor(vibratoPhase);
        const auto fade = settings.vibratoFadeMs <= 1.0f
            ? 1.0f
            : std::clamp(onsetSeconds / (settings.vibratoFadeMs * 0.001f), 0.0f, 1.0f);
        vibratoSemitones = depth * fade
            * std::sin(6.283185307179586f * vibratoPhase);
    }

    const auto totalSemitones = smoothedCorrection + vibratoSemitones
        + settings.pitchSemitones + settings.fineCents / 100.0f;
    const auto pitchRatio = std::pow(2.0f, totalSemitones / 12.0f);
    const auto formantRatio = std::pow(2.0f, std::clamp(settings.formantPercent, -100.0f, 100.0f) / 100.0f)
        * std::pow(pitchRatio, std::clamp(settings.formantFollow, 0.0f, 1.0f));

    const auto shortest = static_cast<float>(sampleRate) / PitchTracker::highestFrequency(activeRange);
    const auto longest = static_cast<float>(longestPeriodFor(activeRange, sampleRate));
    PsolaShifter::Target shift;
    shift.periodSamples = std::clamp(lastPeriod, shortest, longest);
    shift.pitchRatio = pitchRatio;
    shift.formantRatio = formantRatio;
    shift.voiced = estimate.voiced;
    shifter.setTarget(shift);

    readDetected.store(estimate.voiced ? estimate.midiNote : 0.0f, std::memory_order_relaxed);
    readTarget.store(estimate.voiced ? target : 0.0f, std::memory_order_relaxed);
    readCents.store(smoothedCorrection * 100.0f, std::memory_order_relaxed);
    readClarity.store(estimate.clarity, std::memory_order_relaxed);
    readVoiced.store(estimate.voiced ? 1 : 0, std::memory_order_relaxed);
    readBand.store(bandForFrequency(estimate.frequency), std::memory_order_relaxed);
}

void AutoTuneEngine::process(float* const* data, int channels, int count)
{
    if (dry.empty() || data == nullptr || count <= 0) return;
    channels = std::clamp(channels, 1, channelCount);

    const auto inputGain = std::pow(10.0f, std::clamp(settings.inputGainDb, -24.0f, 24.0f) / 20.0f);
    const auto wet = std::clamp(settings.dryWet, 0.0f, 1.0f);
    const auto settleStep = 1.0f / std::max(1.0f, settleSeconds * static_cast<float>(sampleRate));

    for (int offset = 0; offset < count; offset += controlChunk)
    {
        const auto chunk = std::min(controlChunk, count - offset);
        float* chunkData[32];
        const auto used = std::min(channels, 32);
        for (int channel = 0; channel < used; ++channel)
            chunkData[channel] = data[channel] + offset;

        for (int i = 0; i < chunk; ++i)
        {
            auto sum = 0.0f;
            const auto slot = static_cast<size_t>((dryWritten + i) & dryMask);
            for (int channel = 0; channel < used; ++channel)
            {
                const auto sample = chunkData[channel][i] * inputGain;
                chunkData[channel][i] = sample;
                dry[static_cast<size_t>(channel)][slot] = sample;
                sum += sample;
            }
            monoScratch[static_cast<size_t>(i)] = sum / static_cast<float>(used);
        }

        trackers[static_cast<size_t>(rangeIndex(activeRange))].process(monoScratch.data(), chunk);
        updateControl(chunk);
        shifter.process(chunkData, used, chunk);

        for (int i = 0; i < chunk; ++i)
        {
            const auto slot = static_cast<size_t>((dryWritten + i - latency) & dryMask);
            settleGain = std::min(1.0f, settleGain + settleStep);
            const auto wetGain = wet * settleGain;
            for (int channel = 0; channel < used; ++channel)
            {
                const auto dryValue = dry[static_cast<size_t>(channel)][slot];
                chunkData[channel][i] = dryValue * (1.0f - wetGain) + chunkData[channel][i] * wetGain;
            }
        }
        dryWritten += chunk;
    }
}
}
