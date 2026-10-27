#include "ErosionEngine.h"
#include <algorithm>
#include <cmath>

namespace rhino
{
namespace
{
constexpr double twoPi = 6.28318530717958647692;

double whiteNoise(std::uint32_t& state)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<double>(state) / 2147483648.0 - 1.0;
}

// Filter Width runs from a narrow band (Q 8) to a wide one (Q 0.4), evenly in
// octaves of Q.
double bandQ(float width)
{
    return 8.0 * std::pow(0.05, std::clamp(static_cast<double>(width), 0.0, 1.0));
}

// Live's band-pass with a peak of one (RBJ's "constant 0 dB peak gain").
Biquad bandPass(double w0, double q)
{
    const auto cosw = std::cos(w0), alpha = std::sin(w0) / (2.0 * q);
    return eqdetail::normalise(alpha, 0.0, -alpha, 1.0 + alpha, -2.0 * cosw, 1.0 - alpha);
}
}

void ErosionEngine::prepare(double sampleRate)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    swing = swingAt48k * rate / 48000.0;
    centre = static_cast<int>(std::ceil(swing)) + 2;
    // Four-point interpolation reads one sample either side of the two it
    // falls between, so the line reaches past the longest delay by two.
    auto length = 16;
    while (length < 2 * centre + 4)
        length *= 2;
    mask = length - 1;
    for (auto& channel : channels)
        channel.line.assign(static_cast<size_t>(length), 0.0f);
    smoothing = 1.0 - std::exp(-1.0 / (0.02 * rate));
    updateFilter();
    reset();
}

void ErosionEngine::reset()
{
    for (auto& channel : channels)
    {
        std::fill(channel.line.begin(), channel.line.end(), 0.0f);
        channel.at = 0;
        channel.band.reset();
    }
    phase = 0.0;
    sharedNoise = 0x2545f491u;
    ownNoise = 0x6c078965u;
    amount = std::clamp(static_cast<double>(settings.amount), 0.0, 1.0);
    blend = std::clamp(static_cast<double>(settings.noiseBlend), 0.0, 1.0);
    width = std::clamp(static_cast<double>(settings.stereoWidth), 0.0, 1.0);
}

void ErosionEngine::setSettings(const Settings& next)
{
    const auto filterMoved = next.frequency != settings.frequency || next.filterWidth != settings.filterWidth;
    settings = next;
    if (filterMoved)
        updateFilter();
}

void ErosionEngine::updateFilter()
{
    const auto frequency = std::clamp(static_cast<double>(settings.frequency), 20.0, rate * 0.45);
    const auto q = bandQ(settings.filterWidth);
    for (auto& channel : channels)
        channel.band.coefficients = bandPass(twoPi * frequency / rate, q);
    // White noise through a band of peak gain one keeps the share of its power
    // that falls in the band: a resonator's noise bandwidth is pi/2 times its
    // -3 dB width, f/Q, against the whole band up to Nyquist. Scaled back up
    // so the noise swings the delay as far as the sine does, whatever the
    // band.
    const auto variance = (1.0 / 3.0) * (0.5 * 3.14159265358979323846 * frequency / q) / (rate * 0.5);
    noiseGain = std::min(60.0, std::sqrt(0.5 / std::max(1.0e-9, variance)));
}

void ErosionEngine::modulators(double& left, double& right)
{
    const auto sineLeft = std::sin(phase);
    // A quarter turn apart at full width: no longer the same modulator, and
    // never simply its inverse.
    const auto sineRight = std::sin(phase + width * twoPi * 0.25);
    phase += twoPi * std::clamp(static_cast<double>(settings.frequency), 20.0, rate * 0.45) / rate;
    if (phase >= twoPi)
        phase -= twoPi;

    // One noise both sides share and one only the right hears, mixed by
    // Stereo Width at constant power.
    const auto shared = whiteNoise(sharedNoise);
    const auto own = whiteNoise(ownNoise);
    const auto rightNoise = std::sqrt(1.0 - width * width) * shared + width * own;
    // Limited to one either way, so however the noise peaks the delay never
    // swings further than the line allows.
    const auto noiseLeft = std::tanh(channels[0].band.process(shared) * noiseGain);
    const auto noiseRight = std::tanh(channels[1].band.process(rightNoise) * noiseGain);
    left = sineLeft + (noiseLeft - sineLeft) * blend;
    right = sineRight + (noiseRight - sineRight) * blend;
}

// Four-point Hermite between the samples either side of the read point; at a
// whole number of samples it is exactly the sample there.
double ErosionEngine::read(const Channel& channel, double delay) const
{
    const auto position = static_cast<double>(channel.at) - delay;
    const auto whole = static_cast<int>(std::floor(position));
    const auto fraction = position - whole;
    const auto at = [&channel, this] (int index) { return static_cast<double>(channel.line[static_cast<size_t>(index & mask)]); };
    const auto x0 = at(whole - 1), x1 = at(whole), x2 = at(whole + 1), x3 = at(whole + 2);
    const auto c1 = 0.5 * (x2 - x0);
    const auto c2 = x0 - 2.5 * x1 + 2.0 * x2 - 0.5 * x3;
    const auto c3 = 0.5 * (x3 - x0) + 1.5 * (x1 - x2);
    return ((c3 * fraction + c2) * fraction + c1) * fraction + x1;
}

void ErosionEngine::process(float* const* audio, int numChannels, int count)
{
    const auto amountTarget = std::clamp(static_cast<double>(settings.amount), 0.0, 1.0);
    const auto blendTarget = std::clamp(static_cast<double>(settings.noiseBlend), 0.0, 1.0);
    const auto widthTarget = std::clamp(static_cast<double>(settings.stereoWidth), 0.0, 1.0);
    const auto active = std::min(numChannels, maximumChannels);
    for (int i = 0; i < count; ++i)
    {
        amount += (amountTarget - amount) * smoothing;
        blend += (blendTarget - blend) * smoothing;
        width += (widthTarget - width) * smoothing;
        double modulation[maximumChannels];
        modulators(modulation[0], modulation[1]);
        for (int c = 0; c < active; ++c)
        {
            auto& channel = channels[static_cast<size_t>(c)];
            channel.at = (channel.at + 1) & mask;
            channel.line[static_cast<size_t>(channel.at)] = audio[c][i];
            const auto delay = centre + swing * amount * modulation[c];
            audio[c][i] = static_cast<float>(read(channel, delay));
        }
    }
}

void ErosionEngine::modulationPicture(const Settings& settings, double sampleRate, float* points, int count)
{
    ErosionEngine engine;
    engine.setSettings(settings);
    engine.prepare(sampleRate);
    // The noise's band takes a few milliseconds to ring up from rest.
    const auto settle = static_cast<int>(sampleRate * 0.01);
    double left = 0.0, right = 0.0;
    for (int i = 0; i < settle; ++i)
        engine.modulators(left, right);
    for (int i = 0; i < count; ++i)
    {
        engine.modulators(left, right);
        points[i] = static_cast<float>(left);
    }
}
}
