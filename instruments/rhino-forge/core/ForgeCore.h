#pragma once

#include "ForgeWavetable.h"
#include "ForgeFxDsp.h"
#include "ForgeWarp.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// The reusable sound engine. It deliberately owns no AudioProcessor, UI,
// Tracktion, state tree, filesystem, or allocation in renderSample().
//
// The voices are this file's. The effects are not: they live in ForgeFxDsp.h
// and run on the summed voices rather than inside them, the way an insert after
// the synth does. Core owns the three racks because they hold delay lines and
// filter state, and hands each of them the patch that describes it.
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

// --- The oscillator's table ---------------------------------------------------
//
// Ten single-cycle shapes that POSITION morphs through: the table an oscillator
// reads until one is loaded over it.
//
// These formulas are a generator, not the render path. Since M9b-1 they are
// rendered into a real Wavetable once, at startup, and the voice reads that
// table — see ForgeWavetable.h. Writing them as formulas is still the right way
// to author them, but a frame is now sampled data by the time anything plays
// it, which is what lets a frame come from a file or a brush instead.
//
// The order matters as much as the contents: POSITION crossfades whichever two
// frames it falls between, so what sits next to what is what the in-between
// positions sound like. They run from the softest through the pulse family to
// the brightest, with each frame a relative of the one before it.
inline constexpr int waveShapeCount = 10;

inline const char* waveShapeName(int shape)
{
    switch (shape)
    {
        case 1: return "TRI";
        case 2: return "TRAP";
        case 3: return "SQR";
        case 4: return "PULSE";
        case 5: return "THIN";
        case 6: return "SAW";
        case 7: return "HUMP";
        case 8: return "ORGAN";
        case 9: return "VOX";
        default: break;
    }
    return "SINE";
}

// One frame, at a point in its cycle. Every frame is bipolar and reaches full
// scale, so morphing between any two never changes the oscillator's level.
inline float waveShape(int shape, float phase)
{
    const auto cycle = phase * juce::MathConstants<float>::twoPi;
    const auto triangle = 1.0f - 4.0f * std::abs(phase - 0.5f);
    switch (shape)
    {
        case 1: return triangle;
        // A triangle driven past full scale and clipped: flat tops with sloped
        // sides, which is what sits between a triangle and a square.
        case 2: return juce::jlimit(-1.0f, 1.0f, triangle * 3.0f);
        case 3: return phase < 0.5f ? 1.0f : -1.0f;
        case 4: return phase < 0.25f ? 1.0f : -1.0f;
        case 5: return phase < 0.1f ? 1.0f : -1.0f;
        // A rising saw with its jump in the middle of the frame rather than at
        // the edge of it. Rotating a saw by half a cycle changes nothing about
        // what it is — same harmonics, same sound — but it is the difference
        // between a display that shows one diagonal and one that shows the edge
        // that makes a saw a saw. Serum stores its own saw the same way round.
        case 6: return phase < 0.5f ? phase * 2.0f : phase * 2.0f - 2.0f;
        // A rectified sine: two humps a cycle, so the even harmonics arrive and
        // it reads an octave up without being one.
        case 7: return 2.0f * std::abs(std::sin(cycle)) - 1.0f;
        // Drawbars: a fundamental with an octave and a twelfth over it. The
        // scaling is the measured peak of that sum, so it fills the range
        // without being clipped into a different shape.
        case 8: return 0.694224f * (std::sin(cycle) + 0.5f * std::sin(2.0f * cycle)
                                                    + 0.33f * std::sin(3.0f * cycle));
        // A formant: a burst of the third harmonic under a raised cosine, which
        // is the shape a vowel makes and sounds like one.
        case 9: return 1.067774f * std::sin(3.0f * cycle) * (0.5f - 0.5f * std::cos(cycle));
        default: break;
    }
    return std::sin(cycle);
}

// The built-in ten, rendered into a real table once and read from then on.
// The formulas above are now a generator rather than the thing the voice calls:
// every oscillator reads a Wavetable, and this is the one it reads until a
// table is loaded over it. Built on first use, which the Processor and Core
// force to happen at construction so that no audio thread is ever the first
// caller — building it allocates and runs a transform per frame.
inline const Wavetable& builtInWavetable()
{
    static const Wavetable table = []
    {
        std::vector<float> samples(static_cast<size_t>(waveShapeCount) * wavetableFrameSize);
        std::vector<juce::String> names;
        names.reserve(static_cast<size_t>(waveShapeCount));
        for (int shape = 0; shape < waveShapeCount; ++shape)
        {
            names.push_back(waveShapeName(shape));
            for (int i = 0; i < wavetableFrameSize; ++i)
                samples[static_cast<size_t>(shape) * wavetableFrameSize + static_cast<size_t>(i)]
                    = waveShape(shape, static_cast<float>(i) / static_cast<float>(wavetableFrameSize));
        }
        return Wavetable(samples.data(), waveShapeCount, "BASIC SHAPES", std::move(names));
    }();
    return table;
}

// Where a position falls in the built-in table: the frame at or below it, and
// how far past that frame it has travelled.
inline void waveFrameAt(float position, int& frame, float& blend)
{
    wavetableFrameAt(builtInWavetable().frameCount(), position, frame, blend);
}

// One sample of the built-in table at a position, as authored — no band
// limiting, because this is what the panel draws rather than what a note reads.
inline float waveAt(float position, float phase)
{
    return builtInWavetable().sample(position, phase);
}

// What a position is called, for a knob to read out. Exactly on a frame it is
// that frame's name; between two it names both, because a blend of two shapes is
// honestly what it is. Never called from the render.
inline juce::String waveLabel(float position)
{
    const auto& table = builtInWavetable();
    int frame = 0;
    auto blend = 0.0f;
    wavetableFrameAt(table.frameCount(), position, frame, blend);
    if (blend <= 0.01f) return table.frameTitle(frame);
    if (blend >= 0.99f) return table.frameTitle(frame + 1);
    return table.frameTitle(frame) + ">" + table.frameTitle(frame + 1);
}

// --- The sub oscillator's shapes ----------------------------------------------
//
// Six of them, in the order and with the names Serum's sub gives them: a sine,
// a rounded rectangle, a triangle, a saw, a square and a pulse. That order runs
// from the shape with nothing but a fundamental to the ones with the most
// harmonics over it, which is what a picker of six icons wants to read as.
//
// They are frames of a table rather than formulas evaluated per sample, for the
// reason every other shape in Forge is: a saw an octave below the note is a
// harmonic series running past Nyquist, and the band-limited copies a Wavetable
// keeps are what stop the top of it folding back down. The sub was a sine until
// now and a sine has nothing to fold, which is why this is the moment the table
// is needed.
inline constexpr int subShapeCount = 6;

inline const char* subShapeName(int shape)
{
    switch (shape)
    {
        case 1: return "RECT";
        case 2: return "TRI";
        case 3: return "SAW";
        case 4: return "SQR";
        case 5: return "PULSE";
        default: break;
    }
    return "SINE";
}

// One sub frame, at a point in its cycle. Bipolar and full scale, like every
// other frame Forge authors, so changing shape moves the harmonics rather than
// the level.
inline float subShape(int shape, float phase)
{
    const auto cycle = phase * juce::MathConstants<float>::twoPi;
    switch (shape)
    {
        // A sine driven past full scale and clipped: flat tops with the sine's
        // own curve either side of them, which is the rounded rectangle the
        // manual describes -- between a sine and a square, and softer at the
        // corners than the trapezoid the main table carries.
        case 1: return juce::jlimit(-1.0f, 1.0f, std::sin(cycle) * 2.6f);
        case 2: return 1.0f - 4.0f * std::abs(phase - 0.5f);
        // Rotated half a cycle, the same way the main table stores its saw:
        // same harmonics either way round, but the picture shows the edge.
        case 3: return phase < 0.5f ? phase * 2.0f : phase * 2.0f - 2.0f;
        case 4: return phase < 0.5f ? 1.0f : -1.0f;
        case 5: return phase < 0.25f ? 1.0f : -1.0f;
        default: break;
    }
    return std::sin(cycle);
}

// The six, rendered into a band-limited table once. A table of its own rather
// than frames added to the built-in ten: POSITION morphs between neighbours in
// that one, and a sub shape is chosen outright, so putting them in the same
// table would have put six shapes in the oscillators' morph that nobody asked
// to travel through.
inline const Wavetable& subWavetable()
{
    static const Wavetable table = []
    {
        std::vector<float> samples(static_cast<size_t>(subShapeCount) * wavetableFrameSize);
        std::vector<juce::String> names;
        names.reserve(static_cast<size_t>(subShapeCount));
        for (int shape = 0; shape < subShapeCount; ++shape)
        {
            names.push_back(subShapeName(shape));
            for (int i = 0; i < wavetableFrameSize; ++i)
                samples[static_cast<size_t>(shape) * wavetableFrameSize + static_cast<size_t>(i)]
                    = subShape(shape, static_cast<float>(i) / static_cast<float>(wavetableFrameSize));
        }
        return Wavetable(samples.data(), subShapeCount, "SUB SHAPES", std::move(names));
    }();
    return table;
}

// Which shape a sub setting names. Rounded rather than truncated, and clamped,
// so a value arriving from a host lands on a real frame.
inline int subShapeOf(float wave)
{
    return juce::jlimit(0, subShapeCount - 1, juce::roundToInt(wave));
}

// How far the sub sits below the note. OCT 0 is one octave down -- the pitch
// the sub has always run at, and what makes it a sub -- so every patch written
// before the control existed still sounds as it did, and the reading matches
// Serum's, whose own sub is an octave below at zero.
inline float subRatio(float octave)
{
    return 0.5f * std::pow(2.0f, juce::jlimit(-2.0f, 2.0f, octave));
}

// How many LFOs there are. They are identical: none is defined in terms of
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
// either is remapped on the way in; see Processor::migrated.
enum class ModSource
{
    off = 0,
    env1 = 1,                        // env2..env4 follow, up to 4
    lfo1 = env1 + envCount,          // 5, and lfo2..lfo6 up to 10
    velocity = lfo1 + lfoCount,      // 11
    note,                            // 12
    macro1                           // 13, and one per macro after it
};

inline constexpr int modSourceCount = static_cast<int>(ModSource::macro1) + macroCount;

inline int macroIndexOf(int source)
{
    const auto first = static_cast<int>(ModSource::macro1);
    return source >= first && source < first + macroCount ? source - first : -1;
}

// Which LFO a source names, or -1 for a source that is not one.
inline int lfoIndexOf(int source)
{
    const auto first = static_cast<int>(ModSource::lfo1);
    return source >= first && source < first + lfoCount ? source - first : -1;
}

// Which envelope a source names, or -1 for a source that is not one.
inline int envIndexOf(int source)
{
    const auto first = static_cast<int>(ModSource::env1);
    return source >= first && source < first + envCount ? source - first : -1;
}

// Whether a source already swings both ways of its own accord. The LFOs do —
// lfoWave runs -1 to 1 — and nothing else does: an envelope, a velocity, a note
// number and a macro all start at nothing and only rise.
//
// This is what a slot's BI switch acts on. Centring a source that only rises
// turns it into a swing; centring one that already swings would only double its
// reach, which is the depth control's job, so BI leaves the LFOs alone.
inline bool sourceIsBipolar(int source) { return lfoIndexOf(source) >= 0; }

inline const char* modSourceName(int source)
{
    if (source == static_cast<int>(ModSource::velocity)) return "VELOCITY";
    if (source == static_cast<int>(ModSource::note)) return "NOTE";

    static const std::array<const char*, envCount> envs {
        "ENV 1", "ENV 2", "ENV 3", "ENV 4"};
    static const std::array<const char*, lfoCount> lfos {
        "LFO 1", "LFO 2", "LFO 3", "LFO 4", "LFO 5", "LFO 6"};
    static const std::array<const char*, macroCount> macros {
        "MACRO 1", "MACRO 2", "MACRO 3", "MACRO 4",
        "MACRO 5", "MACRO 6", "MACRO 7", "MACRO 8"};
    const auto env = envIndexOf(source);
    if (env >= 0) return envs[static_cast<size_t>(env)];
    const auto lfo = lfoIndexOf(source);
    if (lfo >= 0) return lfos[static_cast<size_t>(lfo)];
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

// Appended to, never inserted into: a slot stores its destination as an index
// into this list, so every index already written into a preset has to keep
// meaning what it meant. The five the mixer added therefore sit at the end
// rather than beside the controls they belong with, and the racks' come after
// those.
inline const std::vector<DestinationInfo>& destinations();

inline const std::array<DestinationInfo, 21>& namedDestinations()
{
    static const std::array<DestinationInfo, 21> table {{
        {"", "OFF"},
        {"oscAPosition", "A POS"},   {"oscALevel", "A LEVEL"}, {"oscAPan", "A PAN"},
        {"oscADetune", "A DETUNE"},  {"oscASemitone", "A PITCH"},
        {"oscBPosition", "B POS"},   {"oscBLevel", "B LEVEL"}, {"oscBPan", "B PAN"},
        {"oscBDetune", "B DETUNE"},  {"oscBSemitone", "B PITCH"},
        {"subLevel", "SUB"},         {"noiseLevel", "NOISE"},
        {"cutoff", "CUTOFF"},        {"resonance", "RES"},     {"drive", "DRIVE"},
        {"subPan", "SUB PAN"},       {"noisePan", "NOISE PAN"},
        {"filterPan", "FLT PAN"},    {"filterMix", "FLT MIX"}, {"filterLevel", "FLT LEVEL"},
    }};
    return table;
}

// Where the named list stops and the racks begin. The original four slots per
// rack remain in their historic contiguous block so every preset keeps naming
// the same destination. New slots are appended after the warp destinations
// below rather than inserted into that block.
inline constexpr int fxDestinationBase = 21;

// A slot's six knobs and its mix. LEVEL is left out on purpose — it is the
// slot's own trim rather than something to play, and a rack whose every stage
// could be swept in level is a rack that is hard to keep at a sane loudness.
inline constexpr int fxDestinationsPerSlot = fxKnobCount + 1;
inline constexpr int legacyFxDestinationCount =
    rackCount * legacyFxSlotCount * fxDestinationsPerSlot;

// Output is deliberately absent, and so are the bus levels and the sends: all
// of them are applied once the voices are summed, so a per-voice modulation of
// one would not mean anything.
//
// The rack's own controls are applied after the voices too, and they are here
// anyway. A rack is one process fed by every note, so a per-voice source
// reaching it has to resolve to a single value; Core takes the loudest voice's,
// which is the voice every other display already follows. Serum allows the same
// thing and warns about the same consequence: a per-voice envelope on an FX
// knob retriggers on every note.

// Where the racks stop and the warp depths begin. They sit past eighty-four
// generated entries rather than beside the oscillator controls they belong
// with, because this list is appended to and never inserted into: a slot stores
// its destination as an index, and moving one is moving it inside every preset
// already saved.
inline constexpr int warpDestinationBase = fxDestinationBase + legacyFxDestinationCount;
inline constexpr int warpDestinationCount = oscillatorCount * warpSlots;

inline const std::array<DestinationInfo, warpDestinationCount>& warpDestinations()
{
    static const std::array<DestinationInfo, warpDestinationCount> table {{
        {"oscAWarp1", "A WARP 1"}, {"oscAWarp2", "A WARP 2"},
        {"oscBWarp1", "B WARP 1"}, {"oscBWarp2", "B WARP 2"},
    }};
    return table;
}

// The mode a warp stage is set to is deliberately absent. A source sweeping a
// list of twenty-six unrelated modes is a stutter rather than a modulation, and
// nothing about it would be continuous; the depth beside it is the thing worth
// playing, and it is here.
inline constexpr int extendedFxDestinationBase = warpDestinationBase + warpDestinationCount;
inline constexpr int extendedFxSlotCount = fxSlotCount - legacyFxSlotCount;
inline constexpr int extendedFxDestinationCount =
    rackCount * extendedFxSlotCount * fxDestinationsPerSlot;
inline constexpr int fxDestinationCount = legacyFxDestinationCount + extendedFxDestinationCount;
inline constexpr int destinationCount = extendedFxDestinationBase + extendedFxDestinationCount;
inline constexpr int modSlotCount = 8;

// A parameter id belonging to one rack slot: fxParameterId(0, 1, "Mix") is
// "fx1s2Mix". One spelling of the pattern, shared by the parameters, the panel,
// the destination list and the tests, so rack slots cannot drift apart from
// the ids they name — the same reason the envelopes and the LFOs have one.
inline juce::String fxParameterId(int rack, int slot, const char* suffix)
{
    return "fx" + juce::String(rack + 1) + "s" + juce::String(slot + 1) + suffix;
}

// A rack's own bypass, which is the button the mixer's BUS and MAIN channels
// carry: fxRackParameterId(0, "Bypass") is "fx1Bypass".
inline juce::String fxRackParameterId(int rack, const char* suffix)
{
    return "fx" + juce::String(rack + 1) + suffix;
}

// Where one slot's run of destinations starts.
inline int fxDestinationOf(int rack, int slot, int control)
{
    if (slot < legacyFxSlotCount)
        return fxDestinationBase
             + ((rack * legacyFxSlotCount) + slot) * fxDestinationsPerSlot + control;
    return extendedFxDestinationBase
         + ((rack * extendedFxSlotCount) + slot - legacyFxSlotCount)
             * fxDestinationsPerSlot + control;
}

// The whole destination list: the named controls, then every rack slot's six
// knobs and its mix. Built once, and held by value because the generated half
// owns the strings it names — a `const char*` here would point at a temporary.
inline const std::vector<DestinationInfo>& destinations()
{
    static const std::vector<DestinationInfo> table = []
    {
        // The ids and labels the generated half needs, kept alive for as long
        // as the table points into them.
        static std::vector<juce::String> pool;
        pool.reserve(static_cast<size_t>(fxDestinationCount) * 2);

        std::vector<DestinationInfo> built;
        built.reserve(static_cast<size_t>(destinationCount));
        for (const auto& named : namedDestinations()) built.push_back(named);

        const auto appendSlot = [&built] (int rack, int slot)
        {
            for (int control = 0; control < fxDestinationsPerSlot; ++control)
            {
                const auto knob = control < fxKnobCount;
                pool.push_back(knob ? fxParameterId(rack, slot, "Knob") + juce::String(control + 1)
                                    : fxParameterId(rack, slot, "Mix"));
                // Named for where it is rather than for what it does: a
                // slot's knob 3 is a different control in a reverb and in a
                // delay, and the matrix cannot know which is in there.
                pool.push_back(juce::String(rackName(rack)) + " " + juce::String(slot + 1) + " "
                               + (knob ? "K" + juce::String(control + 1) : juce::String("MIX")));
                built.push_back({pool[pool.size() - 2].toRawUTF8(), pool.back().toRawUTF8()});
            }
        };

        for (int rack = 0; rack < rackCount; ++rack)
            for (int slot = 0; slot < legacyFxSlotCount; ++slot)
                appendSlot(rack, slot);

        for (const auto& named : warpDestinations()) built.push_back(named);

        for (int rack = 0; rack < rackCount; ++rack)
            for (int slot = legacyFxSlotCount; slot < fxSlotCount; ++slot)
                appendSlot(rack, slot);
        return built;
    }();
    return table;
}

// Held as floats because that is what a parameter read gives back, and it
// keeps the slot a plain value the processor can fill without conversion.
struct ModSlot
{
    float source = 0.0f;
    float destination = 0.0f;
    float depth = 0.0f;
    // Whether the source is centred before the depth is applied — see
    // applyModulation. Held as a float for the same reason the rest are.
    float bipolar = 0.0f;
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

// One mixer channel's send amounts, one per bus. Every channel that can be
// sent anywhere owns a set of these, so a channel is described by what it is
// rather than by which of several parallel arrays its index falls in.
struct Sends
{
    std::array<float, busCount> amount {};
};

// A bus: where the sends arrive. It has a level and a place in the image like
// any channel, and it goes to the main output or across to the other bus.
//
// It carries no effects yet. That is the whole reason a bus exists in Serum,
// and the reason it exists here is that the routing has to be in place before
// a rack can be hung on it — so the topology lands first and M11 fills it in.
// Until then a bus is a summing point: audibly a gain, but one several sources
// share and one that can be panned and re-routed as a group.
struct Bus
{
    float enable = 1.0f;
    // False for the main output, true for the other bus.
    float dest = 0.0f;
    float pan = 0.0f;
    float level = 0.75f;
};

struct Patch
{
    Oscillator a, b;
    float subEnable = 1.0f, subLevel = 0.17f;
    // Which of the six shapes the sub is reading, and how far below the note it
    // is reading it. Both open where the sub has always stood -- a sine, one
    // octave down -- so a patch written before either existed is unchanged.
    float subWave = 0.0f, subOctave = 0.0f;
    float noiseEnable = 0.0f, noiseLevel = 0.35f;
    // SUB and NOISE are panned with the same equal-power law the oscillators
    // use, so a source reading a given level is that loud whichever source it
    // is. Their levels above carry the 3 dB that law costs at centre, which is
    // why they are not the 0.12 and 0.25 they were before the mixer.
    float subPan = 0.0f, noisePan = 0.0f;
    float filterEnable = 1.0f, filterType = 0.0f;
    // Each source either passes through the filter or bypasses it straight to
    // the voice sum, exactly as Serum's per-source routing buttons work.
    float routeA = 1.0f, routeB = 1.0f, routeSub = 1.0f, routeNoise = 1.0f;
    float cutoff = 7800.0f, resonance = 0.12f, drive = 0.08f;
    // The filter's own channel in the mixer: where its output sits in the
    // image, how much of it is the filtered signal rather than what went in,
    // and how loud the whole channel is. The defaults leave the filter exactly
    // as it behaved before it had a channel.
    float filterPan = 0.0f, filterMix = 1.0f, filterLevel = 1.0f;
    // What each channel sends to each bus, parallel to wherever it is already
    // going.
    Sends sendA, sendB, sendSub, sendNoise, sendFilter;
    std::array<Bus, busCount> buses {};
    // One effects rack on the main output and one on each bus, in that order.
    // They run on the summed voices rather than inside them, which is what an
    // insert after the synth is — see ForgeFxDsp.h.
    std::array<Rack, rackCount> racks {};
    // The four envelopes. ENV 1 is the voice's amplitude and every voice is
    // rendered through it; ENV 2-4 reach anything at all through the matrix.
    std::array<EnvSetting, envCount> envs {};
    // The six LFOs. They are sources, not routers: where one goes is a matter
    // for the modulation slots.
    std::array<LfoSetting, lfoCount> lfos {};
    float polyphony = 8.0f, mono = 0.0f, legato = 1.0f, glide = 0.08f;
    float output = 0.75f;
    // Performance macros. Sources only: a macro is a hand on a knob, and what
    // it reaches is a matter for the matrix.
    std::array<float, macroCount> macros {};
};

inline bool on(float enable) { return enable >= 0.5f; }

// How long a finished voice takes to fade out. The amp envelope reaching zero
// is not the end of a voice: everything it feeds goes through the filter, and a
// filter holds energy, so at a low cutoff the voice is still sounding
// milliseconds after the envelope that fed it stopped. Long enough to cover
// half a cycle of the lowest cutoff the filter offers, so cutting the ring off
// is never a step.
inline constexpr float voiceTailSeconds = 0.015f;


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
        case 16: return &patch.subPan;
        case 17: return &patch.noisePan;
        case 18: return &patch.filterPan;
        case 19: return &patch.filterMix;
        case 20: return &patch.filterLevel;
        default: break;
    }
    // Past the racks are the four warp depths, which are named rather than
    // generated and so are read back the same way.
    const auto warp = destination - warpDestinationBase;
    if (warp >= 0 && warp < warpDestinationCount)
    {
        auto& osc = warp < warpSlots ? patch.a : patch.b;
        return &osc.warpAmount[static_cast<size_t>(warp % warpSlots)];
    }

    // The first four rack slots occupy their historic range. Slots five to
    // eight live after the warp block so adding them cannot move an existing
    // destination stored by index in an older preset.
    auto fx = destination - fxDestinationBase;
    auto slotsPerRack = legacyFxSlotCount;
    auto slotOffset = 0;
    if (fx < 0 || fx >= legacyFxDestinationCount)
    {
        fx = destination - extendedFxDestinationBase;
        slotsPerRack = extendedFxSlotCount;
        slotOffset = legacyFxSlotCount;
        if (fx < 0 || fx >= extendedFxDestinationCount) return nullptr;
    }
    const auto control = fx % fxDestinationsPerSlot;
    const auto slot = (fx / fxDestinationsPerSlot) % slotsPerRack + slotOffset;
    const auto rack = fx / (fxDestinationsPerSlot * slotsPerRack);
    auto& held = patch.racks[static_cast<size_t>(rack)].slots[static_cast<size_t>(slot)];
    return control < fxKnobCount ? &held.knobs[static_cast<size_t>(control)] : &held.mix;
}

// Taken as a bare value as well as from a patch, because the panel draws the
// filter's response from the parameter and has to land on the same tap the
// engine will run.
inline FilterType filterTypeOf(float filterType)
{
    return static_cast<FilterType>(juce::jlimit(0, 2, juce::roundToInt(filterType)));
}

inline FilterType filterTypeOf(const Patch& patch) { return filterTypeOf(patch.filterType); }

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

// --- Panning ------------------------------------------------------------------
//
// Two laws, because a pan means two different things in a mixer.
//
// A *source* pan spreads one source among the others. It is the law the unison
// stack already spreads across, and it holds the source's power constant as it
// moves — at the cost of 3 dB against the mono sum, which is why a centred
// source reads 0.707 rather than 1.
//
// A *channel* pan moves a sum that is already balanced: the filter's output, or
// a bus. Costing that 3 dB would mean placing a channel in the centre quietly
// turned it down, so the channel law is the same curve normalised to unity at
// centre instead. Both hold left-squared plus right-squared constant; they
// differ only in where that constant sits.
inline float sourcePanLeft(float pan)
{
    return std::sqrt(0.5f * (1.0f - juce::jlimit(-1.0f, 1.0f, pan)));
}

inline float sourcePanRight(float pan)
{
    return std::sqrt(0.5f * (1.0f + juce::jlimit(-1.0f, 1.0f, pan)));
}

inline float channelPanLeft(float pan)
{
    return std::sqrt(1.0f - juce::jlimit(-1.0f, 1.0f, pan));
}

inline float channelPanRight(float pan)
{
    return std::sqrt(1.0f + juce::jlimit(-1.0f, 1.0f, pan));
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
        tailStep = static_cast<float>(1.0 / (sampleRate * voiceTailSeconds));
        dcBlock = warpDcCoefficient(sampleRate);
        // Touched here so the tables are built on whichever thread prepares
        // the synth, never lazily on the first note from the audio thread.
        builtInWavetable();
        subWavetable();
        // Every rack's delay lines are sized here, which is the one place they
        // may be: a slot's type changes while audio is running, so each slot
        // carries every type's state and none of it can be built on demand.
        for (auto& rack : racks) rack.prepare(sampleRate);
        reset();
    }

    void reset()
    {
        voices = {};
        nextVoice = 0;
        freePhase = {};
        freeHeld = {};
        meterLfoPhase = {};
        meterLfoHeld = {};
        meterLfoValue = {};
        noiseState = 0x9e3779b9u;
        heldCount = 0;
        monoMode = false;
        meterEnvelope = {};
        meterStage = {};
        meterOffsets = {};
        for (auto& rack : racks) rack.reset();
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
            {
                startVoice(voice, note, velocity);
                retriggerLfo(voice, patch);
            }
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
        auto& voice = voices[allocate(voiceCount)];
        startVoice(voice, note, velocity);
        retriggerLfo(voice, patch);
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
        // Every envelope is released by the key that started it, the auxiliary
        // ones included: an envelope that only fell when the amp did would be a
        // second shape with no release of its own.
        for (auto& voice : voices)
            if (voice.active && voice.note == note && voice.envStage[ampEnv] != EnvelopeStage::release)
                for (int env = 0; env < envCount; ++env)
                {
                    const auto i = static_cast<size_t>(env);
                    voice.envStage[i] = EnvelopeStage::release;
                    voice.envReleaseStart[i] = voice.envelope[i];
                }
    }

    void allNotesOff() { reset(); }

    // What an envelope is doing, for the display to draw. Taken from the
    // loudest sounding voice, which is the one a player is listening to — the
    // loudest by ENV 1, so all four readings come from one voice rather than
    // each from whichever voice happens to have that envelope highest. Plain
    // members rather than atomics: Core stays a pure DSP class and the
    // processor owns the hand-off to the message thread.
    float envelopeLevel(int env = ampEnv) const
    {
        return env >= 0 && env < envCount ? meterEnvelope[static_cast<size_t>(env)] : 0.0f;
    }

    int envelopeStage(int env = ampEnv) const
    {
        return env >= 0 && env < envCount
            ? static_cast<int>(meterStage[static_cast<size_t>(env)]) : 0;
    }

    // Where LFO 1 is in its cycle and what it last put out. The phase draws the
    // running indicator; the value is published as well because a
    // sample-and-hold's step cannot be worked back out of the phase.
    float lfoPosition(int lfo) const
    {
        return lfo >= 0 && lfo < lfoCount ? meterLfoPhase[static_cast<size_t>(lfo)] : 0.0f;
    }

    float lfoOutput(int lfo) const
    {
        return lfo >= 0 && lfo < lfoCount ? meterLfoValue[static_cast<size_t>(lfo)] : 0.0f;
    }

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

    // The tempo a synced delay or chorus divides. Set by the Processor once per
    // block, the same reading a synced LFO is resolved against — except that an
    // LFO is resolved before the patch is built and a rack is read from it, so
    // the rack needs the tempo itself rather than a rate worked out from it.
    void setTempo(double bpm) { tempo = bpm; }

    void renderSample(const Patch& patch, const Modulation& modulation, float& left, float& right)
    {
        left = right = 0.0f;

        // Which LFOs anything actually reads. Six of them stepping through a
        // sine for every voice would be five sixths of that work thrown away
        // when one is routed, so the values are only worked out where they are
        // wanted. The phases still move either way — an LFO nothing is pointed
        // at yet is still one the panel draws running.
        std::array<bool, lfoCount> used {};
        for (const auto& slot : modulation.slots)
        {
            if (slot.destination < 0.5f || slot.depth == 0.0f) continue;
            const auto lfo = lfoIndexOf(juce::roundToInt(slot.source));
            if (lfo >= 0) used[static_cast<size_t>(lfo)] = true;
        }

        // The free-running cycles. An LFO in OFF reads these, and they run
        // whether or not a note is playing, which is the whole of what OFF
        // means. One cycle shared by every voice, so a rate set in beats stays
        // in step with the host across a phrase.
        std::array<float, lfoCount> freeValue {};
        for (int i = 0; i < lfoCount; ++i)
        {
            const auto index = static_cast<size_t>(i);
            const auto& setting = patch.lfos[index];
            const auto free = lfoModeOf(setting) == LfoMode::free;
            if (free && used[index])
                freeValue[index] = lfoWave(lfoShapeOf(setting), freePhase[index], freeHeld[index]);

            // What the panel is shown, before any voice has had its say: the
            // free-running phase for an LFO in OFF, and the start of the shape
            // for one that answers the keyboard — which is exactly where it is
            // with nothing playing, and where the next key press will begin it.
            meterLfoPhase[index] = free ? freePhase[index] : 0.0f;
            meterLfoHeld[index] = free ? freeHeld[index] : 0.0f;

            const auto advanced = freePhase[index]
                + juce::jlimit(0.01f, 40.0f, setting.rate) / static_cast<float>(sampleRate);
            if (advanced >= 1.0f) freeHeld[index] = noise();
            freePhase[index] = wrap(advanced);
        }

        const auto modulated = modulation.anyActive();
        // Whether anything is pointed at a rack at all. A rack runs once on the
        // summed voices, so a per-voice source reaching one has to be resolved
        // to a single voice's value — and carrying a copy of every rack out of
        // the loop to do that is only worth it when something actually is.
        auto fxModulated = false;
        if (modulated)
            for (const auto& slot : modulation.slots)
                if (slot.depth != 0.0f && slot.source >= 0.5f
                    && juce::roundToInt(slot.destination) >= fxDestinationBase)
                    fxModulated = true;
        const Patch* fxPatch = &patch;

        meterEnvelope = {};
        meterStage = {};
        meterOffsets = {};

        // What every voice has sent to each bus. Filled inside the loop and
        // resolved once after it.
        std::array<float, busCount> busLeft {}, busRight {};

        for (auto& voice : voices)
        {
            if (!voice.active) continue;
            // All four, whether or not anything reads them. An envelope's value
            // is its state rather than something worked out from a phase, so
            // one skipped while nothing points at it would come back wrong the
            // moment something did.
            for (int env = 0; env < envCount; ++env)
            {
                const auto i = static_cast<size_t>(env);
                const auto& shape = patch.envs[i];
                updateEnvelope(voice.envelope[i], voice.envStage[i], voice.envReleaseStart[i],
                               shape.attack, shape.decay, shape.sustain, shape.release);
            }
            // A voice whose envelope has run out is not finished: the filter
            // it fed is still ringing, and at a low cutoff that ring is loud
            // enough to hear. Dropping the voice here truncates it, and a
            // truncated ring is the click at the end of a note — loudest with
            // the sub on, because a sine an octave down is exactly what a low
            // cutoff passes. So it is faded out instead, and the ring decays
            // into the fade.
            if (voice.envStage[ampEnv] == EnvelopeStage::idle)
            {
                voice.tail -= tailStep;
                if (voice.tail <= 0.0f)
                {
                    voice.active = false;
                    continue;
                }
            }
            const auto loudest = voice.envelope[ampEnv] >= meterEnvelope[ampEnv];
            if (loudest)
            {
                meterEnvelope = voice.envelope;
                meterStage = voice.envStage;
            }

            // Every LFO that answers the keyboard runs inside the voice, so a
            // new note restarts its own shape and leaves the notes already
            // sounding where they were. An LFO in OFF is the one cycle
            // everything shares, and the voice simply reads it.
            std::array<float, lfoCount> lfoValues {};
            for (int i = 0; i < lfoCount; ++i)
            {
                const auto index = static_cast<size_t>(i);
                const auto& setting = patch.lfos[index];
                const auto mode = lfoModeOf(setting);
                if (mode == LfoMode::free)
                {
                    lfoValues[index] = freeValue[index];
                    continue;
                }
                if (used[index])
                    lfoValues[index] = lfoWave(lfoShapeOf(setting),
                                               voice.lfoPhase[index], voice.lfoHeld[index]);
                // The panel follows the loudest voice, exactly as ENV 1's
                // display does and for the same reason: that is the note a
                // player is listening to.
                if (loudest)
                {
                    meterLfoPhase[index] = voice.lfoPhase[index];
                    meterLfoHeld[index] = voice.lfoHeld[index];
                }
                advanceVoiceLfo(voice, i, setting, mode);
            }

            // Modulation is per voice and per sample: every source is per voice
            // now that the LFOs are, and every destination is read inside the
            // voice. With no live slot the patch is used as it stands and
            // nothing is copied.
            const Patch* voicePatch = &patch;
            if (modulated)
            {
                scratch = patch;
                applyModulation(scratch, modulation, voice, lfoValues, loudest);
                voicePatch = &scratch;
            }
            const auto& active = *voicePatch;

            // The loudest voice is the one every display already follows, and
            // it is the one a rack follows too: one process fed by every note
            // cannot have a value per note.
            if (fxModulated && loudest) { fxScratch.racks = active.racks; fxPatch = &fxScratch; }

            Buses buses;
            renderOscillators(voice, active, buses);

            // Drive belongs to the filter, so only what is routed into it is
            // driven, and switching the module off bypasses the drive with it.
            // The filter state keeps running either way, so switching the
            // module or a route back on does not click.
            const auto inputLeft = buses.wetLeft, inputRight = buses.wetRight;
            auto routedLeft = inputLeft, routedRight = inputRight;
            const auto type = filterTypeOf(active);
            if (on(active.filterEnable))
            {
                routedLeft = filter(saturate(routedLeft, active.drive),
                                    voice.lowLeft, voice.bandLeft, active.cutoff, active.resonance, type);
                routedRight = filter(saturate(routedRight, active.drive),
                                     voice.lowRight, voice.bandRight, active.cutoff, active.resonance, type);
                // MIX blends what came out against what went in. With the
                // module switched off there is nothing to blend — the two are
                // the same signal — so the knob is skipped rather than applied
                // to a pair of identical values.
                const auto mix = juce::jlimit(0.0f, 1.0f, active.filterMix);
                routedLeft = routedLeft * mix + inputLeft * (1.0f - mix);
                routedRight = routedRight * mix + inputRight * (1.0f - mix);
            }
            else
            {
                filter(routedLeft, voice.lowLeft, voice.bandLeft, active.cutoff, active.resonance, type);
                filter(routedRight, voice.lowRight, voice.bandRight, active.cutoff, active.resonance, type);
            }

            // The rest of the filter's channel: its place in the image and its
            // own fader, then its sends, which are taken after that fader
            // exactly as every other channel's are.
            const auto filterGain = juce::jlimit(0.0f, 1.0f, active.filterLevel);
            routedLeft *= filterGain * channelPanLeft(active.filterPan);
            routedRight *= filterGain * channelPanRight(active.filterPan);
            send(routedLeft, routedRight, active.sendFilter, buses);

            left += (routedLeft + buses.dryLeft) * voice.tail;
            right += (routedRight + buses.dryRight) * voice.tail;
            // The busses are one sum across every voice rather than one per
            // voice. It makes no difference to a gain, and it is what an
            // effects rack will need when one arrives: a reverb on a bus is a
            // single tail fed by every note, not a copy per note.
            for (int bus = 0; bus < busCount; ++bus)
            {
                const auto index = static_cast<size_t>(bus);
                busLeft[index] += buses.sendLeft[index] * voice.tail;
                busRight[index] += buses.sendRight[index] * voice.tail;
            }
        }

        resolveBuses(*fxPatch, busLeft, busRight, left, right, racks, tempo);

        // Everything that reached the main output, through the main rack, and
        // only then through the master level — which is the order Serum states:
        // audio routed to MAIN passes the modules, and then the master volume.
        racks[0].process(fxPatch->racks[0], tempo, left, right);

        // The values the panel draws, worked out once from the phases the loop
        // settled on rather than per voice: a sample and hold's step cannot be
        // read back out of its phase, so it has to be carried this far.
        for (int i = 0; i < lfoCount; ++i)
            meterLfoValue[static_cast<size_t>(i)] =
                lfoWave(lfoShapeOf(patch.lfos[static_cast<size_t>(i)]),
                        meterLfoPhase[static_cast<size_t>(i)], meterLfoHeld[static_cast<size_t>(i)]);

        const auto gain = juce::jlimit(0.0f, 1.25f, patch.output) * 0.28f;
        left = softClip(left * gain);
        right = softClip(right * gain);
    }

private:
    // Each bus given its level and its place, then handed to wherever it goes.
    //
    // A bus pointed at the other is folded in first, so the one being fed is
    // resolved last and arrives at the output carrying both. Two busses pointed
    // at each other is a loop with no answer; the second of the pair goes to the
    // main output instead, which is a setting the panel then never has to
    // refuse. Written for two busses, because "the other bus" is only a thing
    // there are two of.
    static void resolveBuses(const Patch& patch, std::array<float, busCount>& busLeft,
                             std::array<float, busCount>& busRight, float& left, float& right,
                             std::array<FxRack, rackCount>& racks, double tempo)
    {
        static_assert(busCount == 2, "resolveBuses routes a bus to 'the other one'");
        const auto crossed = [&patch] (int bus)
        {
            const auto& settings = patch.buses[static_cast<size_t>(bus)];
            return on(settings.enable) && on(settings.dest);
        };

        // A bus feeding the other is resolved first, whichever of the two it is.
        std::array<int, busCount> order {0, 1};
        if (crossed(1) && !crossed(0)) order = {1, 0};

        for (int i = 0; i < busCount; ++i)
        {
            const auto bus = order[static_cast<size_t>(i)];
            const auto index = static_cast<size_t>(bus);
            const auto& settings = patch.buses[index];
            if (!on(settings.enable)) continue;

            // A bus's rack sits between what arrived and the bus's own fader,
            // so the fader sets how much of the processed signal is heard
            // rather than how hard the rack is driven. Rack 0 is the main
            // output's, so bus n uses rack n + 1.
            racks[static_cast<size_t>(bus + 1)].process(patch.racks[static_cast<size_t>(bus + 1)],
                                                        tempo, busLeft[index], busRight[index]);

            const auto gain = juce::jlimit(0.0f, 1.0f, settings.level);
            const auto outLeft = busLeft[index] * gain * channelPanLeft(settings.pan);
            const auto outRight = busRight[index] * gain * channelPanRight(settings.pan);

            const auto other = static_cast<size_t>(1 - bus);
            // The second of a mutually crossed pair: its destination would be
            // the bus that has already been folded into it.
            const auto loop = i == busCount - 1 && crossed(1 - bus);
            if (on(settings.dest) && !loop)
            {
                busLeft[other] += outLeft;
                busRight[other] += outRight;
            }
            else
            {
                left += outLeft;
                right += outRight;
            }
        }
    }

    enum class EnvelopeStage { idle, attack, decay, sustain, release };

    // What a voice accumulates into: the sources routed through the filter, the
    // sources that bypass it, and what every channel has sent to each bus.
    //
    // A send is parallel to wherever the channel is already going, so a source
    // appears in one of the first two pairs and in as many of the send pairs as
    // it is sent to.
    struct Buses
    {
        float wetLeft = 0.0f, wetRight = 0.0f, dryLeft = 0.0f, dryRight = 0.0f;
        std::array<float, busCount> sendLeft {}, sendRight {};
    };

    // One channel's signal handed to the destination it names and to whichever
    // busses it is sent to. The routing decision is made here, once, rather
    // than being threaded through everything downstream.
    static void distribute(float left, float right, bool throughFilter,
                           const Sends& sends, Buses& buses)
    {
        (throughFilter ? buses.wetLeft : buses.dryLeft) += left;
        (throughFilter ? buses.wetRight : buses.dryRight) += right;
        send(left, right, sends, buses);
    }

    static void send(float left, float right, const Sends& sends, Buses& buses)
    {
        for (int bus = 0; bus < busCount; ++bus)
        {
            const auto index = static_cast<size_t>(bus);
            const auto amount = juce::jlimit(0.0f, 1.0f, sends.amount[index]);
            if (amount <= 0.0f) continue;
            buses.sendLeft[index] += left * amount;
            buses.sendRight[index] += right * amount;
        }
    }

    struct Voice
    {
        bool active = false;
        int note = 0;
        float velocity = 0.0f;
        std::array<float, unisonMax> phaseA {}, phaseB {};
        // What each oscillator's warp stages are holding on to, one set per
        // member of the stack: the filter modes' state and FM SELF's last
        // output. Per member rather than per oscillator because every member is
        // reading the table at a phase of its own — one filter shared by twelve
        // detuned copies would be a filter fed twelve different signals.
        std::array<std::array<WarpState, warpSlots>, unisonMax> warpA {}, warpB {};
        // What an asymmetric warp leaves behind, taken off each oscillator's
        // output rather than off every member of its stack: the offset is the
        // same in all of them, so blocking it once after the sum is the same
        // answer for a twelfth of the work.
        std::array<WarpDcBlocker, 2> dcA {}, dcB {};
        float phaseSub = 0.0f;
        float currentHz = 0.0f, targetHz = 0.0f;
        // ENV 1 at index zero is the amplitude this voice is rendered at and
        // the one that says when it is finished; ENV 2-4 are carried for the
        // matrix to read and affect nothing on their own.
        std::array<float, envCount> envelope {}, envReleaseStart {};
        // Full until the envelope has finished, then run down to nothing so
        // whatever the filter is still ringing with is let go of rather than
        // cut off. It is also what guarantees the voice comes back: a filter
        // pushed hard would otherwise ring for a long time.
        float tail = 1.0f;
        float lowLeft = 0.0f, bandLeft = 0.0f, lowRight = 0.0f, bandRight = 0.0f;
        std::array<EnvelopeStage, envCount> envStage {};
        // Every LFO that answers the keyboard runs a copy of itself inside each
        // voice, which is what makes TRIG and ENV mean anything: a new note
        // restarts its own shape and leaves the notes already sounding alone.
        // An LFO in OFF ignores these and reads the free-running phase instead.
        std::array<float, lfoCount> lfoPhase {};
        std::array<float, lfoCount> lfoHeld {};
        std::array<bool, lfoCount> lfoStopped {};
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
                         const std::array<float, lfoCount>& lfos, bool publish = false)
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
                case ModSource::velocity: amount = voice.velocity; break;
                case ModSource::note:     amount = static_cast<float>(voice.note) / 127.0f; break;
                case ModSource::off:      continue;
                default:
                {
                    if (const auto env = envIndexOf(source); env >= 0)
                    {
                        amount = voice.envelope[static_cast<size_t>(env)];
                        break;
                    }
                    if (const auto lfo = lfoIndexOf(source); lfo >= 0)
                    {
                        amount = lfos[static_cast<size_t>(lfo)];
                        break;
                    }
                    const auto macro = macroIndexOf(source);
                    if (macro < 0) continue;
                    amount = target.macros[static_cast<size_t>(macro)];
                    break;
                }
            }

            // BI centres the source, so the destination's own setting becomes
            // the middle of what the slot can reach rather than one end of it:
            // a macro at rest pulls it as far negative as the depth goes, a
            // macro at half leaves it alone, and a macro at the top pushes it
            // as far positive. Serum calls this POL, and it is the difference
            // between a source at zero meaning "no modulation" and meaning "as
            // far negative as this reaches" — a whole octave on a pitch
            // destination at full depth, and easy to mistake for the oscillator
            // being mistuned.
            //
            // Half, not double: this recentres the reach without resizing it,
            // so one depth still spans the destination exactly once and the top
            // of the control is still the top of the control. Scaling to plus
            // and minus the depth instead would put everything past halfway
            // into the clamp, where turning the knob further did nothing.
            if (slot.bipolar >= 0.5f && !sourceIsBipolar(source)) amount -= 0.5f;

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

    // Which voice a new note takes. A silent one if there is one, and the
    // rotation keeps moving through them so that successive notes do not all
    // land on the same slot and start from the same phases.
    //
    // Past that, the cheapest voice to interrupt. A voice already in its
    // release is the one to take: the key is up and the player has moved on, so
    // the quietest of those is the one least likely still to be heard. Only
    // when every voice is still held does a note that is still being played have
    // to give way, and then the quietest of those. Strict rotation took
    // whichever voice was next whether or not it was busy, so a run of notes
    // longer than the polyphony cut off whatever it landed on.
    size_t allocate(size_t voiceCount)
    {
        for (size_t i = 0; i < voiceCount; ++i)
        {
            const auto index = (nextVoice + i) % voiceCount;
            if (!voices[index].active) return took(index);
        }

        const auto quietest = [this, voiceCount] (bool releasing)
        {
            auto best = voiceCount;
            for (size_t i = 0; i < voiceCount; ++i)
            {
                if ((voices[i].envStage[ampEnv] == EnvelopeStage::release) != releasing) continue;
                if (best == voiceCount
                    || voices[i].envelope[ampEnv] < voices[best].envelope[ampEnv]) best = i;
            }
            return best;
        };
        const auto releasing = quietest(true);
        const auto chosen = releasing < voiceCount ? releasing : quietest(false);
        // Every voice was active to have reached this far, so one of the two
        // searches found one; the fallback is there to keep the index in range
        // rather than because it can be taken.
        return took(chosen < voiceCount ? chosen : 0);
    }

    size_t took(size_t index)
    {
        nextVoice = index + 1;
        return index;
    }

    // A new note starts every keyboard LFO again from the top — this voice's
    // copies of them, so the notes already sounding are untouched. OFF
    // deliberately does not restart, which is the whole point of it: a
    // free-running LFO keeps its place across a phrase.
    //
    // A legato note in mono is not a new note here, for exactly the reason it
    // does not restart the amp envelope — no key was lifted — so an LFO
    // retriggers precisely when that envelope does.
    void retriggerLfo(Voice& voice, const Patch& patch)
    {
        for (int i = 0; i < lfoCount; ++i)
        {
            if (lfoModeOf(patch.lfos[static_cast<size_t>(i)]) == LfoMode::free) continue;
            voice.lfoPhase[static_cast<size_t>(i)] = 0.0f;
            // A fresh step with it: phase zero is where sample and hold takes one.
            voice.lfoHeld[static_cast<size_t>(i)] = noise();
            voice.lfoStopped[static_cast<size_t>(i)] = false;
        }
    }

    // One LFO's cycle moved on by a sample, inside one voice.
    void advanceVoiceLfo(Voice& voice, int index, const LfoSetting& setting, LfoMode mode)
    {
        const auto i = static_cast<size_t>(index);
        // Parked at the end of a one-shot until a note restarts it — or until
        // the mode is taken off ENV, which lets the shape run on from where it
        // stopped rather than leaving the panel showing a dead indicator.
        if (voice.lfoStopped[i] && mode != LfoMode::envelope) voice.lfoStopped[i] = false;
        if (voice.lfoStopped[i]) return;

        const auto advanced = voice.lfoPhase[i]
            + juce::jlimit(0.01f, 40.0f, setting.rate) / static_cast<float>(sampleRate);
        if (advanced >= 1.0f && mode == LfoMode::envelope)
        {
            // A one-shot stops on the last point of the shape and holds it,
            // rather than wrapping round to the first. That hold is what makes
            // it an envelope instead of a cycle that ran once: a saw finishes at
            // the top, a triangle at the bottom, and whatever it is driving
            // stays there until the next note.
            voice.lfoPhase[i] = 1.0f;
            voice.lfoStopped[i] = true;
            return;
        }
        // One new step per cycle, taken as the cycle turns over, so a
        // sample-and-hold changes exactly where the other shapes restart.
        if (advanced >= 1.0f) voice.lfoHeld[i] = noise();
        voice.lfoPhase[i] = wrap(advanced);
    }

    // Taking a voice that is still sounding must not be audible as anything but
    // the new note arriving. Wiping it — phases, filter state and envelope all
    // back to zero — steps the output straight down to silence in one sample,
    // and that step is the click a player hears when a run of notes is longer
    // than the polyphony.
    //
    // So a voice that is still audible is retuned rather than rebuilt. It keeps
    // its oscillator phases, its filter state and the level its envelope has
    // reached, and the attack simply starts again from that level. Amplitude,
    // waveform and filter are all continuous across the steal; the pitch jumps,
    // and a pitch jump is a new note rather than a click.
    void startVoice(Voice& voice, int note, float velocity)
    {
        const auto sounding = voice.active;
        if (!sounding)
        {
            voice = {};
            // A silent voice starts its unison stack at offsets of its own, so
            // two notes struck together do not begin life as one louder note.
            // The offsets are hashed rather than stepped along by a constant:
            // a constant step is a comb, and a harmonic high enough to see the
            // teeth line up on it, which leaves the stack correlated exactly
            // where it should sound widest. A hash has no such structure, and
            // being a hash rather than a random number it is still the same on
            // every run, which is what lets the tests measure it.
            for (juce::uint32 i = 0; i < voice.phaseA.size(); ++i)
            {
                const auto seed = i * 2654435761u + static_cast<juce::uint32>(note) * 40503u;
                voice.phaseA[i] = unitFromHash(seed);
                voice.phaseB[i] = unitFromHash(seed + 2654435741u);
            }
        }
        voice.active = true;
        voice.tail = 1.0f;
        voice.note = note;
        voice.velocity = juce::jlimit(0.0f, 1.0f, velocity);
        voice.currentHz = voice.targetHz = noteFrequency(note);
        // Every envelope starts again, from wherever it had reached: a stolen
        // voice keeps the levels it was at and climbs from them, so all four
        // are continuous across the steal exactly as ENV 1 is.
        for (auto& stage : voice.envStage) stage = EnvelopeStage::attack;
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
    void renderOscillator(std::array<float, unisonMax>& phases,
                          std::array<std::array<WarpState, warpSlots>, unisonMax>& warpStates,
                          std::array<WarpDcBlocker, 2>& dc, const Oscillator& osc, float baseHz,
                          float dt, const std::array<WarpStage, warpSlots>& warp,
                          float& left, float& right) const
    {
        if (!on(osc.enable)) return;
        const auto count = juce::jlimit(1, static_cast<int>(phases.size()), juce::roundToInt(osc.unison));
        const auto position = juce::jlimit(0.0f, 1.0f, osc.position);
        const auto hz = baseHz * tuningRatio(osc);
        const auto detune = juce::jlimit(0.0f, 1.0f, osc.detune);
        const auto blend = juce::jlimit(0.0f, 1.0f, osc.blend);

        // Which band-limited copy of the table this note may read. Chosen from
        // the top of the unison stack rather than its centre, so the sharpest
        // voice in the stack decides and no member of it aliases: detune lifts
        // the top of the stack by at most half of unisonSpreadSemitones, which
        // is a ratio of 1.0413, and 5% of headroom covers that with room to
        // spare. Widening the spread without widening this would let the
        // sharpest voice read a copy that is not band-limited far enough.
        //
        // A warp reads the table somewhere other than where the phase says, or
        // shapes what it finds there, and either makes harmonics the table did
        // not hold. So the copy is chosen for a note that much higher than the
        // one being played: every mode declares how much extra bandwidth it is
        // about to ask for, and the two stages multiply. See ForgeWarp.h.
        const auto& table = osc.table != nullptr ? *osc.table : builtInWavetable();
        const auto warped = warp[0].mode != WarpMode::off || warp[1].mode != WarpMode::off;
        const auto headroom = warped
            ? juce::jlimit(1.0f, warpHeadroomCeiling, warpHeadroom(warp[0]) * warpHeadroom(warp[1]))
            : 1.0f;
        const auto level = table.levelFor(hz * 1.05f * headroom, sampleRate);
        // What FM is doing to the rate the cycle runs at, this sample. One
        // unless a stage is actually modulating the frequency, so an
        // oscillator that is not being frequency-modulated advances exactly as
        // it always did. It is worked out once for the whole stack: every
        // member of it is reading one modulator, and a stack that bent by
        // different amounts would no longer be one oscillator.
        const auto pitch = warped
            ? warpPitchFactor(warp[0].mode, warp[0].amount, warp[0].modulator)
            * warpPitchFactor(warp[1].mode, warp[1].amount, warp[1].modulator)
            : 1.0f;

        auto stackLeft = 0.0f, stackRight = 0.0f, power = 0.0f;
        for (int i = 0; i < count; ++i)
        {
            const auto spread = count == 1 ? 0.0f
                : static_cast<float>(i) / static_cast<float>(count - 1) - 0.5f;
            // Blend and pan read the even position, so the shape of the stack
            // across the gain curve and across the image stays smooth. Only the
            // tuning reads the uneven one, because that is what beats.
            const auto offset = unisonOffset(i, count);
            // Blend balances the centre of the stack against its edges: at 0
            // only the centre voices are heard, at 1 the whole stack is level.
            const auto centreWeight = 1.0f - juce::jmin(1.0f, std::abs(spread) * 2.0f);
            const auto gain = juce::jmap(blend, centreWeight, 1.0f);
            power += gain * gain;

            // The table read, and the two warp stages standing between it and
            // the voice. They chain by one calling the other rather than
            // through a buffer, so a stage that moves the phase moves what the
            // stage in front of it is reading rather than what it already read.
            const auto readTable = [&table, level, position] (float p)
            { return table.sample(level, position, p); };
            auto& states = warpStates[static_cast<size_t>(i)];
            const auto phase = phases[static_cast<size_t>(i)];
            const auto sample = (warped
                ? warpRead(warp[1], phase, states[1], [&] (float p)
                           { return warpRead(warp[0], p, states[0], readTable); })
                : readTable(phase)) * gain;
            const auto pan = juce::jlimit(-1.0f, 1.0f, osc.pan + spread * detune * 1.6f);
            stackLeft += sample * sourcePanLeft(pan);
            stackRight += sample * sourcePanRight(pan);

            const auto ratio = std::pow(2.0f, offset * detune
                                              * unisonSpreadSemitones / 12.0f);
            phases[static_cast<size_t>(i)] =
                wrap(phases[static_cast<size_t>(i)] + hz * ratio * pitch * dt);
        }

        // Power normalisation, so widening the stack changes the sound without
        // changing how loud the oscillator is.
        const auto scale = juce::jlimit(0.0f, 1.0f, osc.level) / std::sqrt(std::max(0.0001f, power));
        auto outLeft = stackLeft * scale, outRight = stackRight * scale;
        // An asymmetric warp puts a constant offset into the signal, which is a
        // thump on every note and a bias the filter would then have to carry.
        // Only while something is warping: an oscillator reading its table
        // straight has no offset to take off.
        if (warped)
        {
            outLeft = dc[0].process(outLeft, dcBlock);
            outRight = dc[1].process(outRight, dcBlock);
        }
        left += outLeft;
        right += outRight;
    }

    void renderOscillators(Voice& voice, const Patch& patch, Buses& buses)
    {
        const auto dt = static_cast<float>(1.0 / sampleRate);
        const auto glide = juce::jlimit(0.0f, 2.0f, patch.glide);
        if (glide <= 0.0001f) voice.currentHz = voice.targetHz;
        else voice.currentHz += (voice.targetHz - voice.currentHz)
            * (1.0f - std::exp(-1.0f / (static_cast<float>(sampleRate) * glide)));
        const auto hz = voice.currentHz;

        // Every source is rendered on its own before it is handed anywhere,
        // because a channel's sends are taken from that channel rather than
        // from the sum it lands in: an oscillator can reach the filter and both
        // busses at once, and it cannot do that while it is being written
        // straight into somebody else's accumulator.
        // Each oscillator's two warp stages, resolved for this sample before
        // either stack is touched: which mode, how deep, the coefficients a
        // filter mode needs, and whatever an FM mode is reading. Worked out
        // once here rather than once per member of a stack, which is what keeps
        // the exponentials out of the inner loop.
        const auto hzA = hz * tuningRatio(patch.a), hzB = hz * tuningRatio(patch.b);
        std::array<WarpStage, warpSlots> warpA {}, warpB {};
        for (int i = 0; i < warpSlots; ++i)
        {
            const auto slot = static_cast<size_t>(i);
            warpA[slot] = warpStageFor(patch.a.warpMode[slot], patch.a.warpAmount[slot], hzA, sampleRate);
            warpB[slot] = warpStageFor(patch.b.warpMode[slot], patch.b.warpAmount[slot], hzB, sampleRate);
        }

        // What FM reads, worked out only where something is actually asking for
        // it. The two oscillators read each other at the phases they both stand
        // at now, before either has advanced, so neither is a sample ahead of
        // the other and swapping which one is rendered first changes nothing.
        const auto asks = [] (const std::array<WarpStage, warpSlots>& warp, bool (*test)(WarpMode))
        {
            for (const auto& stage : warp) if (test(stage.mode)) return true;
            return false;
        };
        const auto wantsOther = asks(warpA, warpReadsOtherOscillator) || asks(warpB, warpReadsOtherOscillator);
        const auto wantsSub = asks(warpA, warpReadsSub) || asks(warpB, warpReadsSub);
        const auto wantsNoise = asks(warpA, warpReadsNoise) || asks(warpB, warpReadsNoise);
        // The sub, read once for whoever needs it: the source itself below, and
        // any warp stage pointed at it. Both read the same shape at the same
        // band limit, because a stage reading FM SUB is reading the sub rather
        // than a sine that happens to stand where it does.
        const auto hzSub = hz * subRatio(patch.subOctave);
        const auto& subTable = subWavetable();
        const auto subBand = subTable.levelFor(hzSub, sampleRate);
        const auto subShapeIndex = subShapeOf(patch.subWave);
        const auto subSample = [&] { return subTable.frameSample(subBand, subShapeIndex, voice.phaseSub); };
        const auto fromSub = wantsSub ? subSample() : 0.0f;
        const auto fromNoise = wantsNoise ? noise() : 0.0f;
        // The centre of the other oscillator's stack, not the whole of it: a
        // modulator is one signal, and twelve detuned copies of one would cost
        // twelve table reads to say the same thing.
        const auto centre = [this] (const Oscillator& osc, const std::array<float, unisonMax>& phases,
                                    float oscHz)
        {
            if (!on(osc.enable)) return 0.0f;
            const auto& table = osc.table != nullptr ? *osc.table : builtInWavetable();
            return table.sample(table.levelFor(oscHz, sampleRate),
                                juce::jlimit(0.0f, 1.0f, osc.position), phases[0]);
        };
        const auto fromB = wantsOther ? centre(patch.b, voice.phaseB, hzB) : 0.0f;
        const auto fromA = wantsOther ? centre(patch.a, voice.phaseA, hzA) : 0.0f;
        // A stage pointed at a source that is switched off is not a stage. The
        // manual says as much -- the other oscillator has to be enabled for FM
        // to work, though its level may be all the way down -- and saying it
        // here rather than letting the modulator come out at zero matters,
        // because a live stage also asks the table for bandwidth it is not
        // going to use, and the carrier would quietly go dull for nothing.
        const auto pointAt = [&] (std::array<WarpStage, warpSlots>& warp, float other, bool otherOn)
        {
            for (auto& stage : warp)
                switch (warpSourceOf(stage.mode))
                {
                    case WarpSource::otherOscillator:
                        if (otherOn) stage.modulator = other; else stage = {};
                        break;
                    case WarpSource::sub:
                        if (on(patch.subEnable)) stage.modulator = fromSub; else stage = {};
                        break;
                    case WarpSource::noise: stage.modulator = fromNoise; break;
                    // A stage reading itself needs nothing from out here; it
                    // keeps its own last output beside its filter state.
                    case WarpSource::self:
                    case WarpSource::none:  break;
                }
        };
        pointAt(warpA, fromB, on(patch.b.enable));
        pointAt(warpB, fromA, on(patch.a.enable));

        auto left = 0.0f, right = 0.0f;
        renderOscillator(voice.phaseA, voice.warpA, voice.dcA, patch.a, hz, dt, warpA, left, right);
        distribute(left, right, on(patch.routeA), patch.sendA, buses);

        left = right = 0.0f;
        renderOscillator(voice.phaseB, voice.warpB, voice.dcB, patch.b, hz, dt, warpB, left, right);
        distribute(left, right, on(patch.routeB), patch.sendB, buses);

        // The sub and the noise generator are their own sources: each is silent
        // unless its own module is on, whatever its level knob reads. Each is
        // placed with the source pan law, the same one the oscillators spread
        // their stacks across.
        if (on(patch.subEnable))
        {
            const auto sub = subSample() * patch.subLevel;
            distribute(sub * sourcePanLeft(patch.subPan), sub * sourcePanRight(patch.subPan),
                       on(patch.routeSub), patch.sendSub, buses);
        }
        if (on(patch.noiseEnable))
        {
            const auto hiss = noise() * patch.noiseLevel;
            distribute(hiss * sourcePanLeft(patch.noisePan), hiss * sourcePanRight(patch.noisePan),
                       on(patch.routeNoise), patch.sendNoise, buses);
        }

        // The amp envelope reaches the sends as well as the two destinations.
        // A send is taken after the channel's own fader, and ENV 1 is part of
        // what that fader amounts to, so a note that has finished must be
        // sending nothing.
        const auto level = voice.envelope[ampEnv] * voice.velocity;
        buses.wetLeft *= level;
        buses.wetRight *= level;
        buses.dryLeft *= level;
        buses.dryRight *= level;
        for (int bus = 0; bus < busCount; ++bus)
        {
            buses.sendLeft[static_cast<size_t>(bus)] *= level;
            buses.sendRight[static_cast<size_t>(bus)] *= level;
        }
        voice.phaseSub = wrap(voice.phaseSub + hzSub * dt);
    }

    // A state-variable filter computes all three responses anyway, so the type
    // is a choice of which tap to return rather than a second filter.
    float filter(float input, float& low, float& band, float cutoff, float resonance, FilterType type) const
    {
        // The ceiling is here because the prewarp runs away as the cutoff
        // approaches Nyquist, not because the topology is fragile — a
        // zero-delay state variable filter is stable whatever g is. At 0.3 it
        // was landing at 13.2 kHz on a 44.1 kHz stream, so the top quarter of a
        // knob that goes to 18 kHz did nothing and a patch with the filter
        // wide open was still being darkened by it. 0.45 leaves tan() well
        // behaved and puts the whole of the knob's range in reach at both of
        // the rates Forge is ever asked to run at.
        const auto limitedCutoff = juce::jlimit(25.0f, static_cast<float>(sampleRate * 0.45), cutoff);
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
    // One over the length of the tail fade in samples, worked out when the
    // sample rate is known rather than per sample.
    float tailStep = 1.0f / (44100.0f * voiceTailSeconds);
    // The pole of the offset blocker a warped oscillator runs, worked out when
    // the sample rate is known rather than per sample.
    float dcBlock = warpDcCoefficient(48000.0);
    // The free-running cycles, one per LFO. An LFO in OFF reads these: the same
    // cycle for every voice and for the panel, running whether or not anything
    // is playing, which is the whole of what OFF means. The keyboard modes
    // ignore them and read the copy inside the voice instead.
    std::array<float, lfoCount> freePhase {};
    std::array<float, lfoCount> freeHeld {};
    // What the panel is shown for each LFO. The held step is carried as well as
    // the phase because a sample and hold's step cannot be worked back out of
    // its phase.
    std::array<float, lfoCount> meterLfoPhase {};
    std::array<float, lfoCount> meterLfoHeld {};
    std::array<float, lfoCount> meterLfoValue {};
    std::uint32_t noiseState = 0x9e3779b9u;
    std::array<juce::NormalisableRange<float>, destinationCount> destinationRanges {};
    // Reused every voice and every sample so a modulated render allocates
    // nothing; only touched when at least one slot is live.
    Patch scratch {};
    // The three racks, rendered. They hold delay lines and filter state, so
    // they belong to the Core rather than to the patch that describes them.
    std::array<FxRack, rackCount> racks;
    double tempo = 0.0;
    // Where the loudest voice's modulated rack settings are kept, so the racks
    // can be run from them once the loop is over. Only touched when something
    // is actually pointed at a rack.
    Patch fxScratch {};

    std::array<int, 16> heldNotes {};
    int heldCount = 0;
    bool monoMode = false;
    std::array<float, envCount> meterEnvelope {};
    std::array<EnvelopeStage, envCount> meterStage {};
    std::array<float, destinationCount> meterOffsets {};
};
}
