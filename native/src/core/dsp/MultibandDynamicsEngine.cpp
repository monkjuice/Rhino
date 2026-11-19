#include "MultibandDynamicsEngine.h"
#include <algorithm>
#include <cmath>

namespace rhino
{
namespace
{
// A Linkwitz-Riley crossover is two Butterworth biquads on either side, and
// what its sides sum to is the all-pass with the same poles.
constexpr double butterworthQ = 0.70710678118654752;
constexpr double decibelsToNepers = 0.11512925464970229;   // ln 10 / 20

// How quickly the detectors follow, before Attack and Release: a peak is
// taken at once and let go over this, and RMS averages over this.
constexpr double peakSeconds = 0.02;
constexpr double rmsSeconds = 0.03;
constexpr double gainSeconds = 0.02;      // Input, Output and the device's Output
constexpr double switchSeconds = 0.01;    // a band switched off or soloed out
constexpr double crossoverSeconds = 0.02;
constexpr int redesignInterval = 16;

Biquad allPass(double w0, double q)
{
    const auto cosw = std::cos(w0), alpha = std::sin(w0) / (2.0 * q);
    return eqdetail::normalise(1.0 - alpha, -2.0 * cosw, 1.0 + alpha, 1.0 + alpha, -2.0 * cosw, 1.0 - alpha);
}

double decayOver(double seconds, double rate)
{
    return std::exp(-1.0 / (seconds * rate));
}

double gainOfDb(double decibels)
{
    return std::exp(decibels * decibelsToNepers);
}

// How far a level is past a threshold, in dB, eased over the knee: nothing
// until half the knee before the threshold, all of it from half the knee
// after, and a parabola between that meets both.
double beyond(double distance, double knee)
{
    if (knee <= 0.0)
        return std::max(0.0, distance);
    if (distance <= -knee * 0.5)
        return 0.0;
    if (distance >= knee * 0.5)
        return distance;
    const auto into = distance + knee * 0.5;
    return into * into / (2.0 * knee);
}

// Attack while the gain moves further from none in the direction it is
// heading, release while it comes back.
void ease(double& current, double target, double attack, double release)
{
    const auto coefficient = (target - current) * target > 0.0 ? attack : release;
    current = target + (current - target) * coefficient;
}
}

bool MultibandDynamicsEngine::bandExists(const Settings& settings, int band)
{
    return band == 0 ? settings.lowBand : band == 2 ? settings.highBand : band == 1;
}

double MultibandDynamicsEngine::slopeOf(float ratio)
{
    return ratio >= infiniteRatio ? 1.0 : 1.0 - 1.0 / std::max(static_cast<double>(ratio), static_cast<double>(minimumRatio));
}

MultibandDynamicsEngine::Curve MultibandDynamicsEngine::curveOf(const Settings& settings, int band)
{
    const auto& controls = settings.bands[static_cast<size_t>(std::clamp(band, 0, bandCount - 1))];
    const auto amount = std::clamp(static_cast<double>(settings.amount), 0.0, 1.0);
    Curve curve;
    curve.aboveThreshold = controls.aboveThresholdDb;
    // The lower threshold never stands above the upper one.
    curve.belowThreshold = std::min(controls.belowThresholdDb, controls.aboveThresholdDb);
    curve.aboveSlope = slopeOf(controls.aboveRatio) * amount;
    curve.belowSlope = slopeOf(controls.belowRatio) * amount;
    curve.knee = settings.softKnee ? kneeDb : 0.0;
    return curve;
}

MultibandDynamicsEngine::Gain MultibandDynamicsEngine::gainOf(const Curve& curve, double levelDb)
{
    const auto level = std::max(levelDb, floorDb);
    Gain gain;
    gain.above = std::clamp(-curve.aboveSlope * beyond(level - curve.aboveThreshold, curve.knee),
                            -maximumCutDb, maximumBoostDb);
    gain.below = std::clamp(curve.belowSlope * beyond(curve.belowThreshold - level, curve.knee),
                            -maximumCutDb, maximumBoostDb);
    return gain;
}

void MultibandDynamicsEngine::prepare(double sampleRate)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    gainSmoothing = 1.0 - decayOver(gainSeconds, rate);
    switchSmoothing = 1.0 - decayOver(switchSeconds, rate);
    frequencySmoothing = 1.0 - decayOver(crossoverSeconds, rate);
    peakDecay = decayOver(peakSeconds, rate);
    rmsSmoothing = 1.0 - decayOver(rmsSeconds, rate);
    setSettings(settings);
    reset();
}

void MultibandDynamicsEngine::setSettings(const Settings& next)
{
    settings = next;
    const auto time = std::clamp(static_cast<double>(settings.time), 0.01, 100.0);
    for (int b = 0; b < bandCount; ++b)
    {
        auto& band = bands[static_cast<size_t>(b)];
        const auto& controls = settings.bands[static_cast<size_t>(b)];
        band.curve = curveOf(settings, b);
        band.attack = decayOver(std::max(1.0e-5, controls.attackMs * 0.001 * time), rate);
        band.release = decayOver(std::max(1.0e-5, controls.releaseMs * 0.001 * time), rate);
    }
}

void MultibandDynamicsEngine::reset()
{
    for (auto& splitter : splitters)
    {
        for (auto* pair : { &splitter.lowPass1, &splitter.highPass1, &splitter.lowPass2, &splitter.highPass2 })
            for (auto& filter : *pair)
                filter.reset();
        splitter.allPass2.reset();
    }
    for (auto& band : bands)
    {
        band.peak = band.meanSquare = 0.0;
        band.above = band.below = 0.0;
    }
    lastLevels = {};
    snapToSettings();
}

// Everything that glides starts where the controls are.
void MultibandDynamicsEngine::snapToSettings()
{
    auto anySolo = false;
    for (int b = 0; b < bandCount; ++b)
        anySolo = anySolo || (bandExists(settings, b) && settings.bands[static_cast<size_t>(b)].solo);
    for (int b = 0; b < bandCount; ++b)
    {
        auto& band = bands[static_cast<size_t>(b)];
        const auto& controls = settings.bands[static_cast<size_t>(b)];
        band.input = gainOfDb(controls.inputDb);
        band.output = gainOfDb(controls.outputDb);
        band.engaged = controls.active ? 1.0 : 0.0;
        band.heard = !anySolo || controls.solo ? 1.0 : 0.0;
    }
    output = gainOfDb(settings.outputDb);
    const auto nyquistGuard = rate * 0.45;
    lowOctaves = std::log2(std::clamp(static_cast<double>(settings.lowCrossoverHz), 20.0, nyquistGuard));
    highOctaves = std::log2(std::clamp(static_cast<double>(settings.highCrossoverHz), 20.0, nyquistGuard));
    designCrossovers();
    untilRedesign = redesignInterval;
}

void MultibandDynamicsEngine::designCrossovers()
{
    designedLow = lowOctaves;
    designedHigh = highOctaves;
    // The upper crossover never stands below the lower one.
    const auto low = std::exp2(designedLow);
    const auto high = std::max(low, std::exp2(designedHigh));
    const auto w1 = 2.0 * eqdetail::pi * low / rate;
    const auto w2 = 2.0 * eqdetail::pi * high / rate;
    const auto lowPass1 = eqdetail::lowPass(w1, butterworthQ), highPass1 = eqdetail::highPass(w1, butterworthQ);
    const auto lowPass2 = eqdetail::lowPass(w2, butterworthQ), highPass2 = eqdetail::highPass(w2, butterworthQ);
    const auto allPass2 = allPass(w2, butterworthQ);
    for (auto& splitter : splitters)
    {
        for (int stage = 0; stage < 2; ++stage)
        {
            splitter.lowPass1[static_cast<size_t>(stage)].coefficients = lowPass1;
            splitter.highPass1[static_cast<size_t>(stage)].coefficients = highPass1;
            splitter.lowPass2[static_cast<size_t>(stage)].coefficients = lowPass2;
            splitter.highPass2[static_cast<size_t>(stage)].coefficients = highPass2;
        }
        splitter.allPass2.coefficients = allPass2;
    }
}

void MultibandDynamicsEngine::process(float* const* audio, int numChannels, int count)
{
    const auto used = std::min(numChannels, maximumChannels);
    lastLevels = {};
    if (used <= 0 || count <= 0)
        return;

    const auto nyquistGuard = rate * 0.45;
    const auto lowTarget = std::log2(std::clamp(static_cast<double>(settings.lowCrossoverHz), 20.0, nyquistGuard));
    const auto highTarget = std::log2(std::clamp(static_cast<double>(settings.highCrossoverHz), 20.0, nyquistGuard));
    const auto outputTarget = gainOfDb(settings.outputDb);
    std::array<bool, bandCount> exists {};
    std::array<double, bandCount> inputTarget {}, outputTargetOf {}, engagedTarget {}, heardTarget {};
    auto anySolo = false;
    for (int b = 0; b < bandCount; ++b)
    {
        exists[static_cast<size_t>(b)] = bandExists(settings, b);
        anySolo = anySolo || (exists[static_cast<size_t>(b)] && settings.bands[static_cast<size_t>(b)].solo);
    }
    for (int b = 0; b < bandCount; ++b)
    {
        const auto& controls = settings.bands[static_cast<size_t>(b)];
        inputTarget[static_cast<size_t>(b)] = gainOfDb(controls.inputDb);
        outputTargetOf[static_cast<size_t>(b)] = gainOfDb(controls.outputDb);
        engagedTarget[static_cast<size_t>(b)] = controls.active ? 1.0 : 0.0;
        heardTarget[static_cast<size_t>(b)] = !anySolo || controls.solo ? 1.0 : 0.0;
    }
    const auto inverseChannels = 1.0 / static_cast<double>(used);
    // The loudest of each band in and out, in the detector's own terms: an
    // amplitude for a peak, a mean square for RMS.
    std::array<double, bandCount> loudestIn {}, loudestOut {};

    for (int i = 0; i < count; ++i)
    {
        lowOctaves += (lowTarget - lowOctaves) * frequencySmoothing;
        highOctaves += (highTarget - highOctaves) * frequencySmoothing;
        if (--untilRedesign <= 0)
        {
            untilRedesign = redesignInterval;
            if (std::abs(lowOctaves - designedLow) > 1.0e-5 || std::abs(highOctaves - designedHigh) > 1.0e-5)
                designCrossovers();
        }

        // Every filter runs every sample, whichever bands are split off, so
        // switching a band in starts its filters from the signal.
        double split[bandCount][maximumChannels] {};
        for (int c = 0; c < used; ++c)
        {
            auto& splitter = splitters[static_cast<size_t>(c)];
            const auto x = static_cast<double>(audio[c][i]);
            const auto low = splitter.lowPass1[1].process(splitter.lowPass1[0].process(x));
            const auto aboveLow = splitter.highPass1[1].process(splitter.highPass1[0].process(x));
            const auto rest = settings.lowBand ? aboveLow : x;
            const auto mid = splitter.lowPass2[1].process(splitter.lowPass2[0].process(rest));
            const auto high = splitter.highPass2[1].process(splitter.highPass2[0].process(rest));
            const auto turned = splitter.allPass2.process(low);
            split[0][c] = settings.highBand ? turned : low;
            split[1][c] = settings.highBand ? mid : rest;
            split[2][c] = high;
        }

        double sum[maximumChannels] {};
        for (int b = 0; b < bandCount; ++b)
        {
            if (!exists[static_cast<size_t>(b)])
                continue;
            auto& band = bands[static_cast<size_t>(b)];
            band.input += (inputTarget[static_cast<size_t>(b)] - band.input) * gainSmoothing;
            band.output += (outputTargetOf[static_cast<size_t>(b)] - band.output) * gainSmoothing;
            band.engaged += (engagedTarget[static_cast<size_t>(b)] - band.engaged) * switchSmoothing;
            band.heard += (heardTarget[static_cast<size_t>(b)] - band.heard) * switchSmoothing;

            // One level for both channels, so the image stays where it is.
            auto peak = 0.0, square = 0.0;
            for (int c = 0; c < used; ++c)
            {
                const auto v = split[b][c] * band.input;
                peak = std::max(peak, std::abs(v));
                square += v * v;
            }
            band.peak = std::max(peak, band.peak * peakDecay);
            band.meanSquare += (square * inverseChannels - band.meanSquare) * rmsSmoothing;
            const auto levelDb = settings.rms ? 10.0 * std::log10(std::max(band.meanSquare, 1.0e-12))
                                              : 20.0 * std::log10(std::max(band.peak, 1.0e-6));
            const auto target = gainOf(band.curve, levelDb);
            ease(band.above, target.above, band.attack, band.release);
            ease(band.below, target.below, band.attack, band.release);

            const auto wet = band.input * gainOfDb(band.above + band.below) * band.output;
            // Switched off, a band passes as it was split; soloed out, it is
            // not heard at all.
            const auto factor = band.heard * (1.0 + band.engaged * (wet - 1.0));
            for (int c = 0; c < used; ++c)
                sum[c] += split[b][c] * factor;

            // Going out, the band is its split signal times the factor, and
            // the detector read it times Input: so what went out reads as
            // what came in, Input taken back off and the factor put on.
            const auto detected = settings.rms ? band.meanSquare : band.peak;
            const auto through = factor / band.input;
            loudestIn[static_cast<size_t>(b)] = std::max(loudestIn[static_cast<size_t>(b)], detected);
            loudestOut[static_cast<size_t>(b)] = std::max(loudestOut[static_cast<size_t>(b)],
                                                          settings.rms ? detected * through * through : detected * through);
        }

        output += (outputTarget - output) * gainSmoothing;
        for (int c = 0; c < used; ++c)
            audio[c][i] = static_cast<float>(sum[c] * output);
    }

    for (size_t b = 0; b < lastLevels.size(); ++b)
    {
        const auto toAmplitude = [this] (double reading)
        {
            return static_cast<float>(settings.rms ? std::sqrt(reading) : reading);
        };
        lastLevels[b] = { toAmplitude(loudestIn[b]), toAmplitude(loudestOut[b]) };
    }
}
}
