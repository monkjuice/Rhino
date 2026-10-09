#pragma once
#include "DjAnalysis.h"
#include <vector>

namespace rhino
{
// A deck's material: decoded once, analysed once, and never written again.
// The session reads the file or renders the track; the engine plays it.
//
// Positions in the engine are frames of this track at its own sample rate,
// whatever rate the device runs at, so a hot cue set at 44.1 kHz still lands
// on the same beat after the device is switched to 96 kHz.
struct DjTrack
{
    std::vector<float> left, right;   // right is empty for a mono file
    double sampleRate = 44100.0;
    juce::String name;
    DjAnalysis analysis;

    int length() const noexcept { return static_cast<int>(left.size()); }
    bool stereo() const noexcept { return !right.empty(); }
    double seconds() const noexcept { return sampleRate > 0.0 ? length() / sampleRate : 0.0; }

    // The beat grid in frames. Zero when the track has no tempo.
    double framesPerBeat() const noexcept
    {
        return analysis.bpm > 0.0 ? sampleRate * 60.0 / analysis.bpm : 0.0;
    }
    double firstBeatFrame() const noexcept { return analysis.firstBeatSeconds * sampleRate; }
    // Fractional beats from beat zero, negative before it.
    double beatAtFrame(double frame) const noexcept
    {
        const auto perBeat = framesPerBeat();
        return perBeat > 0.0 ? (frame - firstBeatFrame()) / perBeat : 0.0;
    }
    double frameAtBeat(double beat) const noexcept { return firstBeatFrame() + beat * framesPerBeat(); }
};
}
