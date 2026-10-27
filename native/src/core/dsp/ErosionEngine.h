#pragma once
#include "BiquadFilter.h"
#include <array>
#include <cstdint>
#include <vector>

namespace rhino
{
// Erosion, after Live 12.4's: a short delay whose length is modulated at
// audio rate by a sine and by noise filtered around the same frequency. Moving
// a delay that fast is phase modulation of the input, so it grows sidebands
// around everything that passes: a sine adds a tone-like ring of them, noise
// a hiss that eats the signal, and either one at a high frequency sounds like
// a sample rate that cannot hold still.
//
// The delay is centred on a whole number of samples and swings at most a few
// either side, so with Amount at zero the device is an exact delay, which it
// reports as latency so the track is shifted back into time.
class ErosionEngine
{
public:
    struct Settings
    {
        float frequency = 3000.0f;   // Hz: the sine's, and the middle of the noise's band
        float amount = 0.5f;         // 0 to 1
        float noiseBlend = 0.5f;     // 0 is the sine alone, 1 the noise alone
        float filterWidth = 0.5f;    // 0 to 1: how wide the noise's band is
        float stereoWidth = 0.0f;    // 0 to 1: one modulator for both sides, to one each

        bool operator==(const Settings&) const = default;
    };

    static constexpr int maximumChannels = 2;
    // The widest swing, in samples at 48 kHz; it scales with the rate so the
    // sound does not.
    static constexpr double swingAt48k = 4.0;

    void prepare(double sampleRate);
    void reset();
    void setSettings(const Settings&);
    // In place; channels past the second are left alone.
    void process(float* const* channels, int numChannels, int count);
    // The centre of the delay, in samples.
    int latencySamples() const { return centre; }

    // `count` samples of the left channel's modulator, -1 to 1, from an engine
    // of its own: what the face draws.
    static void modulationPicture(const Settings&, double sampleRate, float* points, int count);

private:
    struct Channel
    {
        std::vector<float> line;
        int at = 0;
        BiquadFilter band;
    };
    void updateFilter();
    // The next sample of each channel's modulator, -1 to 1.
    void modulators(double& left, double& right);
    double read(const Channel&, double delay) const;

    Settings settings;
    double rate = 48000.0;
    double swing = swingAt48k;
    int centre = 6, mask = 63;
    double phase = 0.0, noiseGain = 1.0;
    double amount = 0.0, blend = 0.0, width = 0.0, smoothing = 0.004;
    std::uint32_t sharedNoise = 1, ownNoise = 1;
    std::array<Channel, maximumChannels> channels;
};
}
