#pragma once
#include "BiquadFilter.h"
#include <array>

namespace rhino
{
// Multiband Dynamics, after Live's: the input split into three bands by
// Linkwitz-Riley crossovers, and each band given two thresholds. Above the
// upper one a ratio over 1 compresses downwards and under 1 expands upwards;
// below the lower one a ratio over 1 compresses upwards and under 1 expands
// downwards. So a ratio over 1 always narrows the dynamic range, as a
// compressor's does, and one under 1 widens it.
//
// The split sums back to the input with only its phase turned: each crossover
// is a fourth-order Linkwitz-Riley pair, and the low band passes through an
// all-pass at the upper crossover so it turns with the other two.
//
// Each band reads its level once for both channels, as a peak or an RMS, and
// eases each threshold's gain on with Attack and off with Release: for the
// upper threshold attack is the signal rising past it, for the lower one
// falling past it. Time scales every attack and release, and Amount every
// ratio's effect, down to none at 0.
class MultibandDynamicsEngine
{
public:
    static constexpr int bandCount = 3;   // low, mid, high
    static constexpr int maximumChannels = 2;
    // A ratio at this or above is infinite: the band is held at its threshold.
    static constexpr float infiniteRatio = 100.0f;
    static constexpr float minimumRatio = 0.25f;
    // The width of Soft Knee, centred on each threshold.
    static constexpr double kneeDb = 6.0;
    // Neither threshold raises a band by more than this. Upward compression
    // would otherwise lift the noise floor without limit, and upward
    // expansion a loud band as far as its ratio says.
    static constexpr double maximumBoostDb = 24.0;
    static constexpr double maximumCutDb = 120.0;
    // Every level is read as at least this, so silence is a level.
    static constexpr double floorDb = -120.0;

    struct Band
    {
        bool active = true;
        bool solo = false;
        float inputDb = 0.0f;
        float outputDb = 0.0f;
        float belowThresholdDb = -60.0f;
        float belowRatio = 1.0f;
        float aboveThresholdDb = -20.0f;
        float aboveRatio = 1.0f;
        float attackMs = 10.0f;
        float releaseMs = 200.0f;

        bool operator==(const Band&) const = default;
    };

    struct Settings
    {
        // Whether the low and high bands are split off. With neither, the
        // mid band is the whole signal and the device has one band.
        bool lowBand = true;
        bool highBand = true;
        float lowCrossoverHz = 120.0f;
        float highCrossoverHz = 2500.0f;
        bool softKnee = true;
        bool rms = true;
        float outputDb = 0.0f;
        float time = 1.0f;      // scales every attack and release
        float amount = 1.0f;    // 0 to 1, how much of each ratio is applied
        std::array<Band, bandCount> bands;

        bool operator==(const Settings&) const = default;
    };

    // What a band's two thresholds do to a steady level, in dB, before
    // attack and release ease them on and off.
    struct Gain
    {
        double above = 0.0;
        double below = 0.0;
    };

    void prepare(double sampleRate);
    void reset();
    void setSettings(const Settings&);
    // In place; channels past the second are left alone.
    void process(float* const* channels, int numChannels, int count);

    // The mid band always exists; the low and high bands only while split off.
    static bool bandExists(const Settings&, int band);
    // The gain a band's thresholds apply to the level its detector reads,
    // which is after the band's Input. The audio runs this function.
    static Gain staticGain(const Settings&, int band, double levelDb);
    // The level a band puts out for a steady level in: its Input, both
    // thresholds, its Output and the device's Output, or the device's Output
    // alone while the band is switched off. The curve the face draws.
    static double transferDb(const Settings&, int band, double levelDb);

private:
    // One band's thresholds as slopes, worked out once from the settings.
    struct Curve
    {
        double aboveThreshold = 0.0, aboveSlope = 0.0;
        double belowThreshold = 0.0, belowSlope = 0.0;
        double knee = 0.0;
    };
    static Curve curveOf(const Settings&, int band);
    static Gain gainOf(const Curve&, double levelDb);

    struct Splitter
    {
        std::array<BiquadFilter, 2> lowPass1, highPass1, lowPass2, highPass2;
        BiquadFilter allPass2;
    };
    struct BandState
    {
        Curve curve;
        double attack = 0.0, release = 0.0;     // per-sample coefficients
        double peak = 0.0, meanSquare = 0.0;    // linked across channels
        double above = 0.0, below = 0.0;        // the gains as eased, dB
        double input = 1.0, output = 1.0;       // linear, smoothed
        double engaged = 1.0, heard = 1.0;      // 0 to 1, smoothed
    };
    void designCrossovers();
    void snapToSettings();

    Settings settings;
    double rate = 48000.0;
    // Per-sample one-pole coefficients, so the sound is the same from any
    // block size.
    double gainSmoothing = 0.001, switchSmoothing = 0.002, frequencySmoothing = 0.001;
    double peakDecay = 0.998, rmsSmoothing = 0.001;
    // The crossovers glide in octaves and are redesigned every few samples,
    // counted across blocks.
    double lowOctaves = 0.0, highOctaves = 0.0;
    double designedLow = 0.0, designedHigh = 0.0;
    int untilRedesign = 0;
    double output = 1.0;
    std::array<Splitter, maximumChannels> splitters;
    std::array<BandState, bandCount> bands;
};
}
