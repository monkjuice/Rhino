#pragma once
#include "PitchTracker.h"
#include <array>
#include <atomic>
#include <vector>

namespace rhino
{
// What the Tuner measures: the pitch of a monophonic input, smoothed enough to
// read, and a history of it for the histogram. It never touches the audio.
//
// The pitch comes from PitchTracker over a band of its own, 30 Hz to 2 kHz,
// which reaches a five-string bass's low B. Each analysis is published for
// the face as plain atomics, so the face reads it on the message thread while
// the audio thread goes on writing: a reading, and a ring of the last few
// seconds of them.
class TunerEngine
{
public:
    static constexpr float lowestHz = 30.0f;
    static constexpr float highestHz = 2000.0f;
    // About five seconds of analyses at 48 kHz.
    static constexpr int historyLength = 512;

    struct Reading
    {
        float frequency = 0.0f;   // Hz; 0 when nothing is held
        float clarity = 0.0f;     // 0 to 1, the tracker's confidence
        bool voiced = false;
    };

    void prepare(double sampleRate, int maximumBlockSize);
    void reset();
    // Audio thread. Reads the channels and leaves them as they are.
    void process(const float* const* channels, int numChannels, int count);

    // Any thread.
    Reading reading() const;
    // The last `count` analyses into `frequencies`, oldest first, 0 where the
    // input had no pitch; answers how many there were, up to `count`.
    int history(float* frequencies, int count) const;
    // How often an analysis lands, for laying the history out in time.
    double analysesPerSecond() const { return analysisRate; }

    // A frequency as a fractional MIDI note against an A4 of `referenceHz`.
    static float noteFor(float frequency, float referenceHz);

private:
    void publish(const PitchTracker::Estimate&);

    PitchTracker tracker;
    std::vector<float> mono;
    double analysisRate = 93.75;
    int seen = 0;
    // The reading as the audio thread keeps it, before it is published.
    float smoothed = 0.0f;
    int unvoicedRun = 0, holdAnalyses = 28;

    std::atomic<float> shownFrequency { 0.0f }, shownClarity { 0.0f };
    std::atomic<bool> shownVoiced { false };
    std::array<std::atomic<float>, historyLength> ring {};
    std::atomic<int> written { 0 };
};
}
