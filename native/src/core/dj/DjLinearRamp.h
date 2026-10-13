#pragma once
#include <algorithm>

namespace rhino
{
// A fade counted in frames rather than smoothed: it takes exactly the frames
// it was started with, whatever the blocks it is read across, and its last
// frame reads exactly nothing. What decides how much of a fading source is
// read, and so when the source can be let go of, is one of these, never a
// one-pole that only approaches its target.
struct DjLinearRamp
{
    int remaining = 0, total = 0;

    void start(int frames) noexcept { total = remaining = std::max(1, frames); }
    void stop() noexcept { remaining = 0; }
    bool active() const noexcept { return remaining > 0; }
    // Started and not a frame of it read yet.
    bool fresh() const noexcept { return remaining > 0 && remaining == total; }
    // The weight of the frame being read, falling from just under one to
    // exactly zero on the ramp's last frame, and a frame on.
    float next() noexcept
    {
        --remaining;
        return static_cast<float>(remaining) / static_cast<float>(total);
    }
};
}
