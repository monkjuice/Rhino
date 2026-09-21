#pragma once

#include "ForgeFx.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>
#include <vector>

// The rack, rendered.
//
// One rule governs this whole file: **nothing here allocates once audio is
// running**. A slot's type changes while notes are sounding, so every slot
// carries the state of every type it could hold, sized at `prepare` for the
// longest line any of them needs. That is a bounded allocation across all slots
// and it buys the thing that matters — changing a slot from a filter to a
// reverb mid-note cannot touch the heap.
//
// The rack runs on the summed output of the voices rather than inside them, the
// way an insert after the synth would. Serum says the same of its own, and it is
// what makes a reverb on a bus one tail fed by every note rather than a copy per
// note.
namespace rhino::forge
{
// --- Small pieces the effects share -------------------------------------------

// A fractional-delay line. Reading between two samples is what lets a chorus
// sweep and a delay be set to a time that is not a whole number of samples.
class DelayLine
{
public:
    void prepare(int maximumSamples)
    {
        buffer.assign(static_cast<size_t>(juce::jmax(4, maximumSamples)), 0.0f);
        write = 0;
    }

    void reset() { std::fill(buffer.begin(), buffer.end(), 0.0f); write = 0; }

    void push(float sample)
    {
        buffer[static_cast<size_t>(write)] = sample;
        if (++write >= static_cast<int>(buffer.size())) write = 0;
    }

    float read(float delaySamples) const
    {
        const auto size = static_cast<int>(buffer.size());
        if (size <= 0) return 0.0f;
        const auto wanted = juce::jlimit(1.0f, static_cast<float>(size - 2), delaySamples);
        const auto whole = static_cast<int>(wanted);
        const auto fraction = wanted - static_cast<float>(whole);
        auto index = write - whole;
        while (index < 0) index += size;
        auto next = index - 1;
        if (next < 0) next += size;
        return buffer[static_cast<size_t>(index)] * (1.0f - fraction)
             + buffer[static_cast<size_t>(next)] * fraction;
    }

    bool ready() const { return buffer.size() > 4; }

private:
    std::vector<float> buffer;
    int write = 0;
};

// A one-pole low pass, for damping a reverb tail and colouring a delay's
// repeats. Cheap, and the only thing either of those needs.
struct OnePole
{
    float state = 0.0f;

    float lowPass(float input, float coefficient)
    {
        state += coefficient * (input - state);
        return state;
    }

    float highPass(float input, float coefficient)
    {
        return input - lowPass(input, coefficient);
    }

    void reset() { state = 0.0f; }
};

struct AllpassStage
{
    float previousInput = 0.0f, previousOutput = 0.0f;

    float process(float input, float coefficient)
    {
        const auto output = -coefficient * input + previousInput + coefficient * previousOutput;
        previousInput = input;
        previousOutput = output;
        return output;
    }

    void reset() { previousInput = previousOutput = 0.0f; }
};

// How far a one-pole has to travel per sample to sit at a given corner.
inline float onePoleCoefficient(float hz, double sampleRate)
{
    const auto rate = static_cast<float>(juce::jmax(1.0, sampleRate));
    return juce::jlimit(0.0005f, 1.0f,
                        1.0f - std::exp(-juce::MathConstants<float>::twoPi * juce::jmax(1.0f, hz) / rate));
}

// A transposed direct-form-II biquad, which is what the equaliser's two bands
// are. The coefficients come from the standard cookbook shapes.
struct Biquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

    float process(float input)
    {
        const auto output = b0 * input + z1;
        z1 = b1 * input - a1 * output + z2;
        z2 = b2 * input - a2 * output;
        return output;
    }

    void reset() { z1 = z2 = 0.0f; }

    void set(float newB0, float newB1, float newB2, float a0, float newA1, float newA2)
    {
        const auto scale = 1.0f / (std::abs(a0) < 1.0e-9f ? 1.0e-9f : a0);
        b0 = newB0 * scale; b1 = newB1 * scale; b2 = newB2 * scale;
        a1 = newA1 * scale; a2 = newA2 * scale;
    }
};

// The three shapes each equaliser band can take. Gain is in decibels and is
// ignored by the pass shapes, which is what the panel greys the knob for.
enum class BandShape { lowShelf, peak, highPass, highShelf, lowPass };

inline void setBand(Biquad& filter, BandShape shape, float hz, float q, float gainDb, double sampleRate)
{
    const auto rate = juce::jmax(1000.0, sampleRate);
    const auto omega = juce::MathConstants<float>::twoPi
                     * juce::jlimit(20.0f, static_cast<float>(rate * 0.45), hz) / static_cast<float>(rate);
    const auto sin = std::sin(omega), cos = std::cos(omega);
    const auto quality = juce::jmax(0.05f, q);
    const auto alpha = sin / (2.0f * quality);
    const auto a = std::pow(10.0f, gainDb / 40.0f);

    switch (shape)
    {
        case BandShape::peak:
            filter.set(1.0f + alpha * a, -2.0f * cos, 1.0f - alpha * a,
                       1.0f + alpha / a, -2.0f * cos, 1.0f - alpha / a);
            return;
        case BandShape::lowShelf:
        {
            const auto beta = 2.0f * std::sqrt(a) * alpha;
            filter.set(a * ((a + 1.0f) - (a - 1.0f) * cos + beta),
                       2.0f * a * ((a - 1.0f) - (a + 1.0f) * cos),
                       a * ((a + 1.0f) - (a - 1.0f) * cos - beta),
                       (a + 1.0f) + (a - 1.0f) * cos + beta,
                       -2.0f * ((a - 1.0f) + (a + 1.0f) * cos),
                       (a + 1.0f) + (a - 1.0f) * cos - beta);
            return;
        }
        case BandShape::highShelf:
        {
            const auto beta = 2.0f * std::sqrt(a) * alpha;
            filter.set(a * ((a + 1.0f) + (a - 1.0f) * cos + beta),
                       -2.0f * a * ((a - 1.0f) + (a + 1.0f) * cos),
                       a * ((a + 1.0f) + (a - 1.0f) * cos - beta),
                       (a + 1.0f) - (a - 1.0f) * cos + beta,
                       2.0f * ((a - 1.0f) - (a + 1.0f) * cos),
                       (a + 1.0f) - (a - 1.0f) * cos - beta);
            return;
        }
        case BandShape::highPass:
            filter.set((1.0f + cos) * 0.5f, -(1.0f + cos), (1.0f + cos) * 0.5f,
                       1.0f + alpha, -2.0f * cos, 1.0f - alpha);
            return;
        case BandShape::lowPass:
            filter.set((1.0f - cos) * 0.5f, 1.0f - cos, (1.0f - cos) * 0.5f,
                       1.0f + alpha, -2.0f * cos, 1.0f - alpha);
            return;
    }
}

// --- Distortion ---------------------------------------------------------------

// The eight shapes, each taking a driven sample to a distorted one. Written to
// stay inside full scale at any drive, so a shape cannot be the thing that makes
// the rack clip.
inline float fxShape(int shape, float x, float drive)
{
    const auto gain = 1.0f + drive * 24.0f;
    const auto driven = x * gain;
    switch (shape)
    {
        // Soft, asymmetric, and gentler on the negative half — the even
        // harmonics that asymmetry brings are what "tube" means here.
        case 0: return std::tanh(driven * (driven > 0.0f ? 1.0f : 0.7f)) * 0.9f;
        case 1: return std::tanh(driven);
        case 2: return juce::jlimit(-1.0f, 1.0f, driven);
        // A diode's knee: soft until it conducts, hard after.
        case 3: return driven / (1.0f + std::abs(driven));
        // Folded back on itself at the rails rather than clipped, so the
        // harmonics keep arriving as the drive goes up instead of settling.
        case 4:
        {
            auto folded = std::fmod(std::abs(driven) + 1.0f, 4.0f);
            folded = std::abs(folded - 2.0f) - 1.0f;
            return driven < 0.0f ? -folded : folded;
        }
        case 5: return std::sin(juce::jlimit(-8.0f, 8.0f, driven) * juce::MathConstants<float>::halfPi);
        // Quantised to fewer and fewer steps as the drive goes up.
        case 6:
        {
            const auto steps = juce::jmax(2.0f, 64.0f * std::pow(1.0f - juce::jlimit(0.0f, 0.99f, drive), 3.0f));
            return juce::jlimit(-1.0f, 1.0f, std::round(x * steps) / steps);
        }
        default: break;
    }
    return juce::jlimit(-1.0f, 1.0f, driven);
}

// --- One slot's state ---------------------------------------------------------

// Every type's state, in one place. A slot holds all of it because its type can
// change between one sample and the next, and the alternative — building the
// state a type needs when it is chosen — is an allocation on the audio thread.
struct FxSlotState
{
    // Delay and chorus.
    std::array<DelayLine, 2> lines;
    std::array<OnePole, 2> loopFilter;
    std::array<float, 2> feedbackHeld {};
    float lfoPhase = 0.0f;

    // Reverb: a bank of combs into a chain of allpasses, per channel.
    static constexpr int combCount = 8;
    static constexpr int allpassCount = 4;
    std::array<std::array<DelayLine, combCount>, 2> combs;
    std::array<std::array<OnePole, combCount>, 2> combDamp;
    std::array<std::array<DelayLine, allpassCount>, 2> allpasses;
    std::array<DelayLine, 2> preDelay;
    std::array<OnePole, 2> reverbLoCut, reverbHiCut;

    // Equaliser and the distortion's own filter.
    std::array<Biquad, 2> low, high;
    std::array<float, 2> svfLow {}, svfBand {};
    std::array<std::array<float, 2>, 2> filterLow {}, filterBand {};
    std::array<OnePole, 2> filterPole;
    // Downsampling holds a sample for several of them.
    std::array<float, 2> held {};
    float holdPhase = 0.0f;

    // Compressor and phaser. Both are deliberately fixed-size; changing a
    // type or stage count while audio runs never allocates.
    float compressorEnvelope = 0.0f, compressorGain = 1.0f;
    static constexpr int phaserStageCount = 12;
    std::array<std::array<AllpassStage, phaserStageCount>, 2> phaser;
    std::array<float, 2> phaserFeedback {};
    float phaserPhase = 0.0f;

    void reset()
    {
        for (auto& line : lines) line.reset();
        for (auto& filter : loopFilter) filter.reset();
        feedbackHeld = {};
        lfoPhase = 0.0f;
        for (auto& channel : combs) for (auto& comb : channel) comb.reset();
        for (auto& channel : combDamp) for (auto& damp : channel) damp.reset();
        for (auto& channel : allpasses) for (auto& allpass : channel) allpass.reset();
        for (auto& line : preDelay) line.reset();
        for (auto& filter : reverbLoCut) filter.reset();
        for (auto& filter : reverbHiCut) filter.reset();
        for (auto& filter : low) filter.reset();
        for (auto& filter : high) filter.reset();
        svfLow = {};
        svfBand = {};
        filterLow = {};
        filterBand = {};
        for (auto& filter : filterPole) filter.reset();
        held = {};
        holdPhase = 0.0f;
        compressorEnvelope = 0.0f;
        compressorGain = 1.0f;
        for (auto& channel : phaser) for (auto& stage : channel) stage.reset();
        phaserFeedback = {};
        phaserPhase = 0.0f;
    }
};

// The comb and allpass lengths a Schroeder reverb is built from, in samples at
// 44.1 kHz. They are mutually prime so the echoes never line up into a pitch,
// and they are scaled to the running sample rate at `prepare`.
inline const std::array<int, FxSlotState::combCount>& reverbCombLengths()
{
    static const std::array<int, FxSlotState::combCount> lengths {
        1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
    return lengths;
}

inline const std::array<int, FxSlotState::allpassCount>& reverbAllpassLengths()
{
    static const std::array<int, FxSlotState::allpassCount> lengths {556, 441, 341, 225};
    return lengths;
}

// How far the right channel's lines are offset from the left, in samples at
// 44.1 kHz. Without it both channels would be the same reverb and the tail
// would be mono however wide it was spread.
inline constexpr int reverbStereoSpread = 23;

// The whole rack, rendered. One of these per rack.
class FxRack
{
public:
    void prepare(double newSampleRate)
    {
        sampleRate = juce::jmax(1000.0, newSampleRate);
        const auto scale = sampleRate / 44100.0;
        const auto scaled = [scale] (int length, int offset)
        {
            return juce::jmax(4, static_cast<int>((length + offset) * scale) + 4);
        };

        for (auto& slot : states)
        {
            for (int channel = 0; channel < 2; ++channel)
            {
                const auto index = static_cast<size_t>(channel);
                const auto offset = channel == 0 ? 0 : reverbStereoSpread;
                slot.lines[index].prepare(static_cast<int>(sampleRate * fxMaxDelaySeconds) + 8);
                slot.preDelay[index].prepare(static_cast<int>(sampleRate * 0.25) + 8);
                for (int comb = 0; comb < FxSlotState::combCount; ++comb)
                    slot.combs[index][static_cast<size_t>(comb)]
                        .prepare(scaled(reverbCombLengths()[static_cast<size_t>(comb)], offset));
                for (int allpass = 0; allpass < FxSlotState::allpassCount; ++allpass)
                    slot.allpasses[index][static_cast<size_t>(allpass)]
                        .prepare(scaled(reverbAllpassLengths()[static_cast<size_t>(allpass)], offset));
            }
            slot.reset();
        }
    }

    void reset() { for (auto& slot : states) slot.reset(); }

    // The rack's whole effect on one sample. Slots run in the order they are
    // declared, top to bottom, which is the order they are drawn in.
    void process(const Rack& rack, double bpm, float& left, float& right)
    {
        if (fxOn(rack.bypass)) return;
        for (int index = 0; index < fxSlotCount; ++index)
        {
            const auto& slot = rack.slots[static_cast<size_t>(index)];
            const auto type = fxTypeOf(slot.type);
            if (type == FxType::off || fxOn(slot.bypass)) continue;

            // Every type is rendered wet and then blended against what came in,
            // so MIX means one thing across the whole rack and no type has to
            // implement it for itself.
            auto wetLeft = left, wetRight = right;
            render(states[static_cast<size_t>(index)], slot, type, bpm, wetLeft, wetRight);

            const auto mix = juce::jlimit(0.0f, 1.0f, slot.mix);
            const auto level = juce::jlimit(0.0f, 2.0f, slot.level);
            left = (wetLeft * mix + left * (1.0f - mix)) * level;
            right = (wetRight * mix + right * (1.0f - mix)) * level;
        }
    }

private:
    double sampleRate = 44100.0;
    std::array<FxSlotState, fxSlotCount> states;

    void render(FxSlotState& state, const FxSlot& slot, FxType type, double bpm,
                float& left, float& right)
    {
        switch (type)
        {
            case FxType::reverb:     renderReverb(state, slot, left, right); return;
            case FxType::delay:      renderDelay(state, slot, bpm, left, right); return;
            case FxType::chorus:     renderChorus(state, slot, bpm, left, right); return;
            case FxType::distortion: renderDistortion(state, slot, left, right); return;
            case FxType::equaliser:  renderEqualiser(state, slot, left, right); return;
            case FxType::filter:     renderFilter(state, slot, left, right); return;
            case FxType::compressor: renderCompressor(state, slot, left, right); return;
            case FxType::phaser:     renderPhaser(state, slot, bpm, left, right); return;
            case FxType::off:        break;
        }
    }

    // --- Reverb ---------------------------------------------------------------
    //
    // A Schroeder bank: eight parallel combs into four allpasses, per channel,
    // with the right channel's lines offset so the tail is genuinely stereo
    // rather than one reverb heard twice.
    void renderReverb(FxSlotState& state, const FxSlot& slot, float& left, float& right)
    {
        const auto size = fxScaled(slot.knobs[0], 0.35f, 1.0f);
        // A hall holds its energy longer than a plate at the same setting, which
        // is most of what separates the two here.
        const auto decay = fxReverbDecay(slot);
        const auto damp = onePoleCoefficient(fxHertz(1.0f - slot.knobs[2], 800.0f, 16000.0f), sampleRate);
        const auto width = juce::jlimit(0.0f, 1.0f, slot.knobs[3]);
        const auto preDelay = fxScaled(slot.knobs[1], 0.0f, 0.2f) * static_cast<float>(sampleRate);
        const auto loCut = onePoleCoefficient(fxHertz(slot.knobs[4], 20.0f, 1200.0f), sampleRate);
        const auto hiCut = onePoleCoefficient(fxHertz(slot.knobs[5], 1200.0f, 20000.0f), sampleRate);

        std::array<float, 2> input {left, right}, output {};
        for (int channel = 0; channel < 2; ++channel)
        {
            const auto index = static_cast<size_t>(channel);
            auto fed = input[index] * 0.25f;
            if (preDelay > 1.0f)
            {
                state.preDelay[index].push(fed);
                fed = state.preDelay[index].read(preDelay);
            }

            auto combined = 0.0f;
            for (int comb = 0; comb < FxSlotState::combCount; ++comb)
            {
                auto& line = state.combs[index][static_cast<size_t>(comb)];
                if (!line.ready()) continue;
                const auto length = static_cast<float>(
                    reverbCombLengths()[static_cast<size_t>(comb)] + (channel == 0 ? 0 : reverbStereoSpread))
                    * static_cast<float>(sampleRate / 44100.0) * size;
                const auto delayed = line.read(length);
                combined += delayed;
                const auto damped = state.combDamp[index][static_cast<size_t>(comb)].lowPass(delayed, damp);
                line.push(fed + damped * decay);
            }
            combined /= static_cast<float>(FxSlotState::combCount);

            for (int allpass = 0; allpass < FxSlotState::allpassCount; ++allpass)
            {
                auto& line = state.allpasses[index][static_cast<size_t>(allpass)];
                if (!line.ready()) continue;
                const auto length = static_cast<float>(
                    reverbAllpassLengths()[static_cast<size_t>(allpass)] + (channel == 0 ? 0 : reverbStereoSpread))
                    * static_cast<float>(sampleRate / 44100.0);
                const auto delayed = line.read(length);
                const auto fedBack = combined + delayed * 0.5f;
                line.push(fedBack);
                combined = delayed - fedBack * 0.5f;
            }

            const auto withoutLow = state.reverbLoCut[index].highPass(combined, loCut);
            output[index] = state.reverbHiCut[index].lowPass(withoutLow, hiCut);
        }

        // Width spreads the pair apart through their own mid and side rather
        // than by panning, so at nothing the tail is mono and at full it is as
        // wide as the bank makes it.
        const auto mid = (output[0] + output[1]) * 0.5f;
        const auto side = (output[0] - output[1]) * 0.5f * width;
        left = mid + side;
        right = mid - side;
    }

    // --- Delay ----------------------------------------------------------------
    void renderDelay(FxSlotState& state, const FxSlot& slot, double bpm, float& left, float& right)
    {
        const auto& info = fxTypes()[static_cast<size_t>(FxType::delay)];
        const auto pingPong = fxModeOf(info.modeA, slot.modeA) == 1;

        const auto seconds = fxDelaySeconds(slot, bpm);
        const auto leftSamples = seconds * static_cast<float>(sampleRate);
        const auto rightSamples = leftSamples * fxOffsetRatio(slot.knobs[1]);
        const auto feedback = juce::jlimit(0.0f, 0.95f, slot.knobs[2]);
        const auto colour = onePoleCoefficient(fxHertz(slot.knobs[3], 200.0f, 16000.0f), sampleRate);
        // Serum's Q reads backwards from the usual: at the top the filter is
        // widest and takes out least. Kept that way round, because that is what
        // the knob is named after.
        const auto width = juce::jlimit(0.0f, 1.0f, slot.knobs[4]);

        const auto delayed = std::array<float, 2> {
            state.lines[0].read(leftSamples), state.lines[1].read(rightSamples)};

        std::array<float, 2> tail {};
        for (int channel = 0; channel < 2; ++channel)
        {
            const auto index = static_cast<size_t>(channel);
            const auto low = state.loopFilter[index].lowPass(delayed[index], colour);
            tail[index] = juce::jmap(width, low, delayed[index]);
        }

        // Ping-pong crosses the repeats, so each one arrives on the other side
        // and the echo walks across the image.
        state.lines[0].push(left + (pingPong ? tail[1] : tail[0]) * feedback);
        state.lines[1].push(right + (pingPong ? tail[0] : tail[1]) * feedback);

        left = tail[0];
        right = tail[1];
    }

    // --- Chorus ---------------------------------------------------------------
    //
    // Four taps: two per side, at two delay times, swept by one LFO in
    // quadrature so the pair on each side never move together.
    void renderChorus(FxSlotState& state, const FxSlot& slot, double bpm, float& left, float& right)
    {
        const auto& info = fxTypes()[static_cast<size_t>(FxType::chorus)];
        const auto highPass = fxModeOf(info.modeB, slot.modeB) == 1;

        const auto rate = fxChorusRate(slot, bpm);
        const auto first = fxScaled(slot.knobs[1], 0.5f, 30.0f) * 0.001f * static_cast<float>(sampleRate);
        const auto second = fxScaled(slot.knobs[2], 0.5f, 30.0f) * 0.001f * static_cast<float>(sampleRate);
        const auto depth = fxScaled(slot.knobs[3], 0.0f, 6.0f) * 0.001f * static_cast<float>(sampleRate);
        const auto feedback = juce::jlimit(0.0f, 0.85f, slot.knobs[4] * 0.85f);
        const auto corner = onePoleCoefficient(fxHertz(slot.knobs[5], 120.0f, 16000.0f), sampleRate);

        const auto phase = state.lfoPhase * juce::MathConstants<float>::twoPi;
        const auto sweepA = std::sin(phase), sweepB = std::cos(phase);

        state.lines[0].push(left + state.feedbackHeld[0] * feedback);
        state.lines[1].push(right + state.feedbackHeld[1] * feedback);

        // The two sides read the same taps in opposite phase, which is what
        // makes four voices out of two delay lines and puts them either side.
        const auto wetLeft = state.lines[0].read(first + depth * (1.0f + sweepA))
                           + state.lines[0].read(second + depth * (1.0f + sweepB));
        const auto wetRight = state.lines[1].read(first + depth * (1.0f - sweepA))
                            + state.lines[1].read(second + depth * (1.0f - sweepB));

        state.feedbackHeld[0] = wetLeft * 0.5f;
        state.feedbackHeld[1] = wetRight * 0.5f;

        left = highPass ? state.loopFilter[0].highPass(wetLeft * 0.5f, corner)
                        : state.loopFilter[0].lowPass(wetLeft * 0.5f, corner);
        right = highPass ? state.loopFilter[1].highPass(wetRight * 0.5f, corner)
                         : state.loopFilter[1].lowPass(wetRight * 0.5f, corner);

        state.lfoPhase += rate / static_cast<float>(sampleRate);
        while (state.lfoPhase >= 1.0f) state.lfoPhase -= 1.0f;
    }

    // --- Distortion -----------------------------------------------------------
    void renderDistortion(FxSlotState& state, const FxSlot& slot, float& left, float& right)
    {
        const auto& info = fxTypes()[static_cast<size_t>(FxType::distortion)];
        const auto shape = fxModeOf(info.modeA, slot.modeA);
        const auto placement = fxDistortionFilterPlacement(slot);
        const auto highPass = fxDistortionFilterHighPass(slot);

        const auto drive = juce::jlimit(0.0f, 1.0f, slot.knobs[0]);
        const auto hz = fxHertz(slot.knobs[1], 40.0f, 16000.0f);
        const auto q = fxScaled(slot.knobs[2], 0.4f, 8.0f);

        // Downsampling is the one shape that is a rate rather than a curve, so
        // it holds a sample across several instead of remapping each one.
        const auto downsample = shape == 7;
        if (downsample)
        {
            const auto factor = fxScaled(drive, 1.0f, 48.0f);
            state.holdPhase += 1.0f;
            if (state.holdPhase >= factor)
            {
                state.holdPhase -= factor;
                state.held[0] = left;
                state.held[1] = right;
            }
        }

        std::array<float, 2> sample {left, right};
        for (int channel = 0; channel < 2; ++channel)
        {
            const auto index = static_cast<size_t>(channel);
            if (placement == 1)
                sample[index] = distortionFilter(state, channel, sample[index], hz, q, highPass);
            sample[index] = downsample ? state.held[index] : fxShape(shape, sample[index], drive);
            if (placement == 2)
                sample[index] = distortionFilter(state, channel, sample[index], hz, q, highPass);
        }
        left = sample[0];
        right = sample[1];
    }

    // The distortion's own filter: Core's state-variable topology rather than a
    // biquad, so a filter placed in front of a distortion sounds like the one
    // the synth already has.
    float distortionFilter(FxSlotState& state, int channel, float input, float hz, float q,
                           bool highPass)
    {
        const auto index = static_cast<size_t>(channel);
        const auto g = std::tan(juce::MathConstants<float>::pi
                                * juce::jlimit(20.0f, static_cast<float>(sampleRate * 0.45), hz)
                                / static_cast<float>(sampleRate));
        const auto damping = 1.0f / juce::jmax(0.05f, q);
        const auto denominator = 1.0f + g * (g + damping);
        const auto high = (input - (damping + g) * state.svfBand[index] - state.svfLow[index]) / denominator;
        const auto band = g * high + state.svfBand[index];
        const auto low = g * band + state.svfLow[index];
        state.svfBand[index] = g * high + band;
        state.svfLow[index] = g * band + low;
        return highPass ? high : low;
    }

    // --- Equaliser ------------------------------------------------------------
    void renderEqualiser(FxSlotState& state, const FxSlot& slot, float& left, float& right)
    {
        const auto& info = fxTypes()[static_cast<size_t>(FxType::equaliser)];
        const auto lowShape = fxModeOf(info.modeA, slot.modeA) == 0 ? BandShape::lowShelf
                            : fxModeOf(info.modeA, slot.modeA) == 1 ? BandShape::peak
                                                                    : BandShape::highPass;
        const auto highShape = fxModeOf(info.modeB, slot.modeB) == 0 ? BandShape::highShelf
                             : fxModeOf(info.modeB, slot.modeB) == 1 ? BandShape::peak
                                                                     : BandShape::lowPass;

        const auto lowHz = fxHertz(slot.knobs[0], 20.0f, 2000.0f);
        const auto lowQ = fxScaled(slot.knobs[1], 0.2f, 6.0f);
        const auto lowGain = fxScaled(slot.knobs[2], -18.0f, 18.0f);
        const auto highHz = fxHertz(slot.knobs[3], 500.0f, 18000.0f);
        const auto highQ = fxScaled(slot.knobs[4], 0.2f, 6.0f);
        const auto highGain = fxScaled(slot.knobs[5], -18.0f, 18.0f);

        for (int channel = 0; channel < 2; ++channel)
        {
            const auto index = static_cast<size_t>(channel);
            setBand(state.low[index], lowShape, lowHz, lowQ, lowGain, sampleRate);
            setBand(state.high[index], highShape, highHz, highQ, highGain, sampleRate);
        }
        left = state.high[0].process(state.low[0].process(left));
        right = state.high[1].process(state.low[1].process(right));
    }

    // --- Filter ---------------------------------------------------------------
    void renderFilter(FxSlotState& state, const FxSlot& slot, float& left, float& right)
    {
        const auto& info = fxTypes()[static_cast<size_t>(FxType::filter)];
        const auto type = fxModeOf(info.modeA, slot.modeA);
        const auto hz = fxHertz(slot.knobs[0], 30.0f, 18000.0f);
        const auto resonance = juce::jlimit(0.0f, 0.98f, slot.knobs[1]);
        const auto drive = juce::jlimit(0.0f, 1.0f, slot.knobs[2]);
        const auto fat = juce::jlimit(0.0f, 1.0f, slot.knobs[3]);
        const auto pan = juce::jlimit(-1.0f, 1.0f, slot.knobs[4] * 2.0f - 1.0f);

        const auto g = std::tan(juce::MathConstants<float>::pi
                                * juce::jmin(hz, static_cast<float>(sampleRate * 0.45))
                                / static_cast<float>(sampleRate));
        const auto damping = juce::jmax(0.05f, 2.0f - resonance * 1.96f);

        std::array<float, 2> sample {left, right};
        for (int channel = 0; channel < 2; ++channel)
        {
            const auto index = static_cast<size_t>(channel);
            const auto driven = drive > 0.0f
                ? std::tanh(sample[index] * (1.0f + drive * 12.0f)) / std::tanh(1.0f + drive * 12.0f)
                : sample[index];
            const auto poleCoefficient = onePoleCoefficient(hz, sampleRate);
            if (type == 1 || type == 5)
            {
                const auto low = state.filterPole[index].lowPass(driven, poleCoefficient);
                sample[index] = type == 1 ? low : driven - low;
            }
            else
            {
                const auto runStage = [&] (float input, int stage)
                {
                    auto& lowState = state.filterLow[index][static_cast<size_t>(stage)];
                    auto& bandState = state.filterBand[index][static_cast<size_t>(stage)];
                    const auto denominator = 1.0f + g * (g + damping);
                    const auto high = (input - (damping + g) * bandState - lowState) / denominator;
                    const auto band = g * high + bandState;
                    const auto low = g * band + lowState;
                    bandState = g * high + band;
                    lowState = g * band + low;
                    return std::array<float, 3> {low, band, high};
                };
                const auto first = runStage(driven, 0);
                if (type == 0) sample[index] = first[0];
                else if (type == 2) sample[index] = runStage(first[0], 1)[0];
                else if (type == 4) sample[index] = first[2];
                else if (type == 3) sample[index] = first[0] + first[2];
                else if (type == 6)
                    sample[index] = juce::jlimit(-1.0f, 1.0f, driven + first[1] * (0.5f + fat * 1.5f));
                else sample[index] = first[1];
            }
            // FAT is a parallel warm path rather than another drive control:
            // it keeps body under a resonant or steep shape.
            if (type != 6)
                sample[index] = juce::jmap(fat * 0.35f, sample[index], std::tanh(driven));
        }
        left = sample[0] * (pan > 0.0f ? 1.0f - pan : 1.0f);
        right = sample[1] * (pan < 0.0f ? 1.0f + pan : 1.0f);
    }

    // --- Compressor -----------------------------------------------------------
    void renderCompressor(FxSlotState& state, const FxSlot& slot, float& left, float& right)
    {
        const auto& info = fxTypes()[static_cast<size_t>(FxType::compressor)];
        const auto rms = fxModeOf(info.modeA, slot.modeA) == 1;
        const auto automatic = fxModeOf(info.modeB, slot.modeB) == 1;
        const auto threshold = fxCompressorThreshold(slot.knobs[0]);
        const auto ratio = fxCompressorRatio(slot.knobs[1]);
        const auto attack = fxCompressorAttack(slot.knobs[2]);
        const auto release = fxCompressorRelease(slot.knobs[3]);
        const auto knee = fxCompressorKnee(slot.knobs[5]);

        const auto peak = juce::jmax(std::abs(left), std::abs(right));
        const auto detected = rms ? peak * peak : peak;
        const auto attackCoefficient = std::exp(-1.0f / static_cast<float>(sampleRate * attack));
        const auto releaseCoefficient = std::exp(-1.0f / static_cast<float>(sampleRate * release));
        const auto coefficient = detected > state.compressorEnvelope ? attackCoefficient : releaseCoefficient;
        state.compressorEnvelope = coefficient * state.compressorEnvelope + (1.0f - coefficient) * detected;
        const auto amplitude = rms ? std::sqrt(juce::jmax(0.0f, state.compressorEnvelope))
                                   : state.compressorEnvelope;
        const auto inputDb = juce::Decibels::gainToDecibels(amplitude, -100.0f);
        const auto outputDb = fxCompressorOutputDb(inputDb, threshold, ratio, knee);
        const auto reduction = outputDb - inputDb;
        const auto makeup = automatic ? juce::jlimit(0.0f, 18.0f, -threshold * (1.0f - 1.0f / ratio) * 0.5f)
                                      : fxCompressorMakeup(slot.knobs[4]);
        const auto targetGain = juce::Decibels::decibelsToGain(reduction + makeup);
        state.compressorGain += (targetGain - state.compressorGain) * (targetGain < state.compressorGain ? 0.2f : 0.02f);
        left = juce::jlimit(-1.0f, 1.0f, left * state.compressorGain);
        right = juce::jlimit(-1.0f, 1.0f, right * state.compressorGain);
    }

    // --- Phaser ---------------------------------------------------------------
    void renderPhaser(FxSlotState& state, const FxSlot& slot, double bpm, float& left, float& right)
    {
        const auto rate = fxPhaserRate(slot, bpm);
        const auto depth = fxScaled(slot.knobs[1], 0.0f, 4.0f);
        const auto centre = fxHertz(slot.knobs[2], 80.0f, 8000.0f);
        const auto feedback = fxScaled(slot.knobs[3], -0.85f, 0.85f);
        const auto spread = fxScaled(slot.knobs[4], -0.5f, 0.5f);
        const auto width = juce::jlimit(0.0f, 1.0f, slot.knobs[5]);
        const auto stages = fxPhaserStages(slot);
        const auto phase = state.phaserPhase * juce::MathConstants<float>::twoPi;

        const std::array<float, 2> input {left, right};
        std::array<float, 2> wet {};
        for (int channel = 0; channel < 2; ++channel)
        {
            const auto index = static_cast<size_t>(channel);
            const auto channelPhase = phase + (channel == 0 ? -spread : spread) * juce::MathConstants<float>::pi;
            const auto sweep = std::sin(channelPhase);
            const auto hz = juce::jlimit(20.0f, static_cast<float>(sampleRate * 0.45),
                                         centre * std::pow(2.0f, depth * sweep));
            const auto tangent = std::tan(juce::MathConstants<float>::pi * hz / static_cast<float>(sampleRate));
            const auto coefficient = (1.0f - tangent) / (1.0f + tangent);
            auto sample = input[index] + state.phaserFeedback[index] * feedback;
            for (int stage = 0; stage < stages; ++stage)
                sample = state.phaser[index][static_cast<size_t>(stage)].process(sample, coefficient);
            state.phaserFeedback[index] = sample;
            wet[index] = (input[index] + sample) * 0.5f;
        }
        const auto mono = (wet[0] + wet[1]) * 0.5f;
        left = juce::jmap(width, mono, wet[0]);
        right = juce::jmap(width, mono, wet[1]);
        state.phaserPhase += rate / static_cast<float>(sampleRate);
        while (state.phaserPhase >= 1.0f) state.phaserPhase -= 1.0f;
    }
};
}
