#pragma once

#include "ForgeWavetable.h"

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
namespace rhino::forge
{
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
        default: break;
    }
    return nullptr;
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
        // Touched here so the table is built on whichever thread prepares the
        // synth, never lazily on the first note from the audio thread.
        builtInWavetable();
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

        meterEnvelope = {};
        meterStage = {};
        meterOffsets = {};

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

            left += (routedLeft + buses.dryLeft) * voice.tail;
            right += (routedRight + buses.dryRight) * voice.tail;
        }

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
        std::array<float, unisonMax> phaseA {}, phaseB {};
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
    void renderOscillator(std::array<float, unisonMax>& phases, const Oscillator& osc, float baseHz,
                          float dt, float& left, float& right) const
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
        const auto& table = osc.table != nullptr ? *osc.table : builtInWavetable();
        const auto level = table.levelFor(hz * 1.05f, sampleRate);

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

            const auto sample = table.sample(level, position, phases[static_cast<size_t>(i)]) * gain;
            const auto pan = juce::jlimit(-1.0f, 1.0f, osc.pan + spread * detune * 1.6f);
            stackLeft += sample * std::sqrt(0.5f * (1.0f - pan));
            stackRight += sample * std::sqrt(0.5f * (1.0f + pan));

            const auto ratio = std::pow(2.0f, offset * detune
                                              * unisonSpreadSemitones / 12.0f);
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

        const auto level = voice.envelope[ampEnv] * voice.velocity;
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
    std::array<int, 16> heldNotes {};
    int heldCount = 0;
    bool monoMode = false;
    std::array<float, envCount> meterEnvelope {};
    std::array<EnvelopeStage, envCount> meterStage {};
    std::array<float, destinationCount> meterOffsets {};
};
}
