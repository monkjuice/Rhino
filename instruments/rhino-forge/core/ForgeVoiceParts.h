#pragma once

#include "ForgeWavetable.h"
#include "ForgeWarp.h"
#include "ForgeLfoTable.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

// What one voice is made of, as counts and as plain settings: how many
// oscillators, unison slots, LFOs, envelopes, macros and busses there are, and
// what each of those holds.
//
// Everything here is a value a parameter read can fill directly. Nothing here
// renders anything — the shapes are in ForgeShapes.h and the voice that plays
// them is in ForgeCore.h.
namespace rhino::forge
{
// How many oscillators a voice has. They are identical and neither is defined
// in terms of the other, which is what lets everything indexed by oscillator —
// a table, a warp destination — be a pair rather than two special cases.
inline constexpr int oscillatorCount = 2;

// How many detuned copies one oscillator's unison stack may hold. The per-voice
// phase arrays are this long, and the Unison parameter's range stops here, so
// the two cannot drift apart.
inline constexpr int unisonMax = 12;

// How far apart detune spreads the stack, in semitones, at the top of the knob.
// Wide enough that a full stack still beats several times a second: the gap
// between neighbours is this divided by the voice count, and a gap that beats
// slower than about once a second stops sounding like chorus and starts
// sounding like a throb.
inline constexpr float unisonSpreadSemitones = 1.4f;

// How far off its even position each member of the stack is pushed, as a share
// of the gap between neighbours.
inline constexpr float unisonJitter = 0.35f;

// A deterministic 32-bit mix. Used where a value has to be spread out but must
// also be identical on every run and every machine, because the tests measure
// what comes out: the stack's detune jitter and its starting phases both come
// from here rather than from a random number generator.
inline juce::uint32 mix32(juce::uint32 x)
{
    x *= 2654435761u;
    x ^= x >> 15;
    x *= 2246822519u;
    x ^= x >> 13;
    return x;
}

inline float unitFromHash(juce::uint32 x)
{
    return static_cast<float>(mix32(x) & 0xffffu) / 65536.0f;
}

// Where one member of the stack sits within the detune span, from -0.5 to 0.5.
//
// The members are deliberately not evenly spaced. Even spacing gives every
// neighbouring pair the same beat rate, so every beat in the stack is a
// multiple of the slowest one and the whole stack swings in and out of phase
// together on a single slow period — heard as a throb rather than as chorus,
// and worse the more voices there are, because the gap and therefore the beat
// gets slower with each one added. Pushing each member off the grid by a fixed
// amount leaves the beat rates sharing no common period, so the movement stays
// continuous. The two ends are pinned, so the stack still spans exactly what
// detune asks for, and the push is small enough that members never reorder.
inline float unisonOffset(int slot, int count)
{
    if (count < 2) return 0.0f;
    const auto last = count - 1;
    if (slot <= 0) return -0.5f;
    if (slot >= last) return 0.5f;
    const auto jitter = unitFromHash(static_cast<juce::uint32>(slot)) * 2.0f - 1.0f;
    return static_cast<float>(slot) / static_cast<float>(last) - 0.5f
         + jitter * unisonJitter / static_cast<float>(last);
}

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
    // The two warp stages, in the order they are applied. A mode is held as a
    // float for the same reason everything else here is: that is what a
    // parameter read gives back. See ForgeWarp.h for what each one does.
    std::array<float, warpSlots> warpMode {};
    std::array<float, warpSlots> warpAmount {};
    // The table this oscillator reads. Null means the built-in frames, which is
    // what every oscillator starts on and what a Core needs no setting up to
    // sound. The Processor owns whatever this points at and outlives the voice
    // reading it; nothing here allocates or frees it.
    const Wavetable* table = nullptr;
};

// Which of the filter's three taps reaches the output.
enum class FilterType { lowPass, highPass, bandPass };

// another, and the panel shows one at a time rather than six at once.
inline constexpr int lfoCount = 6;

// The LFO shapes. An LFO is a modulation source, so every shape is bipolar and
// runs the full -1..1: a slot's depth decides how much of that reaches anything.
enum class LfoShape { sine, triangle, saw, square, sampleHold };
inline constexpr int lfoShapeCount = 5;

inline const char* lfoShapeName(int shape)
{
    switch (shape)
    {
        case 1: return "TRI";
        case 2: return "SAW";
        case 3: return "SQR";
        case 4: return "S&H";
        default: break;
    }
    return "SINE";
}

inline const char* lfoFullShapeName(int shape)
{
    switch (shape)
    {
        case 1: return "Triangle";
        case 2: return "Saw";
        case 3: return "Square";
        case 4: return "Sample & Hold";
        default: break;
    }
    return "Sine";
}

// The shape at a point in its cycle. `held` is the sample-and-hold's current
// step, the one shape that cannot be worked out from the phase alone. A free
// function so the panel draws the very curve the voice is reading, the way the
// oscillator display and the oscillator share morph().
inline float lfoWave(LfoShape shape, float phase, float held)
{
    switch (shape)
    {
        case LfoShape::triangle:   return 1.0f - 4.0f * std::abs(phase - 0.5f);
        case LfoShape::saw:        return phase * 2.0f - 1.0f;
        case LfoShape::square:     return phase < 0.5f ? 1.0f : -1.0f;
        case LfoShape::sampleHold: return held;
        case LfoShape::sine:       break;
    }
    return std::sin(phase * juce::MathConstants<float>::twoPi);
}

// How LFO 1 answers the keyboard. TRIG starts the shape again on every new
// note and then loops for as long as one is held. ENV starts it again too but
// stops on the last point of one cycle and holds it, which turns any shape into
// a one-shot envelope of its own. OFF never restarts: it free-runs across
// notes, so a tempo-synced setting stays in step with the host from one phrase
// to the next rather than jumping every time a key goes down.
enum class LfoMode { trigger, envelope, free };
inline constexpr int lfoModeCount = 3;

inline const char* lfoModeName(int mode)
{
    switch (mode)
    {
        case 1: return "ENV";
        case 2: return "OFF";
        default: break;
    }
    return "TRIG";
}

// Which unit the rate is set in. The two are not a toggle with an implied
// default: HZ and BPM are equal readings of one setting, and the panel shows
// whichever is in charge in place of the other rather than beside it.
inline const char* lfoRateUnitName(int unit) { return unit >= 1 ? "BPM" : "HZ"; }
inline constexpr int lfoRateUnitCount = 2;

// Everything one LFO owns. The rate is always in Hertz — a unit of beats is
// resolved to one before the patch is built, so the Core never sees a tempo.
// Held as floats because that is what a parameter read gives back.
struct LfoSetting
{
    float rate = 0.5f;
    float shape = 0.0f;
    float mode = 0.0f;
    LfoTable table;
};

// The mode and the shape a setting names, clamped, so one reading serves the
// voice, the panel and the tests.
inline LfoMode lfoModeOf(const LfoSetting& lfo)
{
    return static_cast<LfoMode>(juce::jlimit(0, lfoModeCount - 1, juce::roundToInt(lfo.mode)));
}

// A parameter id belonging to one LFO: lfoParameterId(0, "Shape") is
// "lfo1Shape". One spelling of the pattern, shared by the parameters, the
// panel and the tests, so a bank of six cannot drift apart from the ids it
// names. The panel's own declaration writes them out in full instead, because
// that file is deliberately read rather than computed.
inline juce::String lfoParameterId(int lfo, const char* suffix)
{
    return "lfo" + juce::String(lfo + 1) + suffix;
}

inline LfoShape lfoShapeOf(const LfoSetting& lfo)
{
    return static_cast<LfoShape>(juce::jlimit(0, lfoShapeCount - 1, juce::roundToInt(lfo.shape)));
}

inline float lfoValue(const LfoSetting& setting, float phase, float held)
{
    return setting.table.custom ? setting.table.sample(phase)
                                : lfoWave(lfoShapeOf(setting), phase, held);
}

// Tempo-synced rates, as the length of one LFO cycle in beats. A beat is a
// quarter note, so 1/4 is one beat and 1/1 is a bar of four. Musical data
// rather than host data, which is why it sits here beside the shapes: the Core
// never sees a tempo, the Processor turns one of these into a rate in Hz.
struct LfoDivision
{
    const char* label;
    float beats;
};

inline const std::array<LfoDivision, 7>& lfoDivisions()
{
    static const std::array<LfoDivision, 7> table {{
        {"1/1", 4.0f},        {"1/2", 2.0f},        {"1/4", 1.0f},
        {"1/4T", 2.0f / 3.0f}, {"1/8", 0.5f},       {"1/8T", 1.0f / 3.0f},
        {"1/16", 0.25f},
    }};
    return table;
}

inline constexpr int lfoDivisionCount = 7;

// Four envelopes. ENV 1 is hardwired to the voice amplitude, exactly as Serum's
// is, and is also what says when a voice is finished; ENV 2-4 are sources and
// nothing else, so one reaches a control through a modulation slot or not at
// all. All four are shaped identically and all four run in every voice, which
// is what makes an auxiliary envelope answer the keyboard the same way the amp
// one does — the same note starts it and the same key lifting releases it.
inline constexpr int envCount = 4;

// ENV 1's place in everything indexed by envelope. Written as a name because
// "the amp envelope" is what the voice lifecycle, the metering and the level a
// voice is rendered at all mean by index zero.
inline constexpr int ampEnv = 0;

// Everything one envelope owns. Held as floats because that is what a parameter
// read gives back.
struct EnvSetting
{
    float attack = 0.01f, decay = 0.24f, sustain = 0.75f, release = 0.35f;
};

// A parameter id belonging to one envelope: envParameterId(0, "Attack") is
// "env1Attack". The same spelling of the pattern the LFOs use, and for the same
// reason: a bank of four cannot drift apart from the ids it names.
inline juce::String envParameterId(int env, const char* suffix)
{
    return "env" + juce::String(env + 1) + suffix;
}

inline constexpr int macroCount = 8;

// How many mixer busses there are. Two, as Serum has: enough for the two
// parallel paths a patch actually wants — a space and a colour — and few
// enough that every channel can carry a send to each without the strip
// becoming a list.
inline constexpr int busCount = 2;

// Modulation sources. The envelopes and velocity are unipolar (0..1); the LFOs
// are bipolar (-1..1); note is unipolar across the keyboard.
//
// The values are written out rather than left to the compiler because they are
// what a slot's Source parameter stores, so the order of this list is saved
// inside every preset. The four envelopes are one run and the six LFOs are
// another, and a run's length is what everything after it is placed past —
// which is why the sources after the LFOs moved when LFO 2-6 arrived, and why
// everything after ENV 1 moved again when ENV 2-4 did. A preset written before
}
