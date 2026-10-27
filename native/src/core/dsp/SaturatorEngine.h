#pragma once
#include "BiquadFilter.h"
#include "Oversampler.h"
#include "SaturatorCurves.h"
#include <array>
#include <vector>

namespace rhino
{
// Saturator, Live's waveshaper: every sample is mapped through a curve
// (SaturatorCurves.h), at four times the sample rate so the harmonics it
// makes above Nyquist are filtered off rather than folded back.
//
// The colour filters are an EQ applied before the curve and exactly undone
// after it, so they change what saturates without changing the tone of what
// does not: a low shelf at a fixed corner (Amt Lo) and a bell that can be
// placed and widened (Amt Hi, Frequency, Width).
//
// The dry signal is delayed by the oversampler's latency before Dry/Wet mixes
// it back, so the two meet in phase.
class SaturatorEngine
{
public:
    struct Settings
    {
        float driveDb = 0.0f;
        saturator::Curve curve = saturator::Curve::analogClip;
        float bassThresholdDb = -50.0f;
        saturator::PostClip postClip = saturator::PostClip::off;
        bool colour = false;
        float colourLow = 0.0f;           // -1 to 1
        float colourHigh = 0.0f;          // -1 to 1
        float colourFrequency = 1000.0f;  // Hz
        float colourWidth = 0.3f;         // 0 to 1
        saturator::WaveshaperShape waves;
        float outputDb = 0.0f;
        float dryWet = 1.0f;

        bool operator==(const Settings&) const = default;
    };

    static constexpr int oversampling = 4;
    static constexpr int maximumChannels = 2;
    // Where the low colour shelf turns, and how far either colour control
    // tilts what reaches the curve.
    static constexpr double colourLowCornerHz = 250.0;
    static constexpr double colourRangeDb = 18.0;

    void prepare(double sampleRate, int maximumBlockSize);
    void reset();
    void setSettings(const Settings&);
    // In place; channels past the second are left alone.
    void process(float* const* channels, int numChannels, int count);
    int latencySamples() const { return oversampler.latencySamples(); }

    // The curve the face draws: what comes out for inputs from -1 to 1 through
    // Drive, the curve, Post Clip and Output. The colour filters are left out,
    // since they make the curve depend on frequency.
    static void transferCurve(const Settings&, float* points, int count);

private:
    struct Channel
    {
        BiquadFilter lowIn, bellIn, lowOut, bellOut;
        std::vector<float> dryLine;
        int dryAt = 0;
        double drive = 1.0, output = 1.0, mix = 1.0;
    };
    void updateColour();

    Settings settings;
    double rate = 48000.0;
    // Per inner sample (drive, output) and per outer sample (mix): one-pole
    // smoothing, the same from any block size.
    double innerSmoothing = 0.001, outerSmoothing = 0.004;
    Oversampler oversampler;
    std::array<Channel, maximumChannels> channels;
    std::vector<float> wet;
};
}
