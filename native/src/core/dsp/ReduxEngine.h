#pragma once
#include "BiquadFilter.h"
#include <array>
#include <cstdint>

namespace rhino
{
// Redux, after Live's: a downsampler and a bit reducer.
//
// The downsampler holds a sample for as long as the chosen rate's period and
// does nothing to stop what that folds down; the aliasing is the sound. Jitter
// shakes each period's length, independently per channel, which makes it
// noisier and wider. The Pre filter band-limits what is sampled and the Post
// filter smooths the steps, an octave above or below half the rate at a time.
//
// The bit reducer rounds to 2^(bits - 1) steps either side of zero. Shape
// compands first, as mu-law does, so quiet detail keeps finer steps than loud;
// DC Shift moves the steps half a step so zero falls between two of them, and
// a quiet signal crunches between them instead of vanishing into a dead band.
class ReduxEngine
{
public:
    struct Settings
    {
        float rateHz = 11025.0f;
        float jitter = 0.0f;         // 0 to 1
        bool preFilter = true;
        bool postFilter = false;
        float postOctave = 0.0f;     // -3 to 3, from half the rate
        float bits = 8.0f;           // 1 to 16, continuous
        float shape = 0.0f;          // 0 to 1
        bool dcShift = false;
        float dryWet = 1.0f;

        bool operator==(const Settings&) const = default;
    };

    static constexpr int maximumChannels = 2;

    void prepare(double sampleRate);
    void reset();
    void setSettings(const Settings&);
    // In place; channels past the second are left alone.
    void process(float* const* channels, int numChannels, int count);

    // One value through the bit reducer, with no downsampling. Pure.
    static double quantise(double x, float bits, float shape, bool dcShift);

    // What the device does to a sine: two cycles of 375 Hz at `sampleRate`,
    // `count` points of input and of output, worked out by an engine of its
    // own after it has settled.
    static void picture(const Settings&, double sampleRate, float* input, float* output, int count);

private:
    struct Channel
    {
        std::array<BiquadFilter, 2> pre, post;
        double phase = 0.0, period = 1.0, held = 0.0, mix = 1.0;
        std::uint32_t noise = 1;
    };
    void updateFilters();
    double nextPeriod(Channel&) const;

    Settings settings;
    double rate = 48000.0;
    double mixSmoothing = 0.004;
    std::array<Channel, maximumChannels> channels;
};
}
