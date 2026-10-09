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

// A one-pole low-pass coefficient for a cutoff.
float onePole(double cutoff, double rate) noexcept
{
    return static_cast<float>(1.0 - std::exp(-2.0 * pi * std::clamp(cutoff, 10.0, rate * 0.45) / rate));
}

// Reads a delay line with linear interpolation at a fractional delay.
float readLine(const std::vector<float>& line, int write, double delay) noexcept
{
    const auto size = static_cast<int>(line.size());
    if (size == 0) return 0.0f;
    delay = std::clamp(delay, 0.0, static_cast<double>(size - 2));
    const auto whole = static_cast<int>(delay);
    const auto fraction = static_cast<float>(delay - whole);
    const auto i0 = (write - whole + size * 2) % size;
    const auto i1 = (i0 - 1 + size) % size;
    return line[static_cast<size_t>(i0)] * (1.0f - fraction) + line[static_cast<size_t>(i1)] * fraction;
}
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

DjBiquad DjBiquad::lowShelf(double cutoff, double rate, double gainDb)
{
    DjBiquad f;
    const auto A = std::pow(10.0, gainDb / 40.0);
    const auto w0 = 2.0 * pi * std::clamp(cutoff, 10.0, rate * 0.49) / rate;
    const auto cw = std::cos(w0);
    const auto alpha = std::sin(w0) / 2.0 * std::sqrt(2.0);
    const auto root = 2.0 * std::sqrt(A) * alpha;
    const auto a0 = (A + 1.0) + (A - 1.0) * cw + root;
    f.b0 = A * ((A + 1.0) - (A - 1.0) * cw + root) / a0;
    f.b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cw) / a0;
    f.b2 = A * ((A + 1.0) - (A - 1.0) * cw - root) / a0;
    f.a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cw) / a0;
    f.a2 = ((A + 1.0) + (A - 1.0) * cw - root) / a0;
    return f;
}

DjBiquad DjBiquad::highShelf(double cutoff, double rate, double gainDb)
{
    DjBiquad f;
    const auto A = std::pow(10.0, gainDb / 40.0);
    const auto w0 = 2.0 * pi * std::clamp(cutoff, 10.0, rate * 0.49) / rate;
    const auto cw = std::cos(w0);
    const auto alpha = std::sin(w0) / 2.0 * std::sqrt(2.0);
    const auto root = 2.0 * std::sqrt(A) * alpha;
    const auto a0 = (A + 1.0) - (A - 1.0) * cw + root;
    f.b0 = A * ((A + 1.0) + (A - 1.0) * cw + root) / a0;
    f.b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cw) / a0;
    f.b2 = A * ((A + 1.0) + (A - 1.0) * cw - root) / a0;
    f.a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cw) / a0;
    f.a2 = ((A + 1.0) - (A - 1.0) * cw - root) / a0;
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

void DjIsolator::prepare(double rate, int count)
{
    bands = std::clamp(count, 3, maximumBands);
    if (bands == 3)
    {
        splits[0].set(250.0, rate);
        splits[1].set(3000.0, rate);
    }
    else
    {
        splits[0].set(150.0, rate);
        splits[1].set(600.0, rate);
        splits[2].set(3000.0, rate);
    }
    smoothing = static_cast<float>(1.0 - std::exp(-1.0 / (0.01 * rate)));
    reset();
}

void DjIsolator::reset() noexcept
{
    for (auto& split : splits) split.reset();
    current = target;
}

void DjIsolator::setGainsDb(const float* decibels) noexcept
{
    for (int b = 0; b < bands; ++b)
        target[static_cast<size_t>(b)] = decibelsToGain(decibels[b]);
}

void DjIsolator::process(float* left, float* right, int frames) noexcept
{
    float* channels[2] {left, right};
    for (int f = 0; f < frames; ++f)
    {
        for (int b = 0; b < bands; ++b)
            current[static_cast<size_t>(b)] += smoothing * (target[static_cast<size_t>(b)] - current[static_cast<size_t>(b)]);
        for (int c = 0; c < 2; ++c)
        {
            if (channels[c] == nullptr) continue;
            float band0 = 0.0f, rest = 0.0f, band1 = 0.0f, band2 = 0.0f, band3 = 0.0f;
            splits[0].split(c, channels[c][f], band0, rest);
            if (bands == 3)
            {
                splits[1].split(c, rest, band1, band2);
                channels[c][f] = band0 * current[0] + band1 * current[1] + band2 * current[2];
            }
            else
            {
                float rest2 = 0.0f;
                splits[1].split(c, rest, band1, rest2);
                splits[2].split(c, rest2, band2, band3);
                channels[c][f] = band0 * current[0] + band1 * current[1] + band2 * current[2] + band3 * current[3];
            }
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
            for (int c = 0; c < 2; ++c)
                ic1[static_cast<size_t>(c)] = ic2[static_cast<size_t>(c)] = 0.0f;
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

void DjCompressor::prepare(double rate)
{
    attack = static_cast<float>(1.0 - std::exp(-1.0 / (0.005 * rate)));
    release = static_cast<float>(1.0 - std::exp(-1.0 / (0.12 * rate)));
    smoothing = static_cast<float>(1.0 - std::exp(-1.0 / (0.01 * rate)));
    reset();
}

void DjCompressor::reset() noexcept
{
    envelope = 0.0f;
    amount = targetAmount;
}

void DjCompressor::set(float newAmount) noexcept
{
    targetAmount = clamp01(newAmount);
}

void DjCompressor::process(float* left, float* right, int frames) noexcept
{
    for (int f = 0; f < frames; ++f)
    {
        amount += smoothing * (targetAmount - amount);
        const auto peak = std::max(std::abs(left[f]), right != nullptr ? std::abs(right[f]) : 0.0f);
        envelope += (peak > envelope ? attack : release) * (peak - envelope);
        if (amount < 0.001f) continue;
        const auto threshold = -30.0f * amount;
        const auto ratio = 1.0f + 3.0f * amount;
        const auto levelDb = 20.0f * std::log10(std::max(envelope, 1.0e-6f));
        const auto over = std::max(0.0f, levelDb - threshold);
        const auto reductionDb = over * (1.0f - 1.0f / ratio);
        const auto makeupDb = -threshold * (1.0f - 1.0f / ratio) * 0.6f;
        const auto gain = std::pow(10.0f, (makeupDb - reductionDb) / 20.0f);
        left[f] *= gain;
        if (right != nullptr) right[f] *= gain;
    }
}

void DjChannelStrip::prepare(double rate)
{
    compressor.prepare(rate);
    isolator.prepare(rate, bandCount);
    colour.prepare(rate);
    trim.prepare(rate, 0.01);
    faderGain.prepare(rate, 0.005);
    sendLevel.prepare(rate, 0.005);
    reset();
}

void DjChannelStrip::reset() noexcept
{
    compressor.reset();
    isolator.reset();
    colour.reset();
    trim.current = trim.target = decibelsToGain(trimDb.load(std::memory_order_relaxed));
    faderGain.current = faderGain.target = faderGainFor(fader.load(std::memory_order_relaxed), faderCurve.load(std::memory_order_relaxed));
    sendLevel.current = sendLevel.target = clamp01(send.load(std::memory_order_relaxed));
    meter.store(0.0f, std::memory_order_relaxed);
}

// Gentle comes up fast at the bottom, normal is the square, steep keeps
// most of the travel for the top of the throw.
float DjChannelStrip::faderGainFor(float position, int curve) noexcept
{
    const auto p = clamp01(position);
    switch (curve)
    {
        case 0: return std::sqrt(p);
        case 2: return p * p * p * p;
        default: return p * p;
    }
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
    compressor.set(comp.load(std::memory_order_relaxed));
    compressor.process(left, right, frames);
    float decibels[bandCount];
    for (int b = 0; b < bandCount; ++b)
        decibels[b] = eqDb[static_cast<size_t>(b)].load(std::memory_order_relaxed);
    isolator.setGainsDb(decibels);
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
    faderGain.target = faderGainFor(fader.load(std::memory_order_relaxed), faderCurve.load(std::memory_order_relaxed));
    for (int f = 0; f < frames; ++f)
    {
        const auto gain = faderGain.next();
        left[f] *= gain;
        if (right != nullptr) right[f] *= gain;
    }
}

float DjChannelStrip::sendGain() noexcept
{
    sendLevel.target = clamp01(send.load(std::memory_order_relaxed));
    // Once a block: a send moving over a few blocks is smooth enough.
    sendLevel.current += 0.3f * (sendLevel.target - sendLevel.current);
    return sendLevel.current;
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
    isolator.prepare(rate, 3);
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
    const float decibels[3] {lowDb.load(std::memory_order_relaxed), midDb.load(std::memory_order_relaxed),
                             highDb.load(std::memory_order_relaxed)};
    isolator.setGainsDb(decibels);
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

void DjMicSection::prepare(double sampleRate)
{
    rate = sampleRate;
    level.prepare(rate, 0.01);
    ducking.prepare(rate, 0.05);
    reset();
}

void DjMicSection::reset() noexcept
{
    low = DjBiquad::lowShelf(200.0, rate, lowDb.load(std::memory_order_relaxed));
    high = DjBiquad::highShelf(4000.0, rate, highDb.load(std::memory_order_relaxed));
    lastLowDb = lowDb.load(std::memory_order_relaxed);
    lastHighDb = highDb.load(std::memory_order_relaxed);
    level.current = level.target = decibelsToGain(levelDb.load(std::memory_order_relaxed));
    ducking.current = ducking.target = 1.0f;
    envelope = 0.0f;
    meter.store(0.0f, std::memory_order_relaxed);
}

void DjMicSection::process(const float* input, float* out, int frames, float& duck) noexcept
{
    const auto currentMode = static_cast<Mode>(mode.load(std::memory_order_relaxed));
    if (input == nullptr || currentMode == Mode::off)
    {
        std::fill(out, out + frames, 0.0f);
        ducking.target = 1.0f;
        for (int f = 0; f < frames; ++f) ducking.next();
        duck = ducking.current;
        meter.store(0.0f, std::memory_order_relaxed);
        return;
    }
    const auto wantedLow = lowDb.load(std::memory_order_relaxed), wantedHigh = highDb.load(std::memory_order_relaxed);
    if (wantedLow != lastLowDb)
    {
        const auto z1 = low.z1, z2 = low.z2;
        low = DjBiquad::lowShelf(200.0, rate, wantedLow);
        low.z1 = z1;
        low.z2 = z2;
        lastLowDb = wantedLow;
    }
    if (wantedHigh != lastHighDb)
    {
        const auto z1 = high.z1, z2 = high.z2;
        high = DjBiquad::highShelf(4000.0, rate, wantedHigh);
        high.z1 = z1;
        high.z2 = z2;
        lastHighDb = wantedHigh;
    }
    level.target = decibelsToGain(std::clamp(levelDb.load(std::memory_order_relaxed), minimumLevelDb, maximumLevelDb));
    const auto attack = static_cast<float>(1.0 - std::exp(-1.0 / (0.001 * rate)));
    const auto release = static_cast<float>(1.0 - std::exp(-1.0 / (0.25 * rate)));
    const auto duckGain = decibelsToGain(talkoverDuckDb);
    float peak = 0.0f;
    for (int f = 0; f < frames; ++f)
    {
        auto x = high.process(low.process(input[f]));
        x *= level.next();
        out[f] = x;
        const auto magnitude = std::abs(x);
        peak = std::max(peak, magnitude);
        envelope += (magnitude > envelope ? attack : release) * (magnitude - envelope);
        // Talkover: the master ducks while the mic is spoken into, at
        // -40 dBFS and up, and comes back over a quarter of a second.
        ducking.target = currentMode == Mode::talkover && envelope > 0.01f ? duckGain : 1.0f;
        ducking.next();
    }
    duck = ducking.current;
    meter.store(peak, std::memory_order_relaxed);
}

void DjPitchShifter::prepare(double rate)
{
    window = std::max(64, static_cast<int>(0.05 * rate));
    size = window * 4;
    line.assign(static_cast<size_t>(size), 0.0f);
    reset();
}

void DjPitchShifter::reset() noexcept
{
    std::fill(line.begin(), line.end(), 0.0f);
    write = 0;
    phase = 0.0;
}

void DjPitchShifter::setSemitones(float semitones) noexcept
{
    ratio = std::pow(2.0, std::clamp(semitones, -24.0f, 24.0f) / 12.0);
}

// Two taps whose delays sweep through the window at the rate the shift
// needs, each faded out where it wraps and in where the other wraps, so the
// seams never sound: the Hann halves add to one.
float DjPitchShifter::process(float in) noexcept
{
    if (size == 0) return in;
    line[static_cast<size_t>(write)] = in;
    phase += (1.0 - ratio) / window;
    phase -= std::floor(phase);
    float out = 0.0f;
    for (int tap = 0; tap < 2; ++tap)
    {
        auto fraction = phase + tap * 0.5;
        fraction -= std::floor(fraction);
        const auto delay = fraction * window;
        const auto weight = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * pi * fraction));
        out += weight * readLine(line, write, delay);
    }
    write = (write + 1) % size;
    return out;
}

const char* DjSendFx::typeName(Type t)
{
    switch (t)
    {
        case Type::shortDelay: return "Short Delay";
        case Type::longDelay: return "Long Delay";
        case Type::dubEcho: return "Dub Echo";
        case Type::reverb: return "Reverb";
        case Type::count: break;
    }
    return "";
}

double DjSendFx::secondsFor(Type t, float time) noexcept
{
    const auto k = clamp01(time);
    switch (t)
    {
        case Type::shortDelay: return 0.005 + 0.115 * k;
        case Type::longDelay: return 0.05 + 1.95 * k;
        case Type::dubEcho: return 0.05 + 1.2 * k;
        case Type::reverb: return 0.1 * k;   // pre-delay
        case Type::count: break;
    }
    return 0.1;
}

void DjSendFx::prepare(double sampleRate)
{
    rate = sampleRate;
    maxDelay = static_cast<int>(std::ceil(2.2 * rate)) + 4;
    for (auto& channel : line)
        channel.assign(static_cast<size_t>(maxDelay), 0.0f);
    static constexpr double combSeconds[4] {0.0317, 0.0383, 0.0431, 0.0469};
    static constexpr double allpassSeconds[2] {0.0051, 0.0017};
    for (int c = 0; c < 2; ++c)
    {
        for (int i = 0; i < 4; ++i)
            combs[static_cast<size_t>(c)][static_cast<size_t>(i)].assign(
                static_cast<size_t>(std::max(8, static_cast<int>((combSeconds[i] + c * 0.0013) * rate))), 0.0f);
        for (int i = 0; i < 2; ++i)
            allpasses[static_cast<size_t>(c)][static_cast<size_t>(i)].assign(
                static_cast<size_t>(std::max(8, static_cast<int>((allpassSeconds[i] + c * 0.0003) * rate))), 0.0f);
    }
    wet.prepare(rate, 0.02);
    reset();
}

void DjSendFx::reset() noexcept
{
    for (auto& channel : line) std::fill(channel.begin(), channel.end(), 0.0f);
    for (auto& channel : combs) for (auto& comb : channel) std::fill(comb.begin(), comb.end(), 0.0f);
    for (auto& channel : allpasses) for (auto& pass : channel) std::fill(pass.begin(), pass.end(), 0.0f);
    writeIndex = 0;
    combIndex = {};
    combFilter = {};
    allpassIndex = {};
    loopTone = {};
    wet.current = wet.target = clamp01(mix.load(std::memory_order_relaxed));
    delaySmoothed = 0.0f;
}

void DjSendFx::process(float* left, float* right, int frames) noexcept
{
    if (maxDelay <= 0 || left == nullptr) return;
    const auto currentType = std::clamp(type.load(std::memory_order_relaxed), 0, typeCount - 1);
    if (currentType != lastType)
    {
        reset();
        lastType = currentType;
    }
    const auto kind = static_cast<Type>(currentType);
    const auto amount = clamp01(size.load(std::memory_order_relaxed));
    const auto seconds = secondsFor(kind, time.load(std::memory_order_relaxed));
    const auto toneAmount = clamp01(tone.load(std::memory_order_relaxed));
    wet.target = clamp01(mix.load(std::memory_order_relaxed));
    float* channels[2] {left, right != nullptr ? right : left};
    // The feedback's colour: dark to bright with the tone knob.
    const auto toneCoefficient = onePole(500.0 * std::pow(24.0, static_cast<double>(toneAmount)), rate);
    if (kind == Type::reverb)
    {
        const auto decay = 0.5f + 0.47f * amount;
        const auto preDelay = std::clamp(static_cast<int>(seconds * rate), 0, maxDelay - 1);
        for (int f = 0; f < frames; ++f)
        {
            const auto wetGain = wet.next();
            for (int c = 0; c < 2; ++c)
            {
                auto& channel = line[static_cast<size_t>(c)];
                channel[static_cast<size_t>(writeIndex)] = channels[c][f];
                const auto input = channel[static_cast<size_t>((writeIndex - preDelay + maxDelay) % maxDelay)];
                float sum = 0.0f;
                for (int i = 0; i < 4; ++i)
                {
                    auto& comb = combs[static_cast<size_t>(c)][static_cast<size_t>(i)];
                    auto& index = combIndex[static_cast<size_t>(c)][static_cast<size_t>(i)];
                    auto& filtered = combFilter[static_cast<size_t>(c)][static_cast<size_t>(i)];
                    const auto out = comb[static_cast<size_t>(index)];
                    filtered += toneCoefficient * (out - filtered);
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
                channels[c][f] = sum * wetGain;
            }
            writeIndex = (writeIndex + 1) % maxDelay;
        }
        return;
    }
    // The delays. The time glides so a knob turn bends rather than clicks.
    const auto wantedDelay = static_cast<float>(std::clamp(seconds * rate, 1.0, static_cast<double>(maxDelay - 2)));
    if (delaySmoothed <= 0.0f) delaySmoothed = wantedDelay;
    const auto feedback = kind == Type::dubEcho ? 0.4f + 0.58f * amount : 0.1f + 0.8f * amount;
    const auto highPassCoefficient = onePole(150.0, rate);
    std::array<float, 2> highPassed {};
    for (int f = 0; f < frames; ++f)
    {
        delaySmoothed += 0.001f * (wantedDelay - delaySmoothed);
        const auto wetGain = wet.next();
        for (int c = 0; c < 2; ++c)
        {
            auto& channel = line[static_cast<size_t>(c)];
            const auto delayed = readLine(channel, writeIndex, delaySmoothed);
            auto& toned = loopTone[static_cast<size_t>(c)];
            toned += toneCoefficient * (delayed - toned);
            auto feed = toned;
            if (kind == Type::dubEcho)
            {
                // A dub echo's repeats lose their bottom as well as their top.
                auto& lowPassed = highPassed[static_cast<size_t>(c)];
                lowPassed += highPassCoefficient * (toned - lowPassed);
                feed = toned - lowPassed;
            }
            channel[static_cast<size_t>(writeIndex)] = channels[c][f] + feed * feedback;
            channels[c][f] = delayed * wetGain;
        }
        writeIndex = (writeIndex + 1) % maxDelay;
    }
}

const char* DjBeatFx::typeName(Type t)
{
    switch (t)
    {
        case Type::delay: return "Delay";
        case Type::echo: return "Echo";
        case Type::pingPong: return "Ping Pong";
        case Type::spiral: return "Spiral";
        case Type::helix: return "Helix";
        case Type::reverb: return "Reverb";
        case Type::shimmer: return "Shimmer";
        case Type::flanger: return "Flanger";
        case Type::phaser: return "Phaser";
        case Type::filter: return "Filter";
        case Type::trans: return "Trans";
        case Type::roll: return "Roll";
        case Type::pitch: return "Pitch";
        case Type::vinylBrake: return "Vinyl Brake";
        case Type::count: break;
    }
    return "";
}

double DjBeatFx::secondsFor(double beatSeconds) const noexcept
{
    if (autoTime.load(std::memory_order_relaxed))
    {
        const auto beatCount = std::clamp(static_cast<double>(beats.load(std::memory_order_relaxed)), 1.0 / 16.0, 4.0);
        return std::clamp(beatCount * std::max(0.05, beatSeconds), 0.001, longestSeconds - 0.01);
    }
    return std::clamp(static_cast<double>(manualSeconds.load(std::memory_order_relaxed)), 0.001, longestSeconds - 0.01);
}

void DjBeatFx::prepare(double sampleRate, int maximumBlockSize)
{
    rate = sampleRate;
    maximumBlock = std::max(16, maximumBlockSize);
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
    for (auto& shifter : shifters) shifter.prepare(rate);
    for (auto& channel : dry) channel.assign(static_cast<size_t>(maximumBlock), 0.0f);
    bandLimit.prepare(rate, 3);
    wet.prepare(rate, 0.02);
    reset();
}

void DjBeatFx::reset() noexcept
{
    for (auto& channel : line) std::fill(channel.begin(), channel.end(), 0.0f);
    for (auto& channel : combs) for (auto& comb : channel) std::fill(comb.begin(), comb.end(), 0.0f);
    for (auto& channel : allpasses) for (auto& pass : channel) std::fill(pass.begin(), pass.end(), 0.0f);
    for (auto& shifter : shifters) shifter.reset();
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
    brakeLag = 0.0;
    brakeSpeed = 1.0;
    braking = false;
    bandLimit.reset();
    wet.current = wet.target = 0.0f;
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
    const auto seconds = secondsFor(beatSeconds);
    // Where in its cycle a swept effect is: one cycle per `seconds`.
    const auto cycleSeconds = std::max(0.05, seconds);
    auto phase = std::fmod(beatPosition * std::max(0.05, beatSeconds) / cycleSeconds, 1.0);
    if (phase < 0.0) phase += 1.0;
    float* r = right != nullptr ? right : left;
    // The band limit keeps the dry aside and limits what the effect changed.
    const auto limited = !(bandLow.load(std::memory_order_relaxed) && bandMid.load(std::memory_order_relaxed)
                           && bandHigh.load(std::memory_order_relaxed))
                      && frames <= maximumBlock;
    if (limited)
    {
        std::copy(left, left + frames, dry[0].begin());
        std::copy(r, r + frames, dry[1].begin());
    }
    const auto kind = static_cast<Type>(currentType);
    switch (kind)
    {
        case Type::delay: processDelay(left, r, frames, seconds, amount, active, 0); break;
        case Type::echo: processDelay(left, r, frames, seconds, amount, active, 1); break;
        case Type::pingPong: processDelay(left, r, frames, seconds, amount, active, 2); break;
        case Type::spiral: processDelay(left, r, frames, seconds, amount, active, 3); break;
        case Type::helix: processDelay(left, r, frames, seconds, amount, active, 4); break;
        case Type::reverb: processReverb(left, r, frames, amount, static_cast<float>(std::min(0.97, 0.6 + seconds * 0.18)), active, false); break;
        case Type::shimmer: processReverb(left, r, frames, amount, static_cast<float>(std::min(0.97, 0.7 + seconds * 0.13)), active, true); break;
        case Type::flanger: processFlanger(left, r, frames, cycleSeconds, amount, phase, active); break;
        case Type::phaser: processPhaser(left, r, frames, cycleSeconds, amount, phase, active); break;
        case Type::filter: processFilter(left, r, frames, cycleSeconds, amount, phase, active); break;
        case Type::trans: processTrans(left, r, frames, cycleSeconds, amount, phase, active); break;
        case Type::roll: processRoll(left, r, frames, seconds, amount, active); break;
        case Type::pitch: processPitch(left, r, frames, amount, active); break;
        case Type::vinylBrake: processBrake(left, r, frames, seconds, active); break;
        case Type::count: break;
    }
    if (limited)
    {
        const float decibels[3] {bandLow.load(std::memory_order_relaxed) ? 0.0f : DjIsolator::killDb,
                                 bandMid.load(std::memory_order_relaxed) ? 0.0f : DjIsolator::killDb,
                                 bandHigh.load(std::memory_order_relaxed) ? 0.0f : DjIsolator::killDb};
        bandLimit.setGainsDb(decibels);
        // What the effect changed, in the bands it is wanted on, over the dry.
        for (int f = 0; f < frames; ++f)
        {
            left[f] -= dry[0][static_cast<size_t>(f)];
            r[f] -= dry[1][static_cast<size_t>(f)];
        }
        bandLimit.process(left, r, frames);
        for (int f = 0; f < frames; ++f)
        {
            left[f] += dry[0][static_cast<size_t>(f)];
            r[f] += dry[1][static_cast<size_t>(f)];
        }
    }
}

// The delays: plain (0, cut when off), echo (1, filtered repeats that ring
// out), ping pong (2, bouncing between the sides), spiral (3, repeats
// rising a tone each time) and helix (4, repeats rising a fifth and
// building). All but the plain delay keep playing their line after they
// are switched off; only the feed stops.
void DjBeatFx::processDelay(float* l, float* r, int frames, double delaySeconds, float amount, bool active, int mode) noexcept
{
    const auto delay = std::clamp(static_cast<int>(delaySeconds * rate), 1, maxDelay - 1);
    const auto feedback = mode == 4 ? 0.75f + 0.2f * amount : 0.35f + 0.5f * amount;
    wet.target = active || mode != 0 ? 0.3f + 0.7f * amount : 0.0f;
    const auto lowCoefficient = onePole(150.0, rate);
    const auto highCoefficient = onePole(5000.0, rate);
    if (mode == 3) for (auto& shifter : shifters) shifter.setSemitones(2.0f);
    if (mode == 4) for (auto& shifter : shifters) shifter.setSemitones(7.0f);
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
            auto source = mode == 2 ? delayed[1 - c] : delayed[c];
            if (mode != 0)
            {
                // Feedback through a band: the lows and the highs fall away
                // with each repeat, as tape does.
                auto& low = loopLow[static_cast<size_t>(c)];
                auto& high = loopHigh[static_cast<size_t>(c)];
                low += lowCoefficient * (source - low);
                high += highCoefficient * (source - high);
                source = high - low;
            }
            if (mode == 3 || mode == 4)
                source = shifters[static_cast<size_t>(c)].process(source);
            const auto input = active ? channels[c][f] : 0.0f;
            line[static_cast<size_t>(c)][static_cast<size_t>(writeIndex)] = input + source * feedback;
            channels[c][f] = channels[c][f] + delayed[c] * wetGain;
        }
        writeIndex = (writeIndex + 1) % maxDelay;
    }
}

void DjBeatFx::processFlanger(float* l, float* r, int frames, double periodSeconds, float amount, double cycle, bool active) noexcept
{
    wet.target = active ? 0.5f * amount + 0.2f : 0.0f;
    const auto feedback = 0.3f + 0.45f * amount;
    const auto phaseStep = 2.0 * pi / (std::max(0.05, periodSeconds) * rate);
    auto phase = cycle * 2.0 * pi;
    const auto minDelay = 0.0006 * rate, maxDelayFrames = 0.007 * rate;
    float* channels[2] {l, r};
    for (int f = 0; f < frames; ++f)
    {
        const auto sweep = 0.5 - 0.5 * std::cos(phase);
        const auto delayFrames = minDelay + (maxDelayFrames - minDelay) * sweep;
        const auto wetGain = wet.next();
        for (int c = 0; c < 2; ++c)
        {
            auto& channel = line[static_cast<size_t>(c)];
            const auto delayed = readLine(channel, writeIndex, delayFrames);
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
// on the right, with the time setting how long it rings. Shimmer feeds the
// tail back an octave up.
void DjBeatFx::processReverb(float* l, float* r, int frames, float amount, float decay, bool active, bool shimmer) noexcept
{
    wet.target = active ? 0.6f * amount : 0.0f;
    const auto damping = 0.3f;
    if (shimmer) for (auto& shifter : shifters) shifter.setSemitones(12.0f);
    float* channels[2] {l, r};
    for (int f = 0; f < frames; ++f)
    {
        const auto wetGain = wet.next();
        for (int c = 0; c < 2; ++c)
        {
            auto input = channels[c][f];
            if (shimmer)
            {
                // The last of the tail, pitched up, back into the room.
                const auto last = loopHigh[static_cast<size_t>(c)];
                input += shifters[static_cast<size_t>(c)].process(last) * 0.45f;
            }
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
            loopHigh[static_cast<size_t>(c)] = sum;
            channels[c][f] = channels[c][f] + sum * wetGain;
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

// Pitch shifts the whole signal: the knob runs an octave down to an octave
// up, with unity in the middle.
void DjBeatFx::processPitch(float* l, float* r, int frames, float amount, bool active) noexcept
{
    wet.target = active ? 1.0f : 0.0f;
    const auto semitones = std::round((amount - 0.5f) * 24.0f);
    for (auto& shifter : shifters) shifter.setSemitones(semitones);
    float* channels[2] {l, r};
    for (int f = 0; f < frames; ++f)
    {
        const auto wetGain = wet.next();
        for (int c = 0; c < 2; ++c)
        {
            const auto shifted = shifters[static_cast<size_t>(c)].process(channels[c][f]);
            channels[c][f] = channels[c][f] + wetGain * (shifted - channels[c][f]);
        }
    }
}

// Vinyl brake slows the sound to a stop over the time set, as a turntable
// does when its motor is cut, and lets it go again when switched off. The
// line keeps what has just played, and a read that falls behind the write
// at a shrinking speed is the slowing record.
void DjBeatFx::processBrake(float* l, float* r, int frames, double seconds, bool active) noexcept
{
    if (active && !braking)
    {
        braking = true;
        brakeLag = 0.0;
        brakeSpeed = 1.0;
    }
    if (!active)
        braking = false;
    const auto slowing = 1.0 / std::max(1.0, seconds * rate);
    float* channels[2] {l, r};
    for (int f = 0; f < frames; ++f)
    {
        for (int c = 0; c < 2; ++c)
            line[static_cast<size_t>(c)][static_cast<size_t>(writeIndex)] = channels[c][f];
        if (braking)
        {
            brakeSpeed = std::max(0.0, brakeSpeed - slowing);
            brakeLag = std::min(brakeLag + (1.0 - brakeSpeed), static_cast<double>(maxDelay - 3));
            const auto fade = static_cast<float>(std::min(1.0, brakeSpeed * 10.0));
            for (int c = 0; c < 2; ++c)
                channels[c][f] = readLine(line[static_cast<size_t>(c)], writeIndex, brakeLag) * fade;
        }
        writeIndex = (writeIndex + 1) % maxDelay;
    }
}
}
