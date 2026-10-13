#pragma once

#include "ForgeWavetable.h"
#include "ForgeWarp.h"
#include "ForgeLfoTable.h"
#include "ForgeFilter.h"
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
// How many oscillators a voice has. They are identical and none is defined in
// terms of another, so tables, modulation destinations and render state stay
// one fixed-size bank instead of becoming lettered special cases.
inline constexpr int oscillatorCount = 3;

inline const char* oscillatorPrefix(int oscillator)
{
    static constexpr std::array<const char*, oscillatorCount> prefixes {"oscA", "oscB", "oscC"};
    jassert(oscillator >= 0 && oscillator < oscillatorCount);
    return oscillator >= 0 && oscillator < oscillatorCount
        ? prefixes[static_cast<size_t>(oscillator)] : "";
}

inline juce::String oscillatorLetter(int oscillator)
{
    jassert(oscillator >= 0 && oscillator < oscillatorCount);
    return oscillator >= 0 && oscillator < oscillatorCount
        ? juce::String::charToString(static_cast<juce::juce_wchar>('A' + oscillator))
        : juce::String();
}

inline int oscillatorIndexFromId(const juce::String& id)
{
    for (int oscillator = 0; oscillator < oscillatorCount; ++oscillator)
        if (id.startsWith(oscillatorPrefix(oscillator))) return oscillator;
    return -1;
}

// Which oscillator a warp stage on `carrier` reads, or -1 for a mode that reads
// none. The ring is the bank's own order, so the rule is the same at every
// index: the next one round from C is A, and the one before A is C.
inline int warpSourceOscillator(WarpMode mode, int carrier)
{
    const auto at = juce::jlimit(0, oscillatorCount - 1, carrier);
    switch (warpSourceOf(mode))
    {
        case WarpSource::nextOscillator:     return (at + 1) % oscillatorCount;
        case WarpSource::previousOscillator: return (at + oscillatorCount - 1) % oscillatorCount;
        default: break;
    }
    return -1;
}

// What a mode is called on one oscillator. An OSC mode names the oscillator it
// reads -- PD OSC B on A, PD OSC A on B -- so nobody has to work out which way
// round the ring runs from a menu.
//
// Built once and handed out as a pointer, as the plain table's names are,
// because a warp field holds its choices by pointer.
inline const char* warpModeName(int mode, int carrier)
{
    using Names = std::array<std::array<juce::String, warpModeCount>, oscillatorCount>;
    static const Names names = []
    {
        Names built;
        for (int oscillator = 0; oscillator < oscillatorCount; ++oscillator)
            for (int index = 0; index < warpModeCount; ++index)
            {
                const auto which = static_cast<WarpMode>(index);
                const auto source = warpSourceOscillator(which, oscillator);
                const juce::String name(warpInfo(which).name);
                built[static_cast<size_t>(oscillator)][static_cast<size_t>(index)] =
                    source < 0 ? name : name + " " + oscillatorLetter(source);
            }
        return built;
    }();
    return names[static_cast<size_t>(juce::jlimit(0, oscillatorCount - 1, carrier))]
                [static_cast<size_t>(juce::jlimit(0, warpModeCount - 1, mode))].toRawUTF8();
}

// The order a carrier's warp field lists its modes in, which is the menu's:
// family by family in the order the categories are declared, and inside a
// family each OSC mode beside its partner from the other side of the ring, the
// two in letter order. Stored order is arrival order, which would put PD OSC C
// after RM NOISE, so stepping the field's arrows walks this instead and the two
// orders meet only at warpModeAt and warpModePosition.
inline std::array<int, warpModeCount> warpModeOrder(int carrier)
{
    std::array<int, warpModeCount> order {};
    size_t filled = 0;
    for (int category = 0; category < warpCategoryCount; ++category)
        for (int index = 0; index < warpModeCount; ++index)
        {
            const auto mode = static_cast<WarpMode>(index);
            if (warpCategoryOf(mode) != static_cast<WarpCategory>(category)) continue;
            // Seated beside its partner, which comes first in stored order.
            if (warpSourceOf(mode) == WarpSource::previousOscillator) continue;
            order[filled++] = index;
            if (warpSourceOf(mode) != WarpSource::nextOscillator) continue;
            const auto partner = warpOtherSide(mode);
            order[filled++] = static_cast<int>(partner);
            if (warpSourceOscillator(partner, carrier) < warpSourceOscillator(mode, carrier))
                std::swap(order[filled - 2], order[filled - 1]);
        }
    jassert(filled == order.size());
    return order;
}

inline int warpModePosition(int mode, int carrier)
{
    const auto order = warpModeOrder(carrier);
    for (size_t i = 0; i < order.size(); ++i)
        if (order[i] == mode) return static_cast<int>(i);
    return 0;
}

inline int warpModeAt(int position, int carrier)
{
    return warpModeOrder(carrier)[static_cast<size_t>(juce::jlimit(0, warpModeCount - 1, position))];
}

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

// What kind of oscillator this is — not what it is playing, but which engine
// renders it and which controls the module shows for it.
//
// Serum's header menu offers five: Wavetable, Multisample, Sample, Granular,
// Spectral. Forge builds two of them. The numbering deliberately leaves the
// other three room in the middle rather than appending spectral after
// wavetable, so a preset written today names the same mode once Sample and
// Granular arrive and nothing has to be migrated. See
// wiki/pages/forge-spectral.md.
enum class OscMode { wavetable = 0, spectral = 4 };
inline constexpr int oscModeCount = 5;

inline const char* oscModeName(int mode)
{
    switch (mode)
    {
        case 1: return "MULTISAMPLE";
        case 2: return "SAMPLE";
        case 3: return "GRANULAR";
        case 4: return "SPECTRAL";
        default: break;
    }
    return "WAVETABLE";
}

// Whether Forge actually renders this mode yet. The three it does not are
// declared so the numbering is Serum's and a preset survives them arriving,
// but they are left out of the menu rather than offered and then refused.
inline constexpr bool oscModeBuilt(int mode)
{
    return mode == static_cast<int>(OscMode::wavetable)
        || mode == static_cast<int>(OscMode::spectral);
}

// What a mode value read off a parameter means to the engine. Anything not
// built falls back to a wavetable, so a preset from a later version opens and
// sounds like something rather than falling silent.
inline OscMode oscModeOf(float value) noexcept
{
    const auto mode = juce::roundToInt(value);
    return mode == static_cast<int>(OscMode::spectral) ? OscMode::spectral : OscMode::wavetable;
}

// What is stored and what is shown are two orders, exactly as they are for the
// noise module's SOURCE field. The parameter holds Serum's numbering, with gaps
// where Multisample, Sample and Granular will go; the field lists only the
// modes that are built, so stepping it with the arrows cannot land on one that
// does nothing. The two orders meet here and nowhere else.
inline constexpr int oscModeBuiltCount = 2;

inline int oscModeAt(int position) noexcept
{
    auto seen = 0;
    for (int mode = 0; mode < oscModeCount; ++mode)
        if (oscModeBuilt(mode) && seen++ == position) return mode;
    return static_cast<int>(OscMode::wavetable);
}

inline int oscModePosition(int mode) noexcept
{
    auto seen = 0;
    for (int i = 0; i < oscModeCount; ++i)
    {
        if (i == mode) return seen;
        if (oscModeBuilt(i)) ++seen;
    }
    return 0;
}

// Everything one oscillator owns. Every oscillator is the same shape, so
// switching one off or changing its level cannot move another.
struct Oscillator
{
    float enable = 1.0f;
    // Which engine renders this oscillator. Held as a float for the same reason
    // the warp modes are: that is what a parameter read gives back.
    float mode = 0.0f;
    float position = 0.55f;
    float octave = 0.0f, semitone = 0.0f, fine = 0.0f;
    float unison = 2.0f, detune = 0.18f, blend = 0.5f;
    float pan = 0.0f, level = 0.75f;
    // The spectral set. Meaningless while the mode is wavetable, and read by
    // nothing in that case — see wiki/pages/forge-spectral.md for what each one
    // does.
    float scan = 1.0f, cut = 1.0f, mix = 0.0f;
    // How the playhead travels, and the two pairs of markers a mode chooses
    // between: START and END for the modes that play a stretch, the loop's
    // ends for the modes that go round. Each is 0..1 of the whole sample. See
    // SpectralLoop in ForgeSpectral.h.
    float loopMode = 1.0f;
    float start = 0.0f, end = 1.0f, loopStart = 0.0f, loopEnd = 1.0f;
    // The two warp stages, in the order they are applied. A mode is held as a
    // float for the same reason everything else here is: that is what a
    // parameter read gives back. See ForgeWarp.h for what each one does.
    std::array<float, warpSlots> warpMode {};
    std::array<float, warpSlots> warpAmount {};
    // The sample this oscillator resynthesises while its mode is spectral.
    // Null means nothing has been loaded, and a spectral oscillator with
    // nothing loaded is silent — there is no built-in to fall back on, because
    // a sample is a recording and Forge ships no default one.
    //
    // Forward-declared rather than included: ForgeSpectral.h includes this
    // header for the unison spread, so naming the type here would close a
    // cycle. The Processor owns what this points at, exactly as it owns the
    // table below.
    const class Sample* sample = nullptr;
    // The table this oscillator reads. Null means the built-in frames, which is
    // what every oscillator starts on and what a Core needs no setting up to
    // sound. The Processor owns whatever this points at and outlives the voice
    // reading it; nothing here allocates or frees it.
    const Wavetable* table = nullptr;
};

// How many LFOs a voice has. Six, as Serum has eight: enough that a patch
// never has to choose between movement in one place and movement in
// another, and the panel shows one at a time rather than six at once.
inline constexpr int lfoCount = 6;

// What kind of thing an LFO is, Serum's TYPE. NORMAL is a shape read left to
// right: one of the built-in four, or one drawn over the grid. PATH is a route
// across the pad that the phase travels along, and it puts out two values at
// once, how far across it is and how far up. S&H is a staircase of random
// steps, one per grid column, a fresh flight taken at the top of every cycle;
// it was a shape here until it had a type of its own.
enum class LfoType { normal, path, sampleHold };
inline constexpr int lfoTypeCount = 3;

inline const char* lfoTypeName(int type)
{
    switch (type)
    {
        case 1: return "PATH";
        case 2: return "S&H";
        default: break;
    }
    return "NORMAL";
}

// Which way a NORMAL or a PATH LFO travels its shape, Serum's DIRECTION.
// Forward reads it start to end; reverse reads it end to start; ping-pong
// reads it out and back, so one trip takes two cycles and nothing jumps at
// the turn. A sample and hold has no direction: its steps are random either
// way round.
enum class LfoDirection { forward, reverse, pingPong };
inline constexpr int lfoDirectionCount = 3;

inline const char* lfoDirectionName(int direction)
{
    switch (direction)
    {
        case 1: return "REVERSE";
        case 2: return "PING-PONG";
        default: break;
    }
    return "FORWARD";
}

// The LFO shapes a NORMAL LFO starts from. An LFO is a modulation source, so
// every shape is bipolar and runs the full -1..1: a slot's depth decides how
// much of that reaches anything.
enum class LfoShape { sine, triangle, saw, square };
inline constexpr int lfoShapeCount = 4;

inline const char* lfoShapeName(int shape)
{
    switch (shape)
    {
        case 1: return "TRI";
        case 2: return "SAW";
        case 3: return "SQR";
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
        default: break;
    }
    return "Sine";
}

// A step inside a cycle, in a structure whose points have to keep going
// forwards in x. A thousandth of a cycle is under half a pixel on the graph, and
// shorter than the block an LFO turns over in at any rate one can be set to.
inline constexpr float lfoStepGap = 0.001f;

// The default shapes, as node sets rather than as formulas. A Default shape is
// the same kind of thing a drawn one is: the dots on it are its own corners, the
// ring in each gap is its own bend, and picking one up to edit starts from
// exactly what was on screen instead of from a row of sampled dots that only
// looked like it. It is also the only definition of these shapes — the panel and
// the voice both read it, so neither can drift from the other.
//
// Three nodes carry a triangle and a sine alike: the sine is the same two
// segments with both bowed to the limit. A parabola is not a sine, and stands
// about 0.056 proud of one at its widest — but its peaks, its zero crossings and
// its slope through them are all exact, and it is smooth across every join
// including the cycle boundary. That is what a sine costs to be three nodes you
// can take hold of rather than a formula you cannot.
constexpr LfoTable lfoShapeTable(LfoShape shape) noexcept
{
    LfoTable table;
    switch (shape)
    {
        case LfoShape::triangle:
            table.count = 3;
            table.points[0] = {0.0f, -1.0f};
            table.points[1] = {0.5f, 1.0f};
            table.points[2] = {1.0f, -1.0f};
            break;
        case LfoShape::saw:
            table.count = 2;
            table.points[0] = {0.0f, -1.0f};
            table.points[1] = {1.0f, 1.0f};
            break;
        case LfoShape::square:
            table.count = 4;
            table.points[0] = {0.0f, 1.0f};
            table.points[1] = {0.5f - lfoStepGap, 1.0f};
            table.points[2] = {0.5f, -1.0f};
            table.points[3] = {1.0f, -1.0f};
            break;
        case LfoShape::sine:
            table.count = 3;
            table.points[0] = {0.0f, 0.0f, 1.0f};
            table.points[1] = {0.5f, 0.0f, -1.0f};
            table.points[2] = {1.0f, 0.0f};
            break;
    }
    return table;
}

inline constexpr std::array<LfoTable, lfoShapeCount> lfoShapeTables {
    lfoShapeTable(LfoShape::sine), lfoShapeTable(LfoShape::triangle),
    lfoShapeTable(LfoShape::saw), lfoShapeTable(LfoShape::square)};

// The shape at a point in its cycle. A free function so the panel draws the
// very curve the voice is reading, the way the oscillator display and the
// oscillator share morph().
inline float lfoWave(LfoShape shape, float phase)
{
    return lfoShapeTables[static_cast<size_t>(shape)].sample(phase);
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
// Held as floats because that is what a parameter read gives back. SMOOTH is
// the sample and hold's: how much of each step is a ramp to the next rather
// than a hold, from none to the whole step.
struct LfoSetting
{
    float rate = 0.5f;
    float type = 0.0f;
    float shape = 0.0f;
    float mode = 0.0f;
    float direction = 0.0f;
    float smooth = 0.0f;
    LfoTable table;
    LfoPath path;
};

// The type, the mode, the direction and the shape a setting names, clamped,
// so one reading serves the voice, the panel and the tests.
inline LfoType lfoTypeOf(const LfoSetting& lfo)
{
    return static_cast<LfoType>(juce::jlimit(0, lfoTypeCount - 1, juce::roundToInt(lfo.type)));
}

inline LfoMode lfoModeOf(const LfoSetting& lfo)
{
    return static_cast<LfoMode>(juce::jlimit(0, lfoModeCount - 1, juce::roundToInt(lfo.mode)));
}

inline LfoDirection lfoDirectionOf(const LfoSetting& lfo)
{
    if (lfoTypeOf(lfo) == LfoType::sampleHold) return LfoDirection::forward;
    return static_cast<LfoDirection>(
        juce::jlimit(0, lfoDirectionCount - 1, juce::roundToInt(lfo.direction)));
}

// How far the travel phase runs before the cycle starts again: one for a
// shape read one way, two for ping-pong's trip out and back. The rate is the
// speed of travel, so a ping-pong LFO at 1 Hz takes two seconds to come home.
inline float lfoTravelSpan(const LfoSetting& lfo)
{
    return lfoDirectionOf(lfo) == LfoDirection::pingPong ? 2.0f : 1.0f;
}

// Where in its shape, 0..1, an LFO is reading at a point of its travel. This
// is the position the panel's indicator shows, so a reversed LFO is seen
// running right to left and a ping-pong one is seen turning round.
inline float lfoPositionOf(const LfoSetting& lfo, float travel)
{
    switch (lfoDirectionOf(lfo))
    {
        case LfoDirection::reverse: return 1.0f - std::clamp(travel, 0.0f, 1.0f);
        case LfoDirection::pingPong:
            return travel <= 1.0f ? std::clamp(travel, 0.0f, 1.0f) : std::clamp(2.0f - travel, 0.0f, 1.0f);
        case LfoDirection::forward: break;
    }
    return std::clamp(travel, 0.0f, 1.0f);
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

// How many steps a sample and hold takes in a cycle: as many as the grid has
// columns, which is the one place that count is set.
inline int lfoStepCount(const LfoSetting& setting)
{
    return std::clamp(setting.table.columns, 1, maxLfoSteps - 1);
}

// What one LFO puts out: where it is, and, for a PATH, where it is on the
// other axis too. Every other type puts its one value on both, so a slot fed
// from the Y output keeps moving when the type is switched away from PATH
// rather than falling silent.
struct LfoOutput
{
    float x = 0.0f;
    float y = 0.0f;
};

// The value at a position in the shape. `steps` is the sample and hold's
// staircase for this cycle, the one thing the position alone cannot say. A
// free function so the panel draws the very curve the voice is reading.
inline LfoOutput lfoOutputAt(const LfoSetting& setting, float position, const LfoSteps& steps)
{
    switch (lfoTypeOf(setting))
    {
        case LfoType::path:
        {
            const auto& path = setting.path.custom ? setting.path : lfoDefaultPathTable;
            const auto point = path.at(position);
            return {point.x * 2.0f - 1.0f, point.y * 2.0f - 1.0f};
        }
        case LfoType::sampleHold:
        {
            // Held for the whole column, then, with SMOOTH, a straight ramp to
            // the next step over the last part of it: at full SMOOTH every
            // column is a ramp and the staircase is a line through its steps.
            const auto count = lfoStepCount(setting);
            const auto along = std::clamp(position, 0.0f, 1.0f) * static_cast<float>(count);
            const auto index = std::min(count - 1, static_cast<int>(along));
            const auto held = steps[static_cast<size_t>(index)];
            const auto smooth = std::clamp(setting.smooth, 0.0f, 1.0f);
            const auto within = along - static_cast<float>(index);
            if (smooth <= 0.0f || within <= 1.0f - smooth) return {held, held};
            const auto next = steps[static_cast<size_t>(index + 1)];
            const auto value = held + (next - held) * ((within - (1.0f - smooth)) / smooth);
            return {value, value};
        }
        case LfoType::normal: break;
    }
    const auto value = setting.table.custom ? setting.table.sample(position)
                                            : lfoWave(lfoShapeOf(setting), position);
    return {value, value};
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
