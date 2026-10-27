#include "ReduxEngine.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace rhino
{
namespace
{
// A fourth-order Butterworth low-pass is two biquads with these pole Qs.
constexpr double butterworthQ[2] { 0.54119610, 1.30656296 };

// Each channel's clock jitters from a seed of its own, so with Jitter up the
// two sides of a stereo signal are sampled at different moments.
constexpr std::uint32_t seeds[ReduxEngine::maximumChannels] { 0x9e3779b9u, 0x7f4a7c15u };

double uniform(std::uint32_t& state)
{
    // xorshift32: cheap, allocation-free, and the same sequence on every run.
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<double>(state) / 4294967296.0;
}
}

void ReduxEngine::prepare(double sampleRate)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    mixSmoothing = 1.0 - std::exp(-1.0 / (0.02 * rate));
    updateFilters();
    reset();
}

void ReduxEngine::reset()
{
    for (size_t c = 0; c < channels.size(); ++c)
    {
        auto& channel = channels[c];
        for (auto& filter : channel.pre) filter.reset();
        for (auto& filter : channel.post) filter.reset();
        channel.noise = seeds[c];
        channel.phase = 0.0;
        channel.held = 0.0;
        channel.period = nextPeriod(channel);
        // The first input sample is taken at once rather than a period late.
        channel.phase = channel.period;
        channel.mix = std::clamp(static_cast<double>(settings.dryWet), 0.0, 1.0);
    }
}

void ReduxEngine::setSettings(const Settings& next)
{
    const auto filtersMoved = next.rateHz != settings.rateHz || next.postOctave != settings.postOctave;
    settings = next;
    if (filtersMoved)
        updateFilters();
}

void ReduxEngine::updateFilters()
{
    const auto nyquistGuard = rate * 0.45;
    const auto sampleRateHz = std::max(1.0, static_cast<double>(settings.rateHz));
    const auto preCut = std::clamp(sampleRateHz * 0.45, 20.0, nyquistGuard);
    const auto postCut = std::clamp(sampleRateHz * 0.5 * std::pow(2.0, std::clamp(settings.postOctave, -3.0f, 3.0f)),
                                    20.0, nyquistGuard);
    for (auto& channel : channels)
        for (int stage = 0; stage < 2; ++stage)
        {
            channel.pre[static_cast<size_t>(stage)].coefficients
                = eqdetail::lowPass(2.0 * eqdetail::pi * preCut / rate, butterworthQ[stage]);
            channel.post[static_cast<size_t>(stage)].coefficients
                = eqdetail::lowPass(2.0 * eqdetail::pi * postCut / rate, butterworthQ[stage]);
        }
}

// A hold period, as a fraction of the nominal one: one with no jitter, and
// anywhere from a tenth to nearly twice it with all of it.
double ReduxEngine::nextPeriod(Channel& channel) const
{
    const auto shake = uniform(channel.noise) * 2.0 - 1.0;
    return 1.0 + std::clamp(static_cast<double>(settings.jitter), 0.0, 1.0) * 0.9 * shake;
}

double ReduxEngine::quantise(double x, float bits, float shape, bool dcShift)
{
    const auto steps = std::pow(2.0, std::clamp(static_cast<double>(bits), 1.0, 16.0) - 1.0);
    // Shape is mu-law's mu, from none to 255, which is what a telephone uses.
    const auto mu = std::pow(256.0, std::clamp(static_cast<double>(shape), 0.0, 1.0)) - 1.0;
    const auto compand = mu > 1.0e-6;
    const auto sign = x < 0.0 ? -1.0 : 1.0;
    auto value = compand ? sign * std::log1p(mu * std::abs(x)) / std::log1p(mu) : x;
    // Shifted by half a step and back, so the steps straddle zero instead of
    // one sitting on it. Taken back off afterwards: the chain is floating
    // point, and a DC offset left in would only push the next device off
    // centre.
    const auto offset = dcShift ? 0.5 / steps : 0.0;
    value = std::floor((value + offset) * steps + 0.5) / steps - offset;
    if (!compand)
        return value;
    const auto magnitude = std::abs(value);
    return (value < 0.0 ? -1.0 : 1.0) * std::expm1(magnitude * std::log1p(mu)) / mu;
}

void ReduxEngine::process(float* const* audio, int numChannels, int count)
{
    const auto step = std::max(1.0, static_cast<double>(settings.rateHz)) / rate;
    const auto pre = settings.preFilter;
    const auto post = settings.postFilter;
    const auto bits = settings.bits;
    const auto shape = settings.shape;
    const auto dcShift = settings.dcShift;
    const auto mixTarget = std::clamp(static_cast<double>(settings.dryWet), 0.0, 1.0);

    for (int c = 0; c < std::min(numChannels, maximumChannels); ++c)
    {
        auto& channel = channels[static_cast<size_t>(c)];
        auto* samples = audio[c];
        for (int i = 0; i < count; ++i)
        {
            const auto dry = static_cast<double>(samples[i]);
            // Both filters run every sample, whether they are heard or not,
            // so switching one in starts it from the signal rather than from
            // a step.
            auto sampled = dry;
            for (auto& filter : channel.pre)
                sampled = filter.process(sampled);
            if (!pre)
                sampled = dry;
            channel.phase += step;
            if (channel.phase >= channel.period)
            {
                channel.phase = std::fmod(channel.phase - channel.period, 1.0);
                channel.period = nextPeriod(channel);
                channel.held = quantise(sampled, bits, shape, dcShift);
            }
            auto wet = channel.held;
            auto smoothed = wet;
            for (auto& filter : channel.post)
                smoothed = filter.process(smoothed);
            if (post)
                wet = smoothed;
            channel.mix += (mixTarget - channel.mix) * mixSmoothing;
            // Weighted rather than interpolated, so all wet is exactly the wet
            // signal and all dry exactly the dry one.
            samples[i] = static_cast<float>(dry * (1.0 - channel.mix) + wet * channel.mix);
        }
    }
}

void ReduxEngine::picture(const Settings& settings, double sampleRate, float* input, float* output, int count)
{
    constexpr double frequency = 375.0;
    constexpr double amplitude = 0.9;
    ReduxEngine engine;
    auto shown = settings;
    shown.dryWet = 1.0f;
    engine.setSettings(shown);
    engine.prepare(sampleRate);
    // Settled for as long again as is shown, so the filters' start is not
    // part of the picture.
    std::vector<float> audio(static_cast<size_t>(count) * 2);
    for (size_t i = 0; i < audio.size(); ++i)
        audio[i] = static_cast<float>(amplitude * std::sin(2.0 * 3.14159265358979323846 * frequency
                                                           * static_cast<double>(i) / sampleRate));
    for (int i = 0; i < count; ++i)
        input[i] = audio[static_cast<size_t>(count + i)];
    float* channel[1] { audio.data() };
    engine.process(channel, 1, static_cast<int>(audio.size()));
    for (int i = 0; i < count; ++i)
        output[i] = audio[static_cast<size_t>(count + i)];
}
}
