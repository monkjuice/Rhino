#pragma once
#include <juce_core/juce_core.h>
#include <atomic>
#include <vector>

namespace rhino
{
// What a DJ deck knows about its material before it plays a note of it: the
// picture of the waveform, the tempo and where the beats fall, how loud each
// bar is and where the drops are, and the key.
//
// Pure C++ over a float buffer, in RhinoCore, so a test can hand it a
// synthesised beat and measure what it answers. Nothing here knows about the
// engine, the deck or the screen.

// One column of the waveform, as the CDJ draws it: the low, mid and high
// bands kept apart so a kick, a vocal and a hat read as three colours.
struct DjWaveformColumn
{
    float low = 0.0f, mid = 0.0f, high = 0.0f;
    float peak() const noexcept { return low > mid ? (low > high ? low : high) : (mid > high ? mid : high); }
};

struct DjAnalysis
{
    // Frames of audio per waveform column. Fine enough to zoom to a beat and
    // coarse enough that a ten-minute file is a few hundred thousand columns.
    static constexpr int columnFrames = 256;

    // The tempo in beats per minute, or 0 when none was found.
    double bpm = 0.0;
    // Where beat zero sits, a downbeat. The grid extends both ways from it.
    double firstBeatSeconds = 0.0;
    int beatsPerBar = 4;
    // 0..1, how clearly the tempo stood out. Not a probability.
    double confidence = 0.0;

    std::vector<DjWaveformColumn> columns;
    // The level of each bar, counted from the first downbeat, as a fraction
    // of the loudest bar.
    std::vector<float> barEnergy;
    // Bars where the energy comes back after a quieter passage: the drops.
    std::vector<int> drops;
    // 0..11 major, C to B; 12..23 minor, C to B; -1 unknown.
    int keyIndex = -1;

    bool hasGrid() const noexcept { return bpm > 0.0; }
    double secondsPerBeat() const noexcept { return bpm > 0.0 ? 60.0 / bpm : 0.0; }
    // "Am", "F#"
    static juce::String keyName(int key);
    // The Camelot wheel, which is how DJs read a key: "8A" is A minor.
    static juce::String camelotName(int key);
};

struct DjAnalysisOptions
{
    // A track rendered from the song knows its tempo exactly, so detection
    // is skipped and the grid is laid from these.
    double knownBpm = 0.0;
    double knownFirstBeatSeconds = 0.0;
    int beatsPerBar = 4;
    bool detectKey = true;
    // Set from another thread to abandon the analysis; the result is then
    // whatever was finished, and not to be trusted.
    std::atomic<bool>* cancel = nullptr;
};

// right may be null for a mono file. Blocking: a six-minute file takes a
// fraction of a second, so call it on a worker rather than the message thread.
DjAnalysis analyseDjTrack(const float* left, const float* right, int frames, double sampleRate,
                          const DjAnalysisOptions& = {});
}
