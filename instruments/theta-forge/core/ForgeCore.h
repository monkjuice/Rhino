#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// The reusable sound engine. It deliberately owns no AudioProcessor, UI,
// Tracktion, state tree, filesystem, or allocation in renderSample().
//
// Forge is a synthesiser only. Effects live outside this file and, until the
// FX rack milestone, do not exist at all. See PLAN.md.
namespace theta::forge
{
// Everything one oscillator owns. Both oscillators are the same shape: neither
// is defined in terms of the other, so switching one off or changing its level
// cannot move the other.
struct Oscillator
{
    float enable = 1.0f;
    float position = 0.55f;
    float octave = 0.0f, semitone = 0.0f, fine = 0.0f;
    float unison = 2.0f, detune = 0.18f, blend = 0.5f;
    float pan = 0.0f, level = 0.75f;
};

// Which of the filter's three taps reaches the output.
enum class FilterType { lowPass, highPass, bandPass };

struct Patch
{
    Oscillator a, b;
    float subEnable = 1.0f, subLevel = 0.12f;
    float noiseEnable = 0.0f, noiseLevel = 0.25f;
    float filterEnable = 1.0f, filterType = 0.0f;
    // Each source either passes through the filter or bypasses it straight to
    // the voice sum, exactly as Serum's per-source routing buttons work.
    float routeA = 1.0f, routeB = 1.0f, routeSub = 1.0f, routeNoise = 1.0f;
    float cutoff = 7800.0f, resonance = 0.12f, drive = 0.08f;
    float attack = 0.01f, decay = 0.24f, sustain = 0.75f, release = 0.35f;
    float lfoRate = 0.5f, lfoCutoff = 0.0f, lfoPosition = 0.0f, lfoPitch = 0.0f;
    float polyphony = 8.0f, mono = 0.0f, legato = 1.0f, glide = 0.08f;
    float output = 0.75f;
};

inline bool on(float enable) { return enable >= 0.5f; }

inline FilterType filterTypeOf(const Patch& patch)
{
    return static_cast<FilterType>(juce::jlimit(0, 2, juce::roundToInt(patch.filterType)));
}

// Drive at zero is genuinely clean: the saturation is skipped rather than run
// at unity, which would still compress the peaks.
inline float saturate(float x, float drive)
{
    const auto amount = juce::jlimit(0.0f, 1.0f, drive);
    if (amount <= 0.0f) return x;
    const auto gain = 1.0f + amount * 12.0f;
    return std::tanh(x * gain) / std::tanh(gain);
}

// Exactly linear below the knee, asymptotic to full scale above it. A quiet
// patch passes through untouched — which a plain tanh does not do — while a
// loud one still cannot leave full scale.
inline float softClip(float x)
{
    constexpr float knee = 0.8f;
    const auto magnitude = std::abs(x);
    if (magnitude <= knee) return x;
    const auto limited = knee + (1.0f - knee) * std::tanh((magnitude - knee) / (1.0f - knee));
    return x < 0.0f ? -limited : limited;
}

// Octave, semitone and fine are one frequency multiplier. Fine is in cents.
inline float tuningRatio(const Oscillator& osc)
{
    return std::pow(2.0f, osc.octave + osc.semitone / 12.0f + osc.fine / 1200.0f);
}

class Core final
{
public:
    void initialise(double newSampleRate)
    {
        sampleRate = std::max(1.0, newSampleRate);
        reset();
    }

    void reset()
    {
        voices = {};
        nextVoice = 0;
        lfoPhase = 0.0f;
        noiseState = 0x9e3779b9u;
        heldCount = 0;
        monoMode = false;
        meterEnvelope = 0.0f;
        meterStage = EnvelopeStage::idle;
    }

    void noteOn(int note, float velocity)
    {
        noteOn(note, velocity, Patch {});
    }

    void noteOn(int note, float velocity, const Patch& patch)
    {
        monoMode = patch.mono >= 0.5f;
        if (monoMode)
        {
            hold(note);
            auto& voice = voices[0];
            const auto continueEnvelope = voice.active && patch.legato >= 0.5f;
            if (!continueEnvelope)
                startVoice(voice, note, velocity);
            else
            {
                voice.note = note;
                voice.targetHz = noteFrequency(note);
                voice.velocity = juce::jlimit(0.0f, 1.0f, velocity);
            }
            if (patch.glide <= 0.0001f) voice.currentHz = voice.targetHz;
            return;
        }

        const auto voiceCount = static_cast<size_t>(juce::jlimit(1, static_cast<int>(voices.size()), juce::roundToInt(patch.polyphony)));
        auto& voice = voices[nextVoice++ % voiceCount];
        startVoice(voice, note, velocity);
    }

    void noteOff(int note)
    {
        if (monoMode)
        {
            releaseHeld(note);
            auto& voice = voices[0];
            if (voice.active && voice.note == note && heldCount > 0)
            {
                voice.note = heldNotes[static_cast<size_t>(heldCount - 1)];
                voice.targetHz = noteFrequency(voice.note);
                return;
            }
        }
        for (auto& voice : voices)
            if (voice.active && voice.note == note && voice.ampStage != EnvelopeStage::release)
            {
                voice.ampStage = EnvelopeStage::release;
                voice.ampReleaseStart = voice.ampEnvelope;
            }
    }

    void allNotesOff() { reset(); }

    // What ENV 1 is doing, for the display to draw. Taken from the loudest
    // sounding voice, which is the one a player is listening to. Plain members
    // rather than atomics: Core stays a pure DSP class and the processor owns
    // the hand-off to the message thread.
    float envelopeLevel() const { return meterEnvelope; }
    int envelopeStage() const { return static_cast<int>(meterStage); }

    void renderSample(const Patch& patch, float& left, float& right)
    {
        left = right = 0.0f;
        const auto lfo = std::sin(lfoPhase * juce::MathConstants<float>::twoPi);
        lfoPhase = wrap(lfoPhase + juce::jlimit(0.01f, 40.0f, patch.lfoRate) / static_cast<float>(sampleRate));

        // LFO 1 is the only thing that moves the cutoff, by design: Forge has a
        // single envelope and it is hardwired to amplitude. Everything else
        // reaches the cutoff once the modulation matrix exists.
        const auto lfoOctaves = juce::jlimit(-1.0f, 1.0f, patch.lfoCutoff) * lfo * 3.0f;
        const auto cutoff = patch.cutoff * std::pow(2.0f, lfoOctaves);

        meterEnvelope = 0.0f;
        meterStage = EnvelopeStage::idle;

        for (auto& voice : voices)
        {
            if (!voice.active) continue;
            updateEnvelope(voice.ampEnvelope, voice.ampStage, voice.ampReleaseStart,
                           patch.attack, patch.decay, patch.sustain, patch.release);
            if (voice.ampStage == EnvelopeStage::idle)
            {
                voice.active = false;
                continue;
            }
            if (voice.ampEnvelope >= meterEnvelope)
            {
                meterEnvelope = voice.ampEnvelope;
                meterStage = voice.ampStage;
            }

            Buses buses;
            renderOscillators(voice, patch, lfo, buses);

            // Drive belongs to the filter, so only what is routed into it is
            // driven, and switching the module off bypasses the drive with it.
            // The filter state keeps running either way, so switching the
            // module or a route back on does not click.
            auto routedLeft = buses.wetLeft, routedRight = buses.wetRight;
            const auto type = filterTypeOf(patch);
            if (on(patch.filterEnable))
            {
                routedLeft = filter(saturate(routedLeft, patch.drive),
                                    voice.lowLeft, voice.bandLeft, cutoff, patch.resonance, type);
                routedRight = filter(saturate(routedRight, patch.drive),
                                     voice.lowRight, voice.bandRight, cutoff, patch.resonance, type);
            }
            else
            {
                filter(routedLeft, voice.lowLeft, voice.bandLeft, cutoff, patch.resonance, type);
                filter(routedRight, voice.lowRight, voice.bandRight, cutoff, patch.resonance, type);
            }

            left += routedLeft + buses.dryLeft;
            right += routedRight + buses.dryRight;
        }

        const auto gain = juce::jlimit(0.0f, 1.25f, patch.output) * 0.28f;
        left = softClip(left * gain);
        right = softClip(right * gain);
    }

private:
    enum class EnvelopeStage { idle, attack, decay, sustain, release };

    // What a voice accumulates into: the sources routed through the filter,
    // and the sources that bypass it.
    struct Buses
    {
        float wetLeft = 0.0f, wetRight = 0.0f, dryLeft = 0.0f, dryRight = 0.0f;
    };

    struct Voice
    {
        bool active = false;
        int note = 0;
        float velocity = 0.0f;
        std::array<float, 8> phaseA {}, phaseB {};
        float phaseSub = 0.0f;
        float currentHz = 0.0f, targetHz = 0.0f;
        float ampEnvelope = 0.0f, ampReleaseStart = 0.0f;
        float lowLeft = 0.0f, bandLeft = 0.0f, lowRight = 0.0f, bandRight = 0.0f;
        EnvelopeStage ampStage = EnvelopeStage::idle;
    };

    static float wrap(float phase) { return phase - std::floor(phase); }
    static float noteFrequency(int note) { return static_cast<float>(juce::MidiMessage::getMidiNoteInHertz(note)); }

    void startVoice(Voice& voice, int note, float velocity)
    {
        voice = {};
        voice.active = true;
        voice.note = note;
        voice.velocity = juce::jlimit(0.0f, 1.0f, velocity);
        voice.currentHz = voice.targetHz = noteFrequency(note);
        voice.ampStage = EnvelopeStage::attack;
        for (size_t i = 0; i < voice.phaseA.size(); ++i)
        {
            const auto offset = static_cast<float>(i) / static_cast<float>(voice.phaseA.size());
            voice.phaseA[i] = std::fmod(offset * 0.37f + static_cast<float>(note) * 0.013f, 1.0f);
            voice.phaseB[i] = std::fmod(offset * 0.61f + static_cast<float>(note) * 0.019f, 1.0f);
        }
    }

    void hold(int note)
    {
        releaseHeld(note);
        if (heldCount < static_cast<int>(heldNotes.size())) heldNotes[static_cast<size_t>(heldCount++)] = note;
    }

    void releaseHeld(int note)
    {
        for (int i = 0; i < heldCount; ++i)
            if (heldNotes[static_cast<size_t>(i)] == note)
            {
                for (int j = i; j + 1 < heldCount; ++j) heldNotes[static_cast<size_t>(j)] = heldNotes[static_cast<size_t>(j + 1)];
                --heldCount;
                return;
            }
    }

    static float morph(float phase, float position)
    {
        phase = wrap(phase);
        const auto sine = std::sin(phase * juce::MathConstants<float>::twoPi);
        const auto triangle = 1.0f - 4.0f * std::abs(phase - 0.5f);
        const auto saw = phase * 2.0f - 1.0f;
        const auto square = phase < 0.5f ? 1.0f : -1.0f;
        const float frames[] {sine, triangle, saw, square};
        const auto scaled = juce::jlimit(0.0f, 1.0f, position) * 3.0f;
        const auto index = std::min(2, static_cast<int>(scaled));
        return juce::jmap(scaled - static_cast<float>(index), frames[index], frames[index + 1]);
    }

    void updateEnvelope(float& value, EnvelopeStage& stage, float releaseStart,
                        float attack, float decay, float sustain, float release) const
    {
        const auto dt = static_cast<float>(1.0 / sampleRate);
        switch (stage)
        {
            case EnvelopeStage::attack:
                value += dt / std::max(0.001f, attack);
                if (value >= 1.0f) { value = 1.0f; stage = EnvelopeStage::decay; }
                break;
            case EnvelopeStage::decay:
                value -= (1.0f - juce::jlimit(0.0f, 1.0f, sustain)) * dt / std::max(0.001f, decay);
                if (value <= sustain) { value = sustain; stage = EnvelopeStage::sustain; }
                break;
            case EnvelopeStage::sustain: value = sustain; break;
            case EnvelopeStage::release:
                value -= releaseStart * dt / std::max(0.001f, release);
                if (value <= 0.0001f) { value = 0.0f; stage = EnvelopeStage::idle; }
                break;
            case EnvelopeStage::idle: value = 0.0f; break;
        }
    }

    float noise()
    {
        noiseState ^= noiseState << 13;
        noiseState ^= noiseState >> 17;
        noiseState ^= noiseState << 5;
        return static_cast<float>(noiseState & 0xffffu) / 32767.5f - 1.0f;
    }

    // One oscillator's whole contribution: its own tuning, its own unison
    // stack, its own pan and its own level, summed into the voice.
    void renderOscillator(std::array<float, 8>& phases, const Oscillator& osc, float baseHz,
                          float lfo, float lfoPosition, float dt, float& left, float& right) const
    {
        if (!on(osc.enable)) return;
        const auto count = juce::jlimit(1, static_cast<int>(phases.size()), juce::roundToInt(osc.unison));
        const auto position = juce::jlimit(0.0f, 1.0f, osc.position + lfo * lfoPosition * 0.5f);
        const auto hz = baseHz * tuningRatio(osc);
        const auto detune = juce::jlimit(0.0f, 1.0f, osc.detune);
        const auto blend = juce::jlimit(0.0f, 1.0f, osc.blend);

        auto stackLeft = 0.0f, stackRight = 0.0f, power = 0.0f;
        for (int i = 0; i < count; ++i)
        {
            const auto spread = count == 1 ? 0.0f
                : static_cast<float>(i) / static_cast<float>(count - 1) - 0.5f;
            // Blend balances the centre of the stack against its edges: at 0
            // only the centre voices are heard, at 1 the whole stack is level.
            const auto centreWeight = 1.0f - juce::jmin(1.0f, std::abs(spread) * 2.0f);
            const auto gain = juce::jmap(blend, centreWeight, 1.0f);
            power += gain * gain;

            const auto sample = morph(phases[static_cast<size_t>(i)], position) * gain;
            const auto pan = juce::jlimit(-1.0f, 1.0f, osc.pan + spread * detune * 1.6f);
            stackLeft += sample * std::sqrt(0.5f * (1.0f - pan));
            stackRight += sample * std::sqrt(0.5f * (1.0f + pan));

            const auto ratio = std::pow(2.0f, spread * detune * 0.7f / 12.0f);
            phases[static_cast<size_t>(i)] = wrap(phases[static_cast<size_t>(i)] + hz * ratio * dt);
        }

        // Power normalisation, so widening the stack changes the sound without
        // changing how loud the oscillator is.
        const auto scale = juce::jlimit(0.0f, 1.0f, osc.level) / std::sqrt(std::max(0.0001f, power));
        left += stackLeft * scale;
        right += stackRight * scale;
    }

    void renderOscillators(Voice& voice, const Patch& patch, float lfo, Buses& buses)
    {
        const auto dt = static_cast<float>(1.0 / sampleRate);
        const auto glide = juce::jlimit(0.0f, 2.0f, patch.glide);
        if (glide <= 0.0001f) voice.currentHz = voice.targetHz;
        else voice.currentHz += (voice.targetHz - voice.currentHz)
            * (1.0f - std::exp(-1.0f / (static_cast<float>(sampleRate) * glide)));
        const auto pitchRatio = std::pow(2.0f, juce::jlimit(-12.0f, 12.0f, patch.lfoPitch) * lfo / 12.0f);
        const auto hz = voice.currentHz * pitchRatio;

        // Each source is handed whichever pair of accumulators its routing
        // selects, so the routing decision is made once, here, rather than
        // being threaded through everything downstream.
        renderOscillator(voice.phaseA, patch.a, hz, lfo, patch.lfoPosition, dt,
                         on(patch.routeA) ? buses.wetLeft : buses.dryLeft,
                         on(patch.routeA) ? buses.wetRight : buses.dryRight);
        renderOscillator(voice.phaseB, patch.b, hz, lfo, patch.lfoPosition, dt,
                         on(patch.routeB) ? buses.wetLeft : buses.dryLeft,
                         on(patch.routeB) ? buses.wetRight : buses.dryRight);

        // The sub and the noise generator are their own sources: each is silent
        // unless its own module is on, whatever its level knob reads.
        if (on(patch.subEnable))
        {
            const auto sub = std::sin(voice.phaseSub * juce::MathConstants<float>::twoPi) * patch.subLevel;
            (on(patch.routeSub) ? buses.wetLeft : buses.dryLeft) += sub;
            (on(patch.routeSub) ? buses.wetRight : buses.dryRight) += sub;
        }
        if (on(patch.noiseEnable))
        {
            const auto hiss = noise() * patch.noiseLevel;
            (on(patch.routeNoise) ? buses.wetLeft : buses.dryLeft) += hiss;
            (on(patch.routeNoise) ? buses.wetRight : buses.dryRight) += hiss;
        }

        const auto level = voice.ampEnvelope * voice.velocity;
        buses.wetLeft *= level;
        buses.wetRight *= level;
        buses.dryLeft *= level;
        buses.dryRight *= level;
        voice.phaseSub = wrap(voice.phaseSub + hz * 0.5f * dt);
    }

    // A state-variable filter computes all three responses anyway, so the type
    // is a choice of which tap to return rather than a second filter.
    float filter(float input, float& low, float& band, float cutoff, float resonance, FilterType type) const
    {
        const auto limitedCutoff = juce::jlimit(25.0f, static_cast<float>(sampleRate * 0.3), cutoff);
        const auto g = std::tan(juce::MathConstants<float>::pi * limitedCutoff / static_cast<float>(sampleRate));
        const auto damping = 1.0f / (1.0f + juce::jlimit(0.0f, 1.0f, resonance) * 15.0f);
        const auto high = (input - 2.0f * damping * band - low) / (1.0f + 2.0f * damping * g + g * g);
        band += g * high;
        low += g * band;
        switch (type)
        {
            case FilterType::highPass: return high;
            case FilterType::bandPass: return band;
            case FilterType::lowPass: break;
        }
        return low;
    }

    std::array<Voice, 16> voices {};
    double sampleRate = 48000.0;
    size_t nextVoice = 0;
    float lfoPhase = 0.0f;
    std::uint32_t noiseState = 0x9e3779b9u;
    std::array<int, 16> heldNotes {};
    int heldCount = 0;
    bool monoMode = false;
    float meterEnvelope = 0.0f;
    EnvelopeStage meterStage = EnvelopeStage::idle;
};
}
