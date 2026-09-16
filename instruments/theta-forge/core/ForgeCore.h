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

// Modulation sources. ENV 1 and velocity are unipolar (0..1); LFO 1 is bipolar
// (-1..1); note is unipolar across the keyboard.
enum class ModSource { off, env1, lfo1, velocity, note, macro1 };
inline constexpr int macroCount = 8;
// Everything up to the macros, then one entry per macro.
inline constexpr int modSourceCount = static_cast<int>(ModSource::macro1) + macroCount;

inline int macroIndexOf(int source)
{
    const auto first = static_cast<int>(ModSource::macro1);
    return source >= first && source < first + macroCount ? source - first : -1;
}

inline const char* modSourceName(int source)
{
    switch (source)
    {
        case 1: return "ENV 1";
        case 2: return "LFO 1";
        case 3: return "VELOCITY";
        case 4: return "NOTE";
        default: break;
    }
    static const std::array<const char*, macroCount> macros {
        "MACRO 1", "MACRO 2", "MACRO 3", "MACRO 4",
        "MACRO 5", "MACRO 6", "MACRO 7", "MACRO 8"};
    const auto macro = macroIndexOf(source);
    return macro >= 0 ? macros[static_cast<size_t>(macro)] : "OFF";
}

// Everything a slot may be pointed at. Index 0 is "nothing". The id is the
// parameter the destination corresponds to; the Processor uses it to hand the
// Core that parameter's range, so modulation happens in the same normalised
// space the knob moves in and nothing here has to duplicate a range.
struct DestinationInfo
{
    const char* id;
    const char* label;
};

inline const std::array<DestinationInfo, 16>& destinations()
{
    static const std::array<DestinationInfo, 16> table {{
        {"", "OFF"},
        {"oscAPosition", "A POS"},   {"oscALevel", "A LEVEL"}, {"oscAPan", "A PAN"},
        {"oscADetune", "A DETUNE"},  {"oscASemitone", "A PITCH"},
        {"oscBPosition", "B POS"},   {"oscBLevel", "B LEVEL"}, {"oscBPan", "B PAN"},
        {"oscBDetune", "B DETUNE"},  {"oscBSemitone", "B PITCH"},
        {"subLevel", "SUB"},         {"noiseLevel", "NOISE"},
        {"cutoff", "CUTOFF"},        {"resonance", "RES"},     {"drive", "DRIVE"},
    }};
    return table;
}

// Output is deliberately absent: it is applied once after the voices are
// summed, so a per-voice modulation of it would not mean anything.
inline constexpr int destinationCount = 16;
inline constexpr int modSlotCount = 8;

// Held as floats because that is what a parameter read gives back, and it
// keeps the slot a plain value the processor can fill without conversion.
struct ModSlot
{
    float source = 0.0f;
    float destination = 0.0f;
    float depth = 0.0f;
};

struct Modulation
{
    std::array<ModSlot, modSlotCount> slots {};

    bool anyActive() const
    {
        for (const auto& slot : slots)
            if (slot.source >= 0.5f && slot.destination >= 0.5f && slot.depth != 0.0f)
                return true;
        return false;
    }
};

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
    // LFO 1 has a rate and nothing else. It is a source, not a router: where it
    // goes is a matter for the modulation slots.
    float lfoRate = 0.5f;
    float polyphony = 8.0f, mono = 0.0f, legato = 1.0f, glide = 0.08f;
    float output = 0.75f;
    // Performance macros. Sources only: a macro is a hand on a knob, and what
    // it reaches is a matter for the matrix.
    std::array<float, macroCount> macros {};
};

inline bool on(float enable) { return enable >= 0.5f; }

// The field a destination index names, inside a patch that is about to be
// modulated. Null for "nothing", which is also what an out-of-range index gets.
inline float* destinationField(Patch& patch, int destination)
{
    switch (destination)
    {
        case 1:  return &patch.a.position;
        case 2:  return &patch.a.level;
        case 3:  return &patch.a.pan;
        case 4:  return &patch.a.detune;
        case 5:  return &patch.a.semitone;
        case 6:  return &patch.b.position;
        case 7:  return &patch.b.level;
        case 8:  return &patch.b.pan;
        case 9:  return &patch.b.detune;
        case 10: return &patch.b.semitone;
        case 11: return &patch.subLevel;
        case 12: return &patch.noiseLevel;
        case 13: return &patch.cutoff;
        case 14: return &patch.resonance;
        case 15: return &patch.drive;
        default: break;
    }
    return nullptr;
}

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
        meterOffsets = {};
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

    // How far the matrix is moving each destination right now, in that
    // destination's normalised space, so a knob can draw where its value
    // actually is while a source plays it. Same reading the loudest voice is
    // rendering with, for the same reason ENV 1's display follows that voice.
    //
    // Zero with nothing sounding, because with no voice there is no modulated
    // value: a source only reaches a destination through a voice. That is the
    // same rule ENV 1's display follows, and it is what stops the panel
    // animating a patch that is making no sound.
    float modulationOffset(int destination) const
    {
        return destination > 0 && destination < destinationCount
            ? meterOffsets[static_cast<size_t>(destination)] : 0.0f;
    }

    // A destination is modulated in the same normalised space its knob moves
    // in, so a depth of 1.0 means "from here to the top of the knob's travel"
    // whatever the underlying units or skew are. The Processor hands these
    // ranges over at prepare time, so nothing here duplicates a parameter range.
    void setDestinationRange(int destination, juce::NormalisableRange<float> range)
    {
        if (destination > 0 && destination < destinationCount)
            destinationRanges[static_cast<size_t>(destination)] = range;
    }

    void renderSample(const Patch& patch, float& left, float& right)
    {
        renderSample(patch, Modulation {}, left, right);
    }

    void renderSample(const Patch& patch, const Modulation& modulation, float& left, float& right)
    {
        left = right = 0.0f;
        const auto lfo = std::sin(lfoPhase * juce::MathConstants<float>::twoPi);
        lfoPhase = wrap(lfoPhase + juce::jlimit(0.01f, 40.0f, patch.lfoRate) / static_cast<float>(sampleRate));
        const auto modulated = modulation.anyActive();

        meterEnvelope = 0.0f;
        meterStage = EnvelopeStage::idle;
        meterOffsets = {};

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
            const auto loudest = voice.ampEnvelope >= meterEnvelope;
            if (loudest)
            {
                meterEnvelope = voice.ampEnvelope;
                meterStage = voice.ampStage;
            }

            // Modulation is per voice and per sample, because every source
            // except the LFO is per voice and every destination is read inside
            // the voice. With no live slot the patch is used as it stands and
            // nothing is copied.
            const Patch* voicePatch = &patch;
            if (modulated)
            {
                scratch = patch;
                applyModulation(scratch, modulation, voice, lfo, loudest);
                voicePatch = &scratch;
            }
            const auto& active = *voicePatch;

            Buses buses;
            renderOscillators(voice, active, buses);

            // Drive belongs to the filter, so only what is routed into it is
            // driven, and switching the module off bypasses the drive with it.
            // The filter state keeps running either way, so switching the
            // module or a route back on does not click.
            auto routedLeft = buses.wetLeft, routedRight = buses.wetRight;
            const auto type = filterTypeOf(active);
            if (on(active.filterEnable))
            {
                routedLeft = filter(saturate(routedLeft, active.drive),
                                    voice.lowLeft, voice.bandLeft, active.cutoff, active.resonance, type);
                routedRight = filter(saturate(routedRight, active.drive),
                                     voice.lowRight, voice.bandRight, active.cutoff, active.resonance, type);
            }
            else
            {
                filter(routedLeft, voice.lowLeft, voice.bandLeft, active.cutoff, active.resonance, type);
                filter(routedRight, voice.lowRight, voice.bandRight, active.cutoff, active.resonance, type);
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

    // Each live slot nudges its destination in normalised space and the result
    // is converted back to the destination's own units, so one depth control
    // behaves the same whether it points at a percentage, a frequency with a
    // skewed range, or a pan position.
    //
    // `publish` marks the one voice whose reading the knobs draw, so the panel
    // shows what is happening to the voice a player is listening to rather than
    // to whichever voice happened to be rendered last. The offsets are handed
    // over as the voice renders with them, so the ring on a knob and the sound
    // cannot come from two different readings.
    void applyModulation(Patch& target, const Modulation& modulation, const Voice& voice,
                         float lfo, bool publish = false)
    {
        // Offsets are accumulated per destination first and applied once.
        // Applying each slot in turn would round-trip through the destination's
        // range between slots, so two half-depth slots would not add up to one
        // at full depth, and an early slot hitting a limit would swallow a
        // later one pulling the other way.
        std::array<float, destinationCount> offsets {};
        auto touched = false;

        for (const auto& slot : modulation.slots)
        {
            const auto source = juce::roundToInt(slot.source);
            const auto destination = juce::roundToInt(slot.destination);
            if (source <= 0 || destination <= 0 || destination >= destinationCount || slot.depth == 0.0f)
                continue;

            auto amount = 0.0f;
            switch (static_cast<ModSource>(source))
            {
                case ModSource::env1:     amount = voice.ampEnvelope; break;
                case ModSource::lfo1:     amount = lfo; break;
                case ModSource::velocity: amount = voice.velocity; break;
                case ModSource::note:     amount = static_cast<float>(voice.note) / 127.0f; break;
                case ModSource::off:      continue;
                default:
                {
                    const auto macro = macroIndexOf(source);
                    if (macro < 0) continue;
                    amount = target.macros[static_cast<size_t>(macro)];
                    break;
                }
            }

            offsets[static_cast<size_t>(destination)] += slot.depth * amount;
            touched = true;
        }
        if (publish) meterOffsets = offsets;
        if (!touched) return;

        for (int destination = 1; destination < destinationCount; ++destination)
        {
            const auto offset = offsets[static_cast<size_t>(destination)];
            if (offset == 0.0f) continue;
            auto* field = destinationField(target, destination);
            if (field == nullptr) continue;
            const auto& range = destinationRanges[static_cast<size_t>(destination)];
            *field = range.convertFrom0to1(juce::jlimit(0.0f, 1.0f, range.convertTo0to1(*field) + offset));
        }
    }

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
                          float dt, float& left, float& right) const
    {
        if (!on(osc.enable)) return;
        const auto count = juce::jlimit(1, static_cast<int>(phases.size()), juce::roundToInt(osc.unison));
        const auto position = juce::jlimit(0.0f, 1.0f, osc.position);
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

    void renderOscillators(Voice& voice, const Patch& patch, Buses& buses)
    {
        const auto dt = static_cast<float>(1.0 / sampleRate);
        const auto glide = juce::jlimit(0.0f, 2.0f, patch.glide);
        if (glide <= 0.0001f) voice.currentHz = voice.targetHz;
        else voice.currentHz += (voice.targetHz - voice.currentHz)
            * (1.0f - std::exp(-1.0f / (static_cast<float>(sampleRate) * glide)));
        const auto hz = voice.currentHz;

        // Each source is handed whichever pair of accumulators its routing
        // selects, so the routing decision is made once, here, rather than
        // being threaded through everything downstream.
        renderOscillator(voice.phaseA, patch.a, hz, dt,
                         on(patch.routeA) ? buses.wetLeft : buses.dryLeft,
                         on(patch.routeA) ? buses.wetRight : buses.dryRight);
        renderOscillator(voice.phaseB, patch.b, hz, dt,
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
    std::array<juce::NormalisableRange<float>, destinationCount> destinationRanges {};
    // Reused every voice and every sample so a modulated render allocates
    // nothing; only touched when at least one slot is live.
    Patch scratch {};
    std::array<int, 16> heldNotes {};
    int heldCount = 0;
    bool monoMode = false;
    float meterEnvelope = 0.0f;
    EnvelopeStage meterStage = EnvelopeStage::idle;
    std::array<float, destinationCount> meterOffsets {};
};
}
