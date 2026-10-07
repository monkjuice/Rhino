#pragma once
#include "DrumRackEngine.h"
#include <vector>

namespace rhino
{
// Where Slice cuts a pad's sample: at its transients, the onsets of the hits in
// it, or into equal parts. The face draws the cuts, and spreading them puts
// each slice on a pad of its own.
//
// A transient is a sudden rise in level. The sample is measured as the energy
// of overlapping 10 ms windows, 5 ms apart, in decibels; a window more than a
// threshold louder than the one two before it, and louder than its neighbours'
// rises, is a hit. Sensitivity lowers the threshold from 16 dB to 4 dB and lets
// quieter hits through, down from 30 to 60 dB under the loudest window. Two
// hits closer than 60 ms are one. Each cut is placed at the hit's first sample
// to reach a third of its peak, less two milliseconds, so a slice keeps its
// attack.
struct DrumSlicer
{
    static constexpr int mostSlices = 64;

    // The starts of the slices of the pad's part (Playback::start to end), as
    // fractions of the whole file, in order. The first is the part's start,
    // and the last slice runs to the part's end. Message thread: it reads the
    // whole part.
    static std::vector<float> slices(const DrumSample&, const DrumRackEngine::Playback&);
};
}
