#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <functional>

namespace rhino
{
// The count-in.
//
// This is deliberately not the engine's click track. That click is generated
// inside the playback graph, so it only sounds while the playhead is moving -
// and a count-in is precisely the bars before the playhead moves at all. The
// engine's own pre-roll solves the same problem the other way, by rolling the
// playhead backwards through negative time, which is why it can only offer the
// one and two bar counts its CountIn enum names.
//
// Rhino counts in with the playhead standing still, so it synthesises the click
// itself: a decaying sine per beat, a higher one on the first beat of each bar,
// mixed alongside the transport through a second callback on the same device -
// the same arrangement the browser preview uses. Nothing about the edit changes
// while it runs.
//
// renderBlock is the whole of the generator and is driven directly by the
// tests. It mixes into the buffer it is given, so the caller owns clearing it -
// see the note in the audio callback, which is not given a buffer it can trust.
class CountInClick final : public juce::AudioIODeviceCallback,
                           private juce::AsyncUpdater
{
public:
    CountInClick() = default;
    ~CountInClick() override;

    struct Settings
    {
        double tempoBpm = 120.0;
        double beatsPerBar = 4.0;
        int bars = 1;
        // Decibels, as every other level in Rhino is.
        float gainDb = -6.0f;
        bool emphasiseBars = true;
    };

    // Begins the count. whenFinished is called once, on the message thread,
    // after the last beat has been played out.
    void start(const Settings&, double sampleRateToUse, std::function<void()> whenFinished);
    // Abandons a count in progress. whenFinished is not called.
    void cancel();

    bool isRunning() const { return running.load(std::memory_order_acquire); }
    int beatsTotal() const { return totalBeats; }
    // Beats still to count, rounded up, so a display reading it shows the
    // number being counted rather than the one just gone. Zero when idle.
    int beatsRemaining() const;
    // Which bar of the count is being played, from 1. Zero when idle.
    int barsRemaining() const;

    // Mixes the count into the buffer. The caller clears it first.
    void renderBlock(float* const* channels, int numChannels, int numSamples);

    void audioDeviceIOCallbackWithContext(const float* const*, int, float* const* outputChannelData,
                                          int numOutputChannels, int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

private:
    void handleAsyncUpdate() override;
    void triggerBeat(int beat);

    std::function<void()> finished;
    // Read by the message thread while the audio thread writes them.
    std::atomic<bool> running {false};
    std::atomic<juce::int64> samplesPlayed {0};

    double sampleRate = 44100.0;
    double samplesPerBeat = 0.0;
    juce::int64 totalSamples = 0;
    int totalBeats = 0;
    int beatsPerBar = 4;
    bool emphasise = true;
    float gain = 0.5f;

    // One voice: a click is shorter than a beat at any tempo this allows.
    double phase = 0.0, phaseDelta = 0.0, level = 0.0, decay = 0.0;
    float voiceGain = 0.0f;
    juce::int64 position = 0;
    int nextBeat = 0;
};
}
