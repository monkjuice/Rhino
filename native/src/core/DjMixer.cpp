#include "DjMixer.h"
#include <algorithm>

// The mixer's DSP: see DjMixer.h.

namespace rhino
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double butterworthQ = 0.70710678118654752;

float clamp01(float x) noexcept { return std::clamp(x, 0.0f, 1.0f); }
}

DjBiquad DjBiquad::lowPass(double cutoff, double rate, double q)
{
    DjBiquad f;
    const auto w0 = 2.0 * pi * std::clamp(cutoff, 10.0, rate * 0.49) / rate;
    const auto alpha = std::sin(w0) / (2.0 * q);
    const auto cw = std::cos(w0);
    const auto a0 = 1.0 + alpha;
    f.b0 = (1.0 - cw) / 2.0 / a0;
    f.b1 = (1.0 - cw) / a0;
    f.b2 = f.b0;
    f.a1 = -2.0 * cw / a0;
    f.a2 = (1.0 - alpha) / a0;
    return f;
}

DjBiquad DjBiquad::highPass(double cutoff, double rate, double q)
{
    DjBiquad f;
    const auto w0 = 2.0 * pi * std::clamp(cutoff, 10.0, rate * 0.49) / rate;
    const auto alpha = std::sin(w0) / (2.0 * q);
    const auto cw = std::cos(w0);
    const auto a0 = 1.0 + alpha;
    f.b0 = (1.0 + cw) / 2.0 / a0;
    f.b1 = -(1.0 + cw) / a0;
    f.b2 = f.b0;
    f.a1 = -2.0 * cw / a0;
    f.a2 = (1.0 - alpha) / a0;
    return f;
}

void DjCrossover::set(double cutoff, double rate)
{
    for (auto& channel : lows)
        for (auto& stage : channel)
        {
            const auto state1 = stage.z1, state2 = stage.z2;
            stage = DjBiquad::lowPass(cutoff, rate, butterworthQ);
            stage.z1 = state1;
            stage.z2 = state2;
        }
    for (auto& channel : highs)
        for (auto& stage : channel)
        {
            const auto state1 = stage.z1, state2 = stage.z2;
            stage = DjBiquad::highPass(cutoff, rate, butterworthQ);
            stage.z1 = state1;
            stage.z2 = state2;
        }
}

void DjCrossover::reset() noexcept
{
    for (auto& channel : lows) for (auto& stage : channel) stage.reset();
    for (auto& channel : highs) for (auto& stage : channel) stage.reset();
}

void DjCrossover::split(int channel, float in, float& low, float& high) noexcept
{
    auto& l = lows[static_cast<size_t>(channel)];
    auto& h = highs[static_cast<size_t>(channel)];
    low = l[1].process(l[0].process(in));
    high = h[1].process(h[0].process(in));
}

void DjIsolator::prepare(double rate)
{
    lowSplit.set(lowCrossoverHz, rate);
    highSplit.set(highCrossoverHz, rate);
    smoothing = static_cast<float>(1.0 - std::exp(-1.0 / (0.01 * rate)));
    reset();
}

void DjIsolator::reset() noexcept
{
    lowSplit.reset();
    highSplit.reset();
    current = target;
}

void DjIsolator::setGainsDb(float low, float mid, float high) noexcept
{
    target = {decibelsToGain(low), decibelsToGain(mid), decibelsToGain(high)};
}

void DjIsolator::process(float* left, float* right, int frames) noexcept
{
    float* channels[2] {left, right};
    for (int f = 0; f < frames; ++f)
    {
        for (int b = 0; b < 3; ++b)
            current[static_cast<size_t>(b)] += smoothing * (target[static_cast<size_t>(b)] - current[static_cast<size_t>(b)]);
        for (int c = 0; c < 2; ++c)
        {
            if (channels[c] == nullptr) continue;
            float low = 0.0f, rest = 0.0f, mid = 0.0f, high = 0.0f;
            lowSplit.split(c, channels[c][f], low, rest);
            highSplit.split(c, rest, mid, high);
            channels[c][f] = low * current[0] + mid * current[1] + high * current[2];
        }
    }
}

void DjColourFilter::prepare(double sampleRate)
{
    rate = sampleRate;
    reset();
}

void DjColourFilter::reset() noexcept
{
    ic1 = {};
    ic2 = {};
    amount = targetAmount;
    resonance = targetResonance;
}

void DjColourFilter::set(float newAmount, float newResonance) noexcept
{
    targetAmount = std::clamp(newAmount, -1.0f, 1.0f);
    targetResonance = clamp01(newResonance);
}

// A state-variable filter in the topology-preserving form, whose cutoff
// follows the knob on a log scale: the low-pass sweeps from 20 kHz down to
// 60 Hz across the left half, the high-pass from 20 Hz up to 10 kHz across
// the right half.
void DjColourFilter::process(float* left, float* right, int frames) noexcept
{
    float* channels[2] {left, right};
    const auto smoothing = static_cast<float>(1.0 - std::exp(-1.0 / (0.01 * rate)));
    for (int f = 0; f < frames; ++f)
    {
        amount += smoothing * (targetAmount - amount);
        resonance += smoothing * (targetResonance - resonance);
        const auto magnitude = std::abs(amount);
        constexpr float dead = 0.04f;
        if (magnitude <= dead)
        {
            // Through, with the state kept moving so re-engaging is seamless.
            for (int c = 0; c < 2; ++c)
            {
                if (channels[c] == nullptr) continue;
                ic1[static_cast<size_t>(c)] = ic2[static_cast<size_t>(c)] = 0.0f;
            }
            continue;
        }
        const auto travel = (magnitude - dead) / (1.0f - dead);
        const auto lowPass = amount < 0.0f;
        const auto cutoff = lowPass ? 20000.0 * std::pow(60.0 / 20000.0, static_cast<double>(travel))
                                    : 20.0 * std::pow(10000.0 / 20.0, static_cast<double>(travel));
        const auto g = static_cast<float>(std::tan(pi * std::min(cutoff, rate * 0.45) / rate));
        const auto k = 2.0f - 1.9f * resonance;   // Q from 0.5 to 10
        const auto a1 = 1.0f / (1.0f + g * (g + k));
        const auto a2 = g * a1;
        const auto a3 = g * a2;
        // The filter fades in over the dead band's edge rather than switching.
        const auto blend = std::min(1.0f, travel * 8.0f);
        for (int c = 0; c < 2; ++c)
        {
            if (channels[c] == nullptr) continue;
            auto& s1 = ic1[static_cast<size_t>(c)];
            auto& s2 = ic2[static_cast<size_t>(c)];
            const auto v0 = channels[c][f];
            const auto v3 = v0 - s2;
            const auto v1 = a1 * s1 + a2 * v3;
            const auto v2 = s2 + a2 * s1 + a3 * v3;
            s1 = 2.0f * v1 - s1;
            s2 = 2.0f * v2 - s2;
            const auto filtered = lowPass ? v2 : v0 - k * v1 - v2;
            channels[c][f] = v0 + blend * (filtered - v0);
        }
    }
}

void DjChannelStrip::prepare(double rate)
{
    isolator.prepare(rate);
    colour.prepare(rate);
    trim.prepare(rate, 0.01);
    faderGain.prepare(rate, 0.005);
    reset();
}

void DjChannelStrip::reset() noexcept
{
    isolator.reset();
    colour.reset();
    trim.current = trim.target = decibelsToGain(trimDb.load(std::memory_order_relaxed));
    faderGain.current = faderGain.target = faderGainFor(fader.load(std::memory_order_relaxed));
    meter.store(0.0f, std::memory_order_relaxed);
}

float DjChannelStrip::faderGainFor(float position) noexcept
{
    const auto p = clamp01(position);
    return p * p;
}

void DjChannelStrip::processPreFader(float* left, float* right, int frames) noexcept
{
    trim.target = decibelsToGain(std::clamp(trimDb.load(std::memory_order_relaxed), minimumTrimDb, maximumTrimDb));
    for (int f = 0; f < frames; ++f)
    {
        const auto gain = trim.next();
        left[f] *= gain;
        if (right != nullptr) right[f] *= gain;
    }
    isolator.setGainsDb(lowDb.load(std::memory_order_relaxed), midDb.load(std::memory_order_relaxed),
                        highDb.load(std::memory_order_relaxed));
    isolator.process(left, right, frames);
    colour.set(filter.load(std::memory_order_relaxed), resonance.load(std::memory_order_relaxed));
    colour.process(left, right, frames);
    float peak = 0.0f;
    for (int f = 0; f < frames; ++f)
        peak = std::max(peak, std::max(std::abs(left[f]), right != nullptr ? std::abs(right[f]) : 0.0f));
    meter.store(peak, std::memory_order_relaxed);
}

void DjChannelStrip::applyFader(float* left, float* right, int frames) noexcept
{
    faderGain.target = faderGainFor(fader.load(std::memory_order_relaxed));
    for (int f = 0; f < frames; ++f)
    {
        const auto gain = faderGain.next();
        left[f] *= gain;
        if (right != nullptr) right[f] *= gain;
    }
}

void DjCrossfader::gains(float& a, float& b) const noexcept
{
    const auto x = std::clamp(position.load(std::memory_order_relaxed), -1.0f, 1.0f);
    const auto c = clamp01(curve.load(std::memory_order_relaxed));
    const auto t = (x + 1.0f) * 0.5f;
    const auto smoothA = std::cos(t * static_cast<float>(pi) * 0.5f);
    const auto smoothB = std::sin(t * static_cast<float>(pi) * 0.5f);
    constexpr float edge = 0.04f;
    const auto sharpA = t < 1.0f - edge ? 1.0f : (1.0f - t) / edge;
    const auto sharpB = t > edge ? 1.0f : t / edge;
    a = smoothA + c * (sharpA - smoothA);
    b = smoothB + c * (sharpB - smoothB);
}

void DjMasterSection::prepare(double rate)
{
    isolator.prepare(rate);
    level.prepare(rate, 0.01);
    reset();
}

void DjMasterSection::reset() noexcept
{
    isolator.reset();
    level.current = level.target = decibelsToGain(levelDb.load(std::memory_order_relaxed));
    meterLeft.store(0.0f, std::memory_order_relaxed);
    meterRight.store(0.0f, std::memory_order_relaxed);
}

void DjMasterSection::process(float* left, float* right, int frames) noexcept
{
    isolator.setGainsDb(lowDb.load(std::memory_order_relaxed), midDb.load(std::memory_order_relaxed),
                        highDb.load(std::memory_order_relaxed));
    isolator.process(left, right, frames);
    level.target = decibelsToGain(std::clamp(levelDb.load(std::memory_order_relaxed), minimumLevelDb, maximumLevelDb));
    float peakLeft = 0.0f, peakRight = 0.0f;
    for (int f = 0; f < frames; ++f)
    {
        const auto gain = level.next();
        left[f] = softClip(left[f] * gain);
        peakLeft = std::max(peakLeft, std::abs(left[f]));
        if (right != nullptr)
        {
            right[f] = softClip(right[f] * gain);
            peakRight = std::max(peakRight, std::abs(right[f]));
        }
    }
    meterLeft.store(peakLeft, std::memory_order_relaxed);
    meterRight.store(right != nullptr ? peakRight : peakLeft, std::memory_order_relaxed);
}

const char* DjBeatFx::typeName(Type t)
{
    switch (t)
    {
        case Type::echo: return "Echo";
        case Type::delay: return "Delay";
        case Type::flanger: return "Flanger";
        case Type::phaser: return "Phaser";
        case Type::filter: return "Filter";
        case Type::reverb: return "Reverb";
        case Type::roll: return "Roll";
        case Type::trans: return "Trans";
        case Type::count: break;
    }
    return "";
}

void DjBeatFx::prepare(double sampleRate, int)
{
    rate = sampleRate;
    maxDelay = static_cast<int>(std::ceil(longestSeconds * rate)) + 4;
    for (auto& channel : line)
        channel.assign(static_cast<size_t>(maxDelay), 0.0f);
    static constexpr double combSeconds[4] {0.0297, 0.0371, 0.0411, 0.0437};
    static constexpr double allpassSeconds[2] {0.005, 0.0017};
    for (int c = 0; c < 2; ++c)
    {
        for (int i = 0; i < 4; ++i)
            combs[static_cast<size_t>(c)][static_cast<size_t>(i)].assign(
                static_cast<size_t>(std::max(8, static_cast<int>((combSeconds[i] + c * 0.0011) * rate))), 0.0f);
        for (int i = 0; i < 2; ++i)
            allpasses[static_cast<size_t>(c)][static_cast<size_t>(i)].assign(
                static_cast<size_t>(std::max(8, static_cast<int>((allpassSeconds[i] + c * 0.0003) * rate))), 0.0f);
    }
    wet.prepare(rate, 0.02);
    dry.prepare(rate, 0.02);
    reset();
}

void DjBeatFx::reset() noexcept
{
    for (auto& channel : line) std::fill(channel.begin(), channel.end(), 0.0f);
    for (auto& channel : combs) for (auto& comb : channel) std::fill(comb.begin(), comb.end(), 0.0f);
    for (auto& channel : allpasses) for (auto& pass : channel) std::fill(pass.begin(), pass.end(), 0.0f);
    writeIndex = 0;
    combIndex = {};
    combFilter = {};
    allpassIndex = {};
    loopLow = {};
    loopHigh = {};
    ic1 = {};
    ic2 = {};
    phaserState = {};
    rollFilled = rollLength = rollIndex = 0;
    rolling = false;
    wet.current = wet.target = 0.0f;
    dry.current = dry.target = 1.0f;
    wasActive = false;
}

void DjBeatFx::process(float* left, float* right, int frames, double beatSeconds, double beatPosition) noexcept
{
    if (maxDelay <= 0 || left == nullptr) return;
    const auto currentType = std::clamp(type.load(std::memory_order_relaxed), 0, typeCount - 1);
    if (currentType != lastType)
    {
        // A type change clears the lines rather than playing one effect's
        // tail through another's.
        const auto keepWet = wet.current;
        reset();
        wet.current = keepWet;
        lastType = currentType;
    }
    const auto active = on.load(std::memory_order_relaxed);
    const auto amount = clamp01(depth.load(std::memory_order_relaxed));
    const auto beatCount = std::clamp(static_cast<double>(beats.load(std::memory_order_relaxed)), 1.0 / 16.0, 4.0);
    const auto seconds = std::clamp(beatCount * std::max(0.05, beatSeconds), 0.001, longestSeconds - 0.01);
    // Where in its cycle a swept effect is: one cycle per `beats` beats.
    auto phase = std::fmod(beatPosition / beatCount, 1.0);
    if (phase < 0.0) phase += 1.0;
    float* r = right != nullptr ? right : left;
    switch (static_cast<Type>(currentType))
    {
        case Type::echo: processEcho(left, r, frames, seconds, amount, active, false); break;
        case Type::delay: processEcho(left, r, frames, seconds, amount, active, true); break;
        case Type::flanger: processFlanger(left, r, frames, seconds, amount, phase, active); break;
        case Type::phaser: processPhaser(left, r, frames, seconds, amount, phase, active); break;
        case Type::filter: processFilter(left, r, frames, seconds, amount, phase, active); break;
        case Type::reverb: processReverb(left, r, frames, amount, static_cast<float>(std::min(0.97, 0.6 + beatCount * 0.09)), active); break;
        case Type::roll: processRoll(left, r, frames, seconds, amount, active); break;
        case Type::trans: processTrans(left, r, frames, seconds, amount, phase, active); break;
        case Type::count: break;
    }
    wasActive = active;
}

// Echo and delay keep playing their line after they are switched off, so the
// repeats ring out rather than being cut; only the feed stops. Delay bounces
// between the sides. Both darken each repeat.
void DjBeatFx::processEcho(float* l, float* r, int frames, double delaySeconds, float amount, bool active, bool pingPong) noexcept
{
    const auto delay = std::clamp(static_cast<int>(delaySeconds * rate), 1, maxDelay - 1);
    const auto feedback = 0.35f + 0.5f * amount;
    wet.target = 0.3f + 0.7f * amount;
    const auto lowCoefficient = static_cast<float>(1.0 - std::exp(-2.0 * pi * 150.0 / rate));
    const auto highCoefficient = static_cast<float>(1.0 - std::exp(-2.0 * pi * 5000.0 / rate));
    float* channels[2] {l, r};
    for (int f = 0; f < frames; ++f)
    {
        const auto readIndex = (writeIndex - delay + maxDelay) % maxDelay;
        const auto wetGain = wet.next();
        float delayed[2];
        for (int c = 0; c < 2; ++c)
            delayed[c] = line[static_cast<size_t>(c)][static_cast<size_t>(readIndex)];
        for (int c = 0; c < 2; ++c)
        {
            // Feedback through a band: the lows and the highs fall away with
            // each repeat, as tape does.
            auto& low = loopLow[static_cast<size_t>(c)];
            auto& high = loopHigh[static_cast<size_t>(c)];
            const auto source = pingPong ? delayed[1 - c] : delayed[c];
            low += lowCoefficient * (source - low);
            high += highCoefficient * (source - high);
            const auto banded = high - low;
            const auto input = active ? channels[c][f] : 0.0f;
            line[static_cast<size_t>(c)][static_cast<size_t>(writeIndex)] = input + banded * feedback;
            channels[c][f] = channels[c][f] + delayed[c] * wetGain;
        }
        writeIndex = (writeIndex + 1) % maxDelay;
    }
}

void DjBeatFx::processFlanger(float* l, float* r, int frames, double periodSeconds, float amount, double cycle, bool active) noexcept
{
    wet.target = active ? 0.5f * amount + 0.2f : 0.0f;
    const auto feedback = 0.3f + 0.45f * amount;
    // The sweep rides the beat: one cycle per period, starting where the
    // master's beat count says it should.
    const auto phaseStep = 2.0 * pi / (std::max(0.05, periodSeconds) * rate);
    auto phase = cycle * 2.0 * pi;
    const auto minDelay = 0.0006 * rate, maxDelayFrames = 0.007 * rate;
    float* channels[2] {l, r};
    for (int f = 0; f < frames; ++f)
    {
        const auto sweep = 0.5 - 0.5 * std::cos(phase);
        const auto delayFrames = minDelay + (maxDelayFrames - minDelay) * sweep;
        const auto whole = static_cast<int>(delayFrames);
        const auto fraction = static_cast<float>(delayFrames - whole);
        const auto wetGain = wet.next();
        for (int c = 0; c < 2; ++c)
        {
            auto& channel = line[static_cast<size_t>(c)];
            const auto i0 = (writeIndex - whole + maxDelay) % maxDelay;
            const auto i1 = (i0 - 1 + maxDelay) % maxDelay;
            const auto delayed = channel[static_cast<size_t>(i0)] * (1.0f - fraction) + channel[static_cast<size_t>(i1)] * fraction;
            channel[static_cast<size_t>(writeIndex)] = channels[c][f] + delayed * feedback;
            channels[c][f] = channels[c][f] + delayed * wetGain;
        }
        writeIndex = (writeIndex + 1) % maxDelay;
        phase += phaseStep;
        if (phase >= 2.0 * pi) phase -= 2.0 * pi;
    }
}

void DjBeatFx::processPhaser(float* l, float* r, int frames, double periodSeconds, float amount, double cycle, bool active) noexcept
{
    wet.target = active ? 0.4f + 0.5f * amount : 0.0f;
    auto phase = cycle * 2.0 * pi;
    const auto phaseStep = 2.0 * pi / (std::max(0.05, periodSeconds) * rate);
    float* channels[2] {l, r};
    for (int f = 0; f < frames; ++f)
    {
        const auto sweep = 0.5 - 0.5 * std::cos(phase);
        const auto frequency = 200.0 * std::pow(4000.0 / 200.0, sweep);
        const auto coefficient = static_cast<float>((std::tan(pi * frequency / rate) - 1.0) / (std::tan(pi * frequency / rate) + 1.0));
        const auto wetGain = wet.next();
        for (int c = 0; c < 2; ++c)
        {
            auto x = channels[c][f];
            auto& states = phaserState[static_cast<size_t>(c)];
            for (auto& s : states)
            {
                const auto y = coefficient * x + s;
                s = x - coefficient * y;
                x = y;
            }
            channels[c][f] = channels[c][f] + x * wetGain;
        }
        phase += phaseStep;
        if (phase >= 2.0 * pi) phase -= 2.0 * pi;
    }
}

void DjBeatFx::processFilter(float* l, float* r, int frames, double periodSeconds, float amount, double cycle, bool active) noexcept
{
    wet.target = active ? 1.0f : 0.0f;
    auto phase = cycle * 2.0 * pi;
    const auto phaseStep = 2.0 * pi / (std::max(0.05, periodSeconds) * rate);
    const auto k = 2.0f - 1.6f * amount;
    float* channels[2] {l, r};
    for (int f = 0; f < frames; ++f)
    {
        const auto sweep = 0.5 - 0.5 * std::cos(phase);
        const auto cutoff = 150.0 * std::pow(12000.0 / 150.0, sweep * (0.3 + 0.7 * amount));
        const auto g = static_cast<float>(std::tan(pi * std::min(cutoff, rate * 0.45) / rate));
        const auto a1 = 1.0f / (1.0f + g * (g + k));
        const auto a2 = g * a1;
        const auto a3 = g * a2;
        const auto wetGain = wet.next();
        for (int c = 0; c < 2; ++c)
        {
            auto& s1 = ic1[static_cast<size_t>(c)];
            auto& s2 = ic2[static_cast<size_t>(c)];
            const auto v0 = channels[c][f];
            const auto v3 = v0 - s2;
            const auto v1 = a1 * s1 + a2 * v3;
            const auto v2 = s2 + a2 * s1 + a3 * v3;
            s1 = 2.0f * v1 - s1;
            s2 = 2.0f * v2 - s2;
            channels[c][f] = v0 + wetGain * (v2 - v0);
        }
        phase += phaseStep;
        if (phase >= 2.0 * pi) phase -= 2.0 * pi;
    }
}

// Schroeder: four combs in parallel into two allpasses, a little different
// on the right, with the beat length setting how long it rings.
void DjBeatFx::processReverb(float* l, float* r, int frames, float amount, float decay, bool active) noexcept
{
    wet.target = active ? 0.6f * amount : 0.0f;
    const auto damping = 0.3f;
    float* channels[2] {l, r};
    for (int f = 0; f < frames; ++f)
    {
        const auto wetGain = wet.next();
        for (int c = 0; c < 2; ++c)
        {
            const auto input = channels[c][f];
            float sum = 0.0f;
            for (int i = 0; i < 4; ++i)
            {
                auto& comb = combs[static_cast<size_t>(c)][static_cast<size_t>(i)];
                auto& index = combIndex[static_cast<size_t>(c)][static_cast<size_t>(i)];
                auto& filtered = combFilter[static_cast<size_t>(c)][static_cast<size_t>(i)];
                const auto out = comb[static_cast<size_t>(index)];
                filtered += damping * (out - filtered);
                comb[static_cast<size_t>(index)] = input + filtered * decay;
                index = (index + 1) % static_cast<int>(comb.size());
                sum += out;
            }
            sum *= 0.25f;
            for (int i = 0; i < 2; ++i)
            {
                auto& pass = allpasses[static_cast<size_t>(c)][static_cast<size_t>(i)];
                auto& index = allpassIndex[static_cast<size_t>(c)][static_cast<size_t>(i)];
                const auto delayed = pass[static_cast<size_t>(index)];
                const auto out = -sum + delayed;
                pass[static_cast<size_t>(index)] = sum + delayed * 0.5f;
                index = (index + 1) % static_cast<int>(pass.size());
                sum = out;
            }
            channels[c][f] = input + sum * wetGain;
        }
    }
}

// Roll captures the beats after it is switched on and repeats them for as
// long as it stays on.
void DjBeatFx::processRoll(float* l, float* r, int frames, double lengthSeconds, float amount, bool active) noexcept
{
    const auto length = std::clamp(static_cast<int>(lengthSeconds * rate), 16, maxDelay - 1);
    if (active && !rolling)
    {
        rolling = true;
        rollFilled = 0;
        rollLength = length;
        rollIndex = 0;
    }
    if (!active)
        rolling = false;
    wet.target = rolling ? amount : 0.0f;
    float* channels[2] {l, r};
    for (int f = 0; f < frames; ++f)
    {
        const auto wetGain = wet.next();
        if (!rolling)
        {
            // The line keeps a picture of the last beats so the next roll
            // has something to start on.
            for (int c = 0; c < 2; ++c)
                line[static_cast<size_t>(c)][static_cast<size_t>(writeIndex)] = channels[c][f];
            writeIndex = (writeIndex + 1) % maxDelay;
            continue;
        }
        if (rollFilled < rollLength)
        {
            for (int c = 0; c < 2; ++c)
                line[static_cast<size_t>(c)][static_cast<size_t>(rollFilled)] = channels[c][f];
            ++rollFilled;
            continue;
        }
        for (int c = 0; c < 2; ++c)
        {
            const auto looped = line[static_cast<size_t>(c)][static_cast<size_t>(rollIndex)];
            channels[c][f] = channels[c][f] + wetGain * (looped - channels[c][f]);
        }
        rollIndex = (rollIndex + 1) % rollLength;
    }
}

// Trans chops the signal at the beat: open for the first half of each
// period, closed to (1 - depth) for the second, with a millisecond's slope.
void DjBeatFx::processTrans(float* l, float* r, int frames, double periodSeconds, float amount, double cycle, bool active) noexcept
{
    wet.target = active ? 1.0f : 0.0f;
    const auto period = std::max(0.02, periodSeconds);
    auto seconds = cycle * period;
    const auto slopeFrames = std::max(1.0, 0.001 * rate);
    float* channels[2] {l, r};
    for (int f = 0; f < frames; ++f)
    {
        const auto fraction = seconds / period;
        auto gate = fraction < 0.5 ? 1.0f : 1.0f - amount;
        // Slope the edges.
        const auto edgeFrames = std::min(seconds, std::abs(seconds - period * 0.5)) * rate;
        if (edgeFrames < slopeFrames)
        {
            const auto other = fraction < 0.5 ? 1.0f - amount : 1.0f;
            gate = other + (gate - other) * static_cast<float>(edgeFrames / slopeFrames);
        }
        const auto wetGain = wet.next();
        const auto applied = 1.0f + wetGain * (gate - 1.0f);
        for (int c = 0; c < 2; ++c)
            channels[c][f] *= applied;
        seconds += 1.0 / rate;
        if (seconds >= period) seconds -= period;
    }
}
}
