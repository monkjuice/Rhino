#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <vector>

// The filter: what types there are, what each one holds between samples, and
// the one function that runs any of them.
//
// This file depends on nothing of Forge's. That is deliberate, and it is the
// rule Rhino's own EQ lives under: the curve the panel draws and the audio the
// voice renders both read this header, so a response that is drawn cannot be a
// response that is not rendered. `filterSample` is the audio and
// `filterMagnitude` is the picture, and they are written from the same
// coefficients a few lines apart.
//
// The list is grouped the way Serum groups its own, and for the same reason:
// there are thirty-four types here and around ninety-six in Serum, nearly all
// of which differ only in slope. A family per submenu is a menu to use rather
// than a list to read.
//
// Appended to, never inserted into. A type is stored as an index into this
// list, so every index already written into a preset has to keep meaning what
// it meant — LOW, HIGH and BAND are the three Forge shipped with and they are
// still 0, 1 and 2.
namespace rhino::forge
{
// --- The cutoff knob's own travel --------------------------------------------
//
// The ends of the CUTOFF parameter's range, and the exponential sweep between
// them. Three things read this and all three have to agree: the second corner
// of a dual filter, which is a frequency on the same scale; the vowel a formant
// filter's knob is pointing at, which is a position on it; and the readouts.
//
// Declared here rather than taken from the parameter, because ForgeFilter.h
// depends on nothing of Forge's — so the one thing to keep in step is this pair
// against the range in ForgeParameters.cpp, and filterListSuite checks it.
inline constexpr float filterCutoffLowHz = 30.0f;
inline constexpr float filterCutoffHighHz = 18000.0f;

// A position on the knob, 0..1, as a frequency — and back again.
inline float filterCutoffAt(float position)
{
    return filterCutoffLowHz
         * std::pow(filterCutoffHighHz / filterCutoffLowHz, juce::jlimit(0.0f, 1.0f, position));
}

inline float filterCutoffPosition(float hz)
{
    const auto at = std::log(juce::jlimit(filterCutoffLowHz, filterCutoffHighHz, hz)
                             / filterCutoffLowHz)
                  / std::log(filterCutoffHighHz / filterCutoffLowHz);
    return juce::jlimit(0.0f, 1.0f, at);
}

// Where FREQ has to sit for the second filter to land on a given frequency.
// Used by the table below so a default can be written as the frequency it means
// rather than as the number that happens to produce it.
inline float filterSecondAt(float hz) { return filterCutoffPosition(hz); }

// --- The list ----------------------------------------------------------------

enum class FilterCategory { basic, dual, morph, analog, resonator, character };
inline constexpr int filterCategoryCount = 6;

inline const char* filterCategoryName(FilterCategory category)
{
    switch (category)
    {
        case FilterCategory::dual:      return "DUAL";
        case FilterCategory::morph:     return "MORPH";
        case FilterCategory::analog:    return "ANALOG";
        case FilterCategory::resonator: return "RESONATORS";
        case FilterCategory::character: return "CHARACTER";
        case FilterCategory::basic:     break;
    }
    return "BASIC";
}

enum class FilterType
{
    // Basic: one state-variable filter, one of its taps. The first three are
    // the whole of what Forge had before this file existed.
    lowPass, highPass, bandPass, notch, peak,
    // Dual: two state-variable filters in series. CUTOFF is the first one's
    // corner and FREQ is the second one's — a frequency of its own, across the
    // same span the cutoff knob covers, which is what the Serum manual says it
    // is: "FREQ: set the cutoff frequency of the second SVF filter". RES
    // applies equally to both, which it also says.
    //
    // An offset from the cutoff was tried first, on the reasoning that a pair
    // set an octave apart should stay an octave apart across a sweep. It is the
    // wrong call: it makes the notch in PK+NT unreachable while the cutoff is
    // low, which is exactly the setting the manual's own screenshots use — a
    // peak parked at the bottom of the knob and a notch several kilohertz up.
    // An absolute second corner is what lets a sweep drag one filter past a
    // stationary other, and pointing the matrix at FREQ is what moves them
    // together.
    //
    // In series rather than in parallel because that is what the names read as
    // — LP+NT is a low pass with a notch in it — and because it is what gives
    // LP+HP two independently placed edges, which is the one thing BAND cannot
    // do. Parallel would also mean no true notch: the other path would fill it
    // in, and the manual's screenshots show a notch that goes to nothing.
    lowHigh, lowBand, lowPeak, lowNotch,
    highBand, highPeak, highNotch,
    bandPeak, bandNotch,
    peakPeak, peakNotch, notchNotch,
    // Morph: three taps of one filter crossfaded by MORPH rather than
    // combined. The middle response is what the middle of the knob is.
    morphLowBandHigh, morphLowPeakHigh, morphLowNotchHigh, morphBandPeakNotch,
    // Analog: poles in a chain with the last one's output fed back round them
    // through a saturator. That loop is the whole of the character — it is why
    // the resonance limits itself instead of running away, and why the
    // passband moves when the filter is pushed.
    ladder, acid, dirtyLadder, ems,
    // Resonators: a delay or a chain of all-passes rather than a corner, so
    // the cutoff sets the spacing of a row of peaks or nulls instead of the
    // place the band stops.
    comb, flanger, phaser,
    // Character: the ones that are filters only by where they sit. Three of
    // them are not linear at all, which is the point of them.
    formant, ringMod, sampleHold, diffusor, scream, reverb,
};

inline constexpr int filterTypeCount = 34;

// Which response of one state-variable filter a type reads. Five come off the
// one topology, so a dual type is a pair of these and a morph type is three.
enum class FilterTap { low, high, band, notch, peak };

// Everything the panel, the readouts and the engine need to know about one
// type. The second control is the one field whose meaning changes with the
// type, exactly as a rack slot's knobs do: FREQ on a dual, MORPH on a morph,
// DAMP on a comb. `initSecond` is where it opens when the type is chosen from
// the menu — a dual filter whose second corner sat on top of its first would
// open sounding like neither of them.
struct FilterInfo
{
    const char* name;
    FilterCategory category;
    const char* second;
    float initSecond;
    // The taps this type reads, in order: one for a basic type, two for a
    // dual, three for a morph. Nothing else reads them.
    std::array<FilterTap, 3> taps;
};

inline const std::array<FilterInfo, filterTypeCount>& filterTypes()
{
    using C = FilterCategory;
    using T = FilterTap;
    static const std::array<FilterInfo, filterTypeCount> table {{
        // FAT opens at nothing on the five basic types, and that is not a
        // taste: a preset written before this parameter existed loads it at
        // its default, so the default has to be the setting that leaves the
        // filter doing exactly what it did.
        {"LOW",      C::basic, "FAT", 0.0f, {T::low}},
        {"HIGH",     C::basic, "FAT", 0.0f, {T::high}},
        {"BAND",     C::basic, "FAT", 0.0f, {T::band}},
        {"NOTCH",    C::basic, "FAT", 0.0f, {T::notch}},
        {"PEAK",     C::basic, "FAT", 0.0f, {T::peak}},

        // FREQ is an absolute frequency now, so these are positions on the
        // cutoff knob's own travel rather than intervals. LP+HP opens with its
        // high pass at 120 Hz, under anything the low pass is likely to be set
        // to, so the pair passes a band rather than nothing. The rest open
        // their second filter at 2 kHz, where a notch or a peak is audible on
        // almost any patch.
        {"LP+HP",    C::dual, "FREQ", filterSecondAt(120.0f),  {T::low,   T::high}},
        {"LP+BP",    C::dual, "FREQ", filterSecondAt(2000.0f), {T::low,   T::band}},
        {"LP+PK",    C::dual, "FREQ", filterSecondAt(2000.0f), {T::low,   T::peak}},
        {"LP+NT",    C::dual, "FREQ", filterSecondAt(2000.0f), {T::low,   T::notch}},
        {"HP+BP",    C::dual, "FREQ", filterSecondAt(2000.0f), {T::high,  T::band}},
        {"HP+PK",    C::dual, "FREQ", filterSecondAt(2000.0f), {T::high,  T::peak}},
        {"HP+NT",    C::dual, "FREQ", filterSecondAt(2000.0f), {T::high,  T::notch}},
        {"BP+PK",    C::dual, "FREQ", filterSecondAt(2000.0f), {T::band,  T::peak}},
        {"BP+NT",    C::dual, "FREQ", filterSecondAt(2000.0f), {T::band,  T::notch}},
        {"PK+PK",    C::dual, "FREQ", filterSecondAt(2000.0f), {T::peak,  T::peak}},
        {"PK+NT",    C::dual, "FREQ", filterSecondAt(2000.0f), {T::peak,  T::notch}},
        {"NT+NT",    C::dual, "FREQ", filterSecondAt(2000.0f), {T::notch, T::notch}},

        {"LP-BP-HP", C::morph, "MORPH", 0.5f, {T::low,  T::band,  T::high}},
        {"LP-PK-HP", C::morph, "MORPH", 0.5f, {T::low,  T::peak,  T::high}},
        {"LP-NT-HP", C::morph, "MORPH", 0.5f, {T::low,  T::notch, T::high}},
        {"BP-PK-NT", C::morph, "MORPH", 0.5f, {T::band, T::peak,  T::notch}},

        {"LADDER",   C::analog, "FAT",  0.35f, {T::low}},
        {"ACID",     C::analog, "FAT",  0.45f, {T::low}},
        {"DIRTY",    C::analog, "PAIN", 0.45f, {T::low}},
        {"EMS",      C::analog, "FAT",  0.35f, {T::low}},

        {"COMB",     C::resonator, "DAMP",   0.70f, {T::low}},
        {"FLANGER",  C::resonator, "DAMP",   0.70f, {T::low}},
        {"PHASER",   C::resonator, "STAGES", 0.67f, {T::low}},

        {"FORMANT",  C::character, "SHIFT",  0.50f, {T::band}},
        {"RING MOD", C::character, "SPREAD", 0.00f, {T::band}},
        {"S&H",      C::character, "DIFF",   0.00f, {T::low}},
        {"DIFFUSOR", C::character, "STAGES", 0.65f, {T::low}},
        {"SCREAM",   C::character, "FEED",   0.50f, {T::low}},
        {"REVERB",   C::character, "DAMP",   0.60f, {T::low}},
    }};
    return table;
}

inline const FilterInfo& filterInfo(FilterType type)
{
    return filterTypes()[static_cast<size_t>(
        juce::jlimit(0, filterTypeCount - 1, static_cast<int>(type)))];
}

// Taken as a bare value as well as from a patch, because the panel draws the
// response from the parameter and has to land on the type the engine will run.
inline FilterType filterTypeOf(float value)
{
    return static_cast<FilterType>(juce::jlimit(0, filterTypeCount - 1, juce::roundToInt(value)));
}

inline const char* filterTypeName(int type)
{
    return filterTypes()[static_cast<size_t>(juce::jlimit(0, filterTypeCount - 1, type))].name;
}

inline FilterCategory filterCategoryOf(FilterType type) { return filterInfo(type).category; }

// What the second field is called while this type is in charge, and where it
// opens. Never null: every type gives that knob a job, which is what keeps the
// module's layout fixed whatever is chosen.
inline const char* filterSecondLabel(FilterType type) { return filterInfo(type).second; }

inline float filterSecondInit(FilterType type) { return filterInfo(type).initSecond; }

// --- What a filter holds between samples -------------------------------------

// The lowest and highest the cutoff is allowed to reach. The ceiling is here
// because the prewarp runs away as the corner approaches Nyquist, not because
// the topology is fragile — a zero-delay state variable filter is stable
// whatever g is. At 0.3 it was landing at 13.2 kHz on a 44.1 kHz stream, so
// the top quarter of a knob that goes to 18 kHz did nothing and a patch with
// the filter wide open was still being darkened by it. 0.45 leaves tan() well
// behaved and puts the whole of the knob's range in reach at both of the rates
// Forge is ever asked to run at.
inline constexpr float filterLowestHz = 25.0f;
inline constexpr float filterNyquistShare = 0.45f;

inline float filterClampHz(float hz, double sampleRate)
{
    return juce::jlimit(filterLowestHz,
                        static_cast<float>(sampleRate * filterNyquistShare), hz);
}

// How long the comb's delay line has to be: one period of the lowest corner
// the filter allows, so a comb can be tuned to the bottom of the cutoff knob
// at any sample rate. Sized in seconds rather than in samples because a line
// fixed in samples is a different filter at 48 kHz and at 96.
inline constexpr float filterLongestDelaySeconds = 1.0f / filterLowestHz;

// The all-pass chain the diffusor and the reverb smear through, and how long
// each of its stages is. Eight milliseconds in the longest one: enough to
// spread a transient, and short enough that a chain of four still reads as a
// smear rather than as four repeats. A stage asked for longer than its line
// simply saturates at the line, which is what the clamp in filterDelayRead is.
inline constexpr int filterDiffuseStages = 4;
inline constexpr float filterDiffuseSeconds = 0.008f;

inline constexpr int filterLadderStages = 4;
inline constexpr int filterPhaserStages = 8;
inline constexpr int filterFormantBands = 3;

// One channel of one voice's filter: everything that fits in a few dozen bytes,
// which is every type but the four that need a delay line. Small on purpose —
// a voice holds two of these and Core holds sixteen voices, and a voice is
// reset by assigning a default-constructed one over it, so anything in here is
// something that gets wiped on a fresh note and costs nothing to wipe.
struct FilterState
{
    float low = 0.0f, band = 0.0f;
    float low2 = 0.0f, band2 = 0.0f;
    // The poles of a ladder, and the last one's output a sample ago: the
    // feedback is delayed by a sample rather than solved for, which is what
    // every ladder model that stays simple does. SCREAM keeps its own output
    // here for the same reason.
    std::array<float, filterLadderStages> ladder {};
    float feedback = 0.0f;
    // The one-pole sitting in a feedback path: a comb's DAMP, a reverb's.
    float loop = 0.0f;
    std::array<float, filterPhaserStages> phase {};
    float phaserFeedback = 0.0f;
    // The formant bank: three resonators, each a state-variable filter of its
    // own, summed at the vowel's own gains.
    std::array<float, filterFormantBands> formantLow {}, formantBand {};
    // Where the sample and hold is in its period and what it is holding, and
    // the ring modulator's two oscillators.
    float holdPhase = 0.0f, held = 0.0f;
    float ringPhase = 0.0f, ringPhase2 = 0.0f;
};

// The lines the comb, the flanger, the diffusor and the reverb read, which are
// all of the bytes and none of the state above.
//
// They are deliberately *not* in the voice. A fresh note resets a voice by
// assigning a default-constructed one over it, and a line held there would be
// freed and reallocated by that assignment — an allocation on the audio thread,
// on every note. So they belong to Core and are sized once, in `initialise`,
// which is the one place a delay line may be built: exactly where the effects
// racks size theirs, and for exactly the same reason.
//
// One per voice per channel, because a comb shared between voices is a comb fed
// several notes at once, which is a different effect and not a filter. That
// costs about 440 KB in a Core at 48 kHz, on the heap where it belongs.
//
// Lengths are rounded up to a power of two so a read wraps with a mask.
struct FilterDelays
{
    std::vector<float> comb;
    std::array<std::vector<float>, filterDiffuseStages> diffuse;
    int combMask = 0, combWrite = 0;
    int diffuseMask = 0;
    std::array<int, filterDiffuseStages> diffuseWrite {};

    void prepare(double sampleRate)
    {
        const auto rate = juce::jmax(1.0, sampleRate);
        const auto combLength = juce::nextPowerOfTwo(juce::jmax(64,
            juce::roundToInt(rate * static_cast<double>(filterLongestDelaySeconds)) + 4));
        comb.assign(static_cast<size_t>(combLength), 0.0f);
        combMask = combLength - 1;

        const auto stageLength = juce::nextPowerOfTwo(juce::jmax(32,
            juce::roundToInt(rate * static_cast<double>(filterDiffuseSeconds))));
        for (auto& line : diffuse) line.assign(static_cast<size_t>(stageLength), 0.0f);
        diffuseMask = stageLength - 1;

        combWrite = 0;
        diffuseWrite = {};
    }

    // False in a Core that was never given a sample rate. The four types that
    // read a line check it rather than trusting the caller, so an unprepared
    // Core passes the signal through instead of reading an empty vector.
    bool ready() const { return comb.size() > 4 && !diffuse[0].empty(); }

    // Emptied without being resized, which is what a reset is allowed to cost.
    void reset()
    {
        std::fill(comb.begin(), comb.end(), 0.0f);
        for (auto& line : diffuse) std::fill(line.begin(), line.end(), 0.0f);
        combWrite = 0;
        diffuseWrite = {};
    }
};

// --- The settings, and what they resolve to ----------------------------------

// Core's damping term. The response reads it too, so the two cannot drift.
inline float filterDamping(float resonance)
{
    return 1.0f / (1.0f + juce::jlimit(0.0f, 1.0f, resonance) * 15.0f);
}


// The four settings that decide the shape, as one value. The panel draws from
// this and the engine renders from it, so a curve is never a reading of
// something the voice is not running.
struct FilterShape
{
    FilterType type = FilterType::lowPass;
    float cutoff = 7800.0f;
    float resonance = 0.12f;
    // 0..1. What it means is the type's business — see `filterSecondLabel`.
    float second = 0.0f;
    // Only the delay families need it, and only to say how low they can be
    // tuned before the line runs out.
    double sampleRate = 48000.0;
};

// The second corner: a frequency of its own, read off the same travel the
// cutoff knob has, so FREQ at a given position means the same frequency CUTOFF
// would mean there.
inline float filterSecondHz(const FilterShape& shape)
{
    return filterClampHz(filterCutoffAt(juce::jlimit(0.0f, 1.0f, shape.second)),
                         shape.sampleRate);
}

// Where a comb is tuned: the cutoff, and nothing else. The line is sized from
// `filterLowestHz`, which is the same floor the cutoff itself is clamped to, so
// there is no tuning the knob can ask for that the line has no room for — and
// therefore no case where the display and the audio could disagree about where
// the comb went.
inline float filterCombHz(const FilterShape& shape)
{
    return filterClampHz(shape.cutoff, shape.sampleRate);
}

// Where the one-pole in a feedback path sits. 200 Hz at nothing to 18 kHz at
// full: a comb damped to the bottom of that is a thud and to the top of it is
// a bright ring, which is the whole span the control is for.
inline float filterDampHz(const FilterShape& shape)
{
    const auto at = juce::jlimit(0.0f, 1.0f, shape.second);
    return filterClampHz(200.0f * std::exp2(at * 6.5f), shape.sampleRate);
}

// How many stages a type that counts them is running.
inline int filterStagesOf(const FilterShape& shape)
{
    const auto at = juce::jlimit(0.0f, 1.0f, shape.second);
    if (filterTypeOf(static_cast<float>(shape.type)) == FilterType::phaser)
        return 2 + juce::roundToInt(at * static_cast<float>(filterPhaserStages - 2));
    return 1 + juce::roundToInt(at * static_cast<float>(filterDiffuseStages - 1));
}

// Three weights from one position. The first pair crosses over the bottom half
// of the knob and the second pair the top, so the middle of the knob is the
// middle response on its own rather than a blend of the outer two.
inline std::array<float, 3> filterMorphWeights(float morph)
{
    const auto at = juce::jlimit(0.0f, 1.0f, morph) * 2.0f;
    if (at <= 1.0f) return {1.0f - at, at, 0.0f};
    return {0.0f, 2.0f - at, at - 1.0f};
}

// Everything the transcendentals produce, worked out once per voice per sample
// and shared by that voice's two channels. Only the active family's fields are
// filled; the rest are left at nothing because nothing will read them.
struct FilterCoefficients
{
    FilterType type = FilterType::lowPass;
    FilterCategory category = FilterCategory::basic;
    std::array<FilterTap, 3> taps {};
    // The two prewarped corners, and the damping they share.
    float g = 0.0f, g2 = 0.0f, damping = 1.0f;
    float resonance = 0.0f, second = 0.0f;
    std::array<float, 3> morph {};
    // The ladder families: the coefficient of one pole, the feedback round the
    // chain, the make-up that feedback costs, and how hard the loop saturates.
    float pole = 0.0f, feedback = 0.0f, makeUp = 1.0f, heat = 0.0f;
    int stages = 1;
    // The delay families: how far back to read, the one-pole in the loop, and
    // how much of the loop comes back.
    float delaySamples = 0.0f, loopPole = 0.0f, loopGain = 0.0f;
    // The all-pass coefficient the phaser, the diffusor and the reverb share.
    float allpass = 0.0f, scatter = 0.0f;
    // The oscillator a ring modulator or a sample and hold runs on.
    float step = 0.0f, step2 = 0.0f;
    std::array<float, filterFormantBands> formantG {}, formantGain {};
};

// --- Vowels ------------------------------------------------------------------

// The five vowels as three formants each, in Hertz and at the relative levels
// they are actually heard at. Standard values for a male voice; SHIFT is what
// moves the whole set for any other.
inline const std::array<std::array<float, filterFormantBands>, 5>& filterVowelHz()
{
    static const std::array<std::array<float, filterFormantBands>, 5> table {{
        {{ 800.0f, 1150.0f, 2900.0f }},   // A
        {{ 400.0f, 1700.0f, 2300.0f }},   // E
        {{ 350.0f, 1700.0f, 2700.0f }},   // I
        {{ 450.0f,  800.0f, 2830.0f }},   // O
        {{ 325.0f,  700.0f, 2530.0f }},   // U
    }};
    return table;
}

inline const std::array<std::array<float, filterFormantBands>, 5>& filterVowelGain()
{
    static const std::array<std::array<float, filterFormantBands>, 5> table {{
        {{ 1.0f, 0.63f, 0.10f }},
        {{ 1.0f, 0.50f, 0.25f }},
        {{ 1.0f, 0.16f, 0.10f }},
        {{ 1.0f, 0.35f, 0.10f }},
        {{ 1.0f, 0.10f, 0.05f }},
    }};
    return table;
}

// Where the cutoff knob puts the mouth. A formant filter has no corner to set,
// so the knob moves through the vowels instead — the same thing Serum does with
// it ("with just a couple exceptions, such as vowels for formant filters"), and
// the reason FORMANT's cutoff readout is a vowel rather than a frequency. Read
// off the knob's own travel, so the five vowels are evenly spaced across it.
inline float filterVowelPosition(float cutoff)
{
    return filterCutoffPosition(cutoff) * 4.0f;
}

inline const char* filterVowelName(float cutoff)
{
    switch (juce::roundToInt(filterVowelPosition(cutoff)))
    {
        case 1: return "E";
        case 2: return "I";
        case 3: return "O";
        case 4: return "U";
        default: break;
    }
    return "A";
}

// One formant of the vowel the knob is between, and its level. Blended rather
// than stepped, so sweeping the knob is a mouth moving and not five presets.
inline void filterFormantAt(float cutoff, float shift, int band, float& hz, float& gain)
{
    const auto at = filterVowelPosition(cutoff);
    const auto first = juce::jlimit(0, 4, static_cast<int>(at));
    const auto second = juce::jmin(4, first + 1);
    const auto mix = at - static_cast<float>(first);
    const auto i = static_cast<size_t>(band);
    const auto& hzTable = filterVowelHz();
    const auto& gainTable = filterVowelGain();
    hz = juce::jmap(mix, hzTable[static_cast<size_t>(first)][i],
                    hzTable[static_cast<size_t>(second)][i])
       * std::exp2((juce::jlimit(0.0f, 1.0f, shift) - 0.5f) * 2.0f);
    gain = juce::jmap(mix, gainTable[static_cast<size_t>(first)][i],
                      gainTable[static_cast<size_t>(second)][i]);
}

// How tightly the formant resonators are tuned. Broader than the main filter
// at rest, because a vowel is a hump and not a whistle, and RES is what turns
// it into one.
inline float filterFormantDamping(float resonance)
{
    return 1.0f / (1.0f + (0.4f + juce::jlimit(0.0f, 1.0f, resonance) * 0.6f) * 24.0f);
}

// --- Resolving a shape -------------------------------------------------------

inline float filterPrewarp(float hz, double sampleRate)
{
    return std::tan(juce::MathConstants<float>::pi * filterClampHz(hz, sampleRate)
                    / static_cast<float>(sampleRate));
}

// A one-pole's coefficient from a prewarped corner.
inline float filterPoleOf(float g) { return g / (1.0f + g); }

inline FilterCoefficients filterCoefficientsFor(const FilterShape& shape)
{
    FilterCoefficients co;
    const auto& info = filterInfo(shape.type);
    co.type = shape.type;
    co.category = info.category;
    co.taps = info.taps;
    co.resonance = juce::jlimit(0.0f, 1.0f, shape.resonance);
    co.second = juce::jlimit(0.0f, 1.0f, shape.second);
    co.damping = filterDamping(co.resonance);

    switch (info.category)
    {
        case FilterCategory::basic:
            co.g = filterPrewarp(shape.cutoff, shape.sampleRate);
            // FAT saturates the path the resonance comes back through, which
            // rounds a sharp peak off and puts harmonics where it was.
            co.heat = co.second;
            break;

        case FilterCategory::dual:
            co.g = filterPrewarp(shape.cutoff, shape.sampleRate);
            co.g2 = filterPrewarp(filterSecondHz(shape), shape.sampleRate);
            break;

        case FilterCategory::morph:
            co.g = filterPrewarp(shape.cutoff, shape.sampleRate);
            co.morph = filterMorphWeights(co.second);
            break;

        case FilterCategory::analog:
        {
            co.pole = filterPoleOf(filterPrewarp(shape.cutoff, shape.sampleRate));
            // Three poles on the diode ladders and four on the transistor
            // ones, which is the 18 against 24 dB per octave the two are known
            // for. The poles sit on the cutoff rather than being pushed up to
            // put the cascade's own -3 dB point there: the resonance peaks
            // where the poles are, and a resonance that peaked somewhere other
            // than the frequency the knob says is the wrong trade.
            const auto diode = shape.type == FilterType::acid || shape.type == FilterType::ems;
            co.stages = diode ? 3 : filterLadderStages;
            co.feedback = co.resonance * (diode ? 3.4f : 4.0f);
            // Half the gain the feedback costs, put back. All of it would take
            // the ladder's bass loss out, and that loss is half of why a
            // ladder sounds like one.
            co.makeUp = 1.0f + co.feedback * 0.5f;
            co.heat = shape.type == FilterType::dirtyLadder ? 0.55f + co.second * 0.45f
                                                            : co.second;
            break;
        }

        case FilterCategory::resonator:
            if (shape.type == FilterType::phaser)
            {
                co.allpass = filterPoleOf(filterPrewarp(shape.cutoff, shape.sampleRate));
                co.stages = filterStagesOf(shape);
                co.feedback = co.resonance * 0.9f;
                break;
            }
            co.delaySamples = static_cast<float>(shape.sampleRate) / filterCombHz(shape);
            co.loopPole = filterPoleOf(filterPrewarp(filterDampHz(shape), shape.sampleRate));
            // A comb with no feedback is the signal, so its loop never opens
            // fully: there is always a comb there and RES says how sharp it
            // is. A flanger's nulls come from the dry path instead, so its
            // loop can close all the way.
            co.loopGain = shape.type == FilterType::comb ? 0.25f + co.resonance * 0.72f
                                                         : co.resonance * 0.85f;
            break;

        case FilterCategory::character:
            switch (shape.type)
            {
                case FilterType::formant:
                    for (int band = 0; band < filterFormantBands; ++band)
                    {
                        auto hz = 0.0f, gain = 0.0f;
                        filterFormantAt(shape.cutoff, co.second, band, hz, gain);
                        const auto i = static_cast<size_t>(band);
                        co.formantG[i] = filterPrewarp(hz, shape.sampleRate);
                        co.formantGain[i] = gain;
                    }
                    co.damping = filterFormantDamping(co.resonance);
                    break;

                case FilterType::ringMod:
                    co.step = filterClampHz(shape.cutoff, shape.sampleRate)
                            / static_cast<float>(shape.sampleRate);
                    // SPREAD at nothing is one modulator; past it a second one
                    // opens up to an octave above, which is Serum's Ring Mod
                    // and its Ring Mod x2 as one control rather than two types.
                    co.step2 = co.step * std::exp2(co.second);
                    break;

                case FilterType::sampleHold:
                    co.step = filterClampHz(shape.cutoff, shape.sampleRate)
                            / static_cast<float>(shape.sampleRate);
                    break;

                case FilterType::diffusor:
                    co.delaySamples = static_cast<float>(shape.sampleRate)
                                    / filterClampHz(shape.cutoff, shape.sampleRate);
                    co.stages = filterStagesOf(shape);
                    co.scatter = 0.5f + co.resonance * 0.45f;
                    break;

                case FilterType::scream:
                    co.g = filterPrewarp(shape.cutoff, shape.sampleRate);
                    // FEED does not add a second loop: it takes the damping out
                    // from under the one that is already there, until the
                    // filter is oscillating and the saturation on the resonant
                    // path is the only thing holding the level. That is what
                    // screaming is, and it means the response stays exactly
                    // the low pass the display draws.
                    co.damping = juce::jmax(0.02f, co.damping / (1.0f + co.second * 6.0f));
                    co.heat = 1.0f;
                    break;

                case FilterType::reverb:
                    co.delaySamples = static_cast<float>(shape.sampleRate) / filterCombHz(shape);
                    co.loopPole = filterPoleOf(filterPrewarp(filterDampHz(shape), shape.sampleRate));
                    co.loopGain = 0.5f + co.resonance * 0.47f;
                    co.stages = filterDiffuseStages;
                    co.scatter = 0.62f;
                    break;

                default:
                    break;
            }
            break;
    }
    return co;
}

// --- The pieces --------------------------------------------------------------

// The five responses one state-variable filter offers. The taps are read after
// the state is advanced, so `low` and `band` are this sample's outputs; `input`
// is carried because a peak is the signal with a bell added to it rather than
// a band taken off it.
struct FilterSvfOut
{
    float input = 0.0f, low = 0.0f, band = 0.0f, high = 0.0f;
};

// One pass, with `low` and `band` the two integrators' states rather than their
// outputs. That distinction is the whole of this function and it is not a
// nicety: a trapezoidal integrator's output is its state plus half its step and
// its state advances by the whole step, so storing the output as the state
// halves the gain of both integrators and moves the corner about an octave
// below the frequency the knob says.
//
// Forge did exactly that until the response checks in ForgeTestsFilter.cpp
// went in and measured the rendered gain against the drawn curve. At a corner
// of 700 Hz the filter was passing 0.196 of a 700 Hz tone where the analogue
// prototype passes 0.500, and its resonant peak sat at 350 Hz. Corrected, the
// two agree to under a per cent across the band and at every resonance — which
// is what makes the display worth drawing, and what makes a dual filter's two
// corners land where FREQ says they do.
//
// The resonance feeds back from the band-pass *output*, which is `g` ahead of
// its state; that is where the `2R + g` comes from, and leaving it at `2R` is a
// two per cent error in the damping at a low corner and much more at a high
// one.
inline FilterSvfOut filterSvf(float input, float& low, float& band,
                              float g, float damping, float heat)
{
    // The resonant path. FAT saturates it, which rounds a sharp peak off and
    // puts harmonics where it was; at nothing it is the term itself.
    auto fed = (2.0f * damping + g) * band;
    if (heat > 0.0f) fed += heat * (std::tanh(fed) - fed);
    const auto high = (input - fed - low) / (1.0f + 2.0f * damping * g + g * g);
    const auto bandStep = g * high;
    const auto bandOut = bandStep + band;
    band = bandOut + bandStep;
    const auto lowStep = g * bandOut;
    const auto lowOut = lowStep + low;
    low = lowOut + lowStep;
    return {input, lowOut, bandOut, high};
}

inline float filterTapOf(const FilterSvfOut& svf, FilterTap tap)
{
    switch (tap)
    {
        case FilterTap::high:  return svf.high;
        case FilterTap::band:  return svf.band;
        case FilterTap::notch: return svf.low + svf.high;
        case FilterTap::peak:  return svf.input + svf.band;
        case FilterTap::low:   break;
    }
    return svf.low;
}

// One pole of a ladder, in the form whose state is its own output.
inline float filterPoleStep(float& z, float input, float pole)
{
    const auto v = (input - z) * pole;
    const auto out = v + z;
    z = out + v;
    return out;
}

// A one-pole all-pass: flat in magnitude, and the phase is the whole point of
// it. Built out of the low pass above rather than written again, because two
// low passes minus the signal is exactly an all-pass and one of them is
// already here.
inline float filterAllpassStep(float& z, float input, float pole)
{
    return 2.0f * filterPoleStep(z, input, pole) - input;
}

// A read back into a delay line, interpolated, so sweeping the cutoff glides
// the comb instead of stepping it. `write` is the slot the next sample goes
// in, so a delay of one is the sample written last time.
inline float filterDelayRead(const float* line, int mask, int write, float delay)
{
    const auto held = juce::jlimit(1.0f, static_cast<float>(mask - 1), delay);
    const auto whole = static_cast<int>(held);
    const auto fraction = held - static_cast<float>(whole);
    const auto near = line[static_cast<size_t>((write - whole) & mask)];
    const auto far = line[static_cast<size_t>((write - whole - 1) & mask)];
    return near + (far - near) * fraction;
}

// A Schroeder all-pass: a delay with the signal fed round it. Flat in
// magnitude, and what it does instead is spread a transient out in time.
inline float filterDiffuseStep(FilterDelays& delays, int stage, float input,
                               float delay, float coefficient)
{
    const auto i = static_cast<size_t>(stage);
    auto& write = delays.diffuseWrite[i];
    auto* line = delays.diffuse[i].data();
    const auto delayed = filterDelayRead(line, delays.diffuseMask, write, delay);
    const auto fed = input + coefficient * delayed;
    line[static_cast<size_t>(write)] = fed;
    write = (write + 1) & delays.diffuseMask;
    return delayed - coefficient * fed;
}

// The stage delays, shortest last. Mutually unrelated lengths rather than a
// constant ratio: equal ratios put every stage's own comb on the same
// harmonics and the chain rings on one note instead of scattering.
inline float filterDiffuseSpread(int stage)
{
    switch (stage)
    {
        case 1: return 0.68f;
        case 2: return 0.43f;
        case 3: return 0.29f;
        default: break;
    }
    return 1.0f;
}

inline float filterWrapPhase(float phase)
{
    return phase - std::floor(phase);
}

// --- One sample ---------------------------------------------------------------

// Any type, one sample, one channel. The switch is on the same type for every
// voice and every sample of a block, so it predicts perfectly; what it saves
// is a second copy of the state-variable filter per family.
inline float filterSample(FilterState& state, FilterDelays& delays, float input,
                          const FilterCoefficients& co)
{
    switch (co.category)
    {
        case FilterCategory::basic:
        {
            const auto svf = filterSvf(input, state.low, state.band, co.g, co.damping, co.heat);
            return filterTapOf(svf, co.taps[0]);
        }

        case FilterCategory::dual:
        {
            const auto first = filterSvf(input, state.low, state.band, co.g, co.damping, 0.0f);
            const auto between = filterTapOf(first, co.taps[0]);
            const auto second = filterSvf(between, state.low2, state.band2, co.g2, co.damping, 0.0f);
            return filterTapOf(second, co.taps[1]);
        }

        case FilterCategory::morph:
        {
            const auto svf = filterSvf(input, state.low, state.band, co.g, co.damping, 0.0f);
            return co.morph[0] * filterTapOf(svf, co.taps[0])
                 + co.morph[1] * filterTapOf(svf, co.taps[1])
                 + co.morph[2] * filterTapOf(svf, co.taps[2]);
        }

        case FilterCategory::analog:
        {
            // The saturator is inside the loop, not in front of it. That is
            // the difference between a ladder being driven and a ladder with a
            // distortion before it: in the loop it bends the resonance, which
            // is what stops it running away and what moves the corner when the
            // level goes up.
            //
            // It is never skipped, whatever FAT is set to. Four poles with a
            // feedback of four is a ladder at the edge of oscillation, and with
            // nothing bounding the path round them it does not sit at that edge
            // — it runs away, and a corner near Nyquist gets there in a few
            // hundred samples. FAT says how *hard* the path saturates; that it
            // saturates at all is what keeps the filter a filter.
            const auto fed = std::tanh(state.feedback * (1.0f + co.heat * 5.0f));
            auto x = input * co.makeUp - co.feedback * fed;
            if (co.type == FilterType::acid)
            {
                // A diode conducts one way. Leaning the curve is what puts the
                // even harmonics in that a transistor ladder does not have,
                // and it is most of why this family squelches.
                x = std::tanh(x + 0.22f * x * std::abs(x)) ;
            }
            else if (co.type == FilterType::dirtyLadder)
            {
                x = std::tanh(x * 1.6f);
            }
            for (int stage = 0; stage < co.stages; ++stage)
                x = filterPoleStep(state.ladder[static_cast<size_t>(stage)], x, co.pole);
            state.feedback = x;
            // What the EMS keeps that the ladders do not: a little of the
            // signal past the poles, so the body is still there with the
            // resonance up rather than swallowed by it.
            if (co.type == FilterType::ems) return x + 0.12f * input;
            return x;
        }

        case FilterCategory::resonator:
        {
            if (co.type == FilterType::phaser)
            {
                auto x = input + co.feedback * state.phaserFeedback;
                for (int stage = 0; stage < co.stages; ++stage)
                    x = filterAllpassStep(state.phase[static_cast<size_t>(stage)], x, co.allpass);
                state.phaserFeedback = x;
                // Against the dry signal, because an all-pass on its own does
                // nothing to the magnitude at all. The nulls are the two
                // cancelling.
                return 0.5f * (input + x);
            }

            if (!delays.ready()) return input;
            const auto delayed = filterDelayRead(delays.comb.data(), delays.combMask,
                                                 delays.combWrite, co.delaySamples);
            const auto damped = filterPoleStep(state.loop, delayed, co.loopPole);
            const auto fed = input + co.loopGain * damped;
            delays.comb[static_cast<size_t>(delays.combWrite)] = fed;
            delays.combWrite = (delays.combWrite + 1) & delays.combMask;
            // A comb is the loop, normalised so its peaks stay near unity
            // however tight it is; a flanger is the loop against the dry, which
            // is what turns its peaks into nulls.
            if (co.type == FilterType::comb) return fed * (1.0f - co.loopGain);
            return 0.5f * (input + damped);
        }

        case FilterCategory::character:
            switch (co.type)
            {
                case FilterType::formant:
                {
                    auto sum = 0.0f;
                    for (int band = 0; band < filterFormantBands; ++band)
                    {
                        const auto i = static_cast<size_t>(band);
                        const auto svf = filterSvf(input, state.formantLow[i], state.formantBand[i],
                                                   co.formantG[i], co.damping, 0.0f);
                        // Normalised by the damping so a resonator's own peak
                        // is about unity whatever RES is set to, and the vowel
                        // gains are what balance the three against each other.
                        sum += co.formantGain[i] * svf.band * 2.0f * co.damping;
                    }
                    return sum;
                }

                case FilterType::ringMod:
                {
                    state.ringPhase = filterWrapPhase(state.ringPhase + co.step);
                    auto modulator = std::sin(state.ringPhase * juce::MathConstants<float>::twoPi);
                    if (co.second > 0.0f)
                    {
                        state.ringPhase2 = filterWrapPhase(state.ringPhase2 + co.step2);
                        modulator = 0.5f * (modulator
                            + std::sin(state.ringPhase2 * juce::MathConstants<float>::twoPi));
                    }
                    return input * modulator;
                }

                case FilterType::sampleHold:
                    state.holdPhase += co.step;
                    if (state.holdPhase >= 1.0f)
                    {
                        state.holdPhase = filterWrapPhase(state.holdPhase);
                        state.held = input;
                    }
                    // DIFF crosses from the held steps to the difference
                    // between them and the signal, which is Serum's SampHold
                    // and its SampHold- as one control.
                    return state.held - co.second * input;

                case FilterType::diffusor:
                {
                    if (!delays.ready()) return input;
                    auto x = input;
                    for (int stage = 0; stage < co.stages; ++stage)
                        x = filterDiffuseStep(delays, stage, x,
                                              co.delaySamples * filterDiffuseSpread(stage),
                                              co.scatter);
                    return x;
                }

                case FilterType::scream:
                {
                    const auto svf = filterSvf(input, state.low, state.band,
                                               co.g, co.damping, co.heat);
                    return svf.low;
                }

                case FilterType::reverb:
                {
                    if (!delays.ready()) return input;
                    const auto delayed = filterDelayRead(delays.comb.data(), delays.combMask,
                                                         delays.combWrite, co.delaySamples);
                    const auto damped = filterPoleStep(state.loop, delayed, co.loopPole);
                    auto x = input + co.loopGain * damped;
                    // The all-passes are inside the loop, which is what makes
                    // this a reverb rather than a comb: each time round, the
                    // energy is scattered again instead of arriving as one
                    // repeat.
                    for (int stage = 0; stage < co.stages; ++stage)
                        x = filterDiffuseStep(delays, stage, x,
                                              co.delaySamples * filterDiffuseSpread(stage),
                                              co.scatter);
                    delays.comb[static_cast<size_t>(delays.combWrite)] = x;
                    delays.combWrite = (delays.combWrite + 1) & delays.combMask;
                    return x * (1.0f - co.loopGain);
                }

                default:
                    break;
            }
            break;
    }
    return input;
}

// --- The response -------------------------------------------------------------
//
// The gain each type has at one frequency, as a plain multiplier. Analogue
// prototypes: Core prewarps its corners, so the knee sits at the frequency the
// knob says whatever the sample rate is, and carrying the warp through the rest
// of the curve would move it by less than a pixel across the band the panel
// draws.
//
// Three of these types are not linear and so have no transfer function at all.
// Rather than draw a shape none of them has, each returns the truest thing that
// can be said about it: unity for the ring modulator and the diffusor, because
// neither takes anything out, and a hold's own sinc for the sample and hold,
// because that genuinely is what holding a value does to a spectrum. The
// reverb returns the envelope its damping puts on the tail rather than the
// teeth of the loop, because the all-passes inside that loop scatter the teeth
// and drawing them where a plain comb would put them would be a lie.

// One tap of the state-variable prototype, as a complex gain, so taps can be
// blended and chained the way the engine blends and chains them.
inline std::complex<float> filterTapResponse(FilterTap tap, float x, float damping)
{
    const std::complex<float> s {0.0f, x};
    const auto d = std::complex<float> {1.0f - x * x, 2.0f * damping * x};
    switch (tap)
    {
        case FilterTap::high:  return -x * x / d;
        case FilterTap::band:  return s / d;
        case FilterTap::notch: return std::complex<float> {1.0f - x * x, 0.0f} / d;
        case FilterTap::peak:  return (d + s) / d;
        case FilterTap::low:   break;
    }
    return 1.0f / d;
}

// One pole of a ladder chain, and the chain itself.
inline std::complex<float> filterLadderResponse(float x, int stages, float feedback, float makeUp)
{
    const auto pole = 1.0f / std::complex<float> {1.0f, x};
    auto chain = std::complex<float> {1.0f, 0.0f};
    for (int i = 0; i < stages; ++i) chain *= pole;
    return makeUp * chain / (1.0f + feedback * chain);
}

inline float filterMagnitude(const FilterShape& shape, float hz)
{
    const auto co = filterCoefficientsFor(shape);
    const auto frequency = juce::jmax(1.0e-4f, hz);
    const auto x = frequency / juce::jmax(1.0e-4f, shape.cutoff);

    switch (co.category)
    {
        case FilterCategory::basic:
            return std::abs(filterTapResponse(co.taps[0], x, co.damping));

        case FilterCategory::dual:
        {
            const auto x2 = frequency / juce::jmax(1.0e-4f, filterSecondHz(shape));
            return std::abs(filterTapResponse(co.taps[0], x, co.damping)
                            * filterTapResponse(co.taps[1], x2, co.damping));
        }

        case FilterCategory::morph:
            return std::abs(co.morph[0] * filterTapResponse(co.taps[0], x, co.damping)
                          + co.morph[1] * filterTapResponse(co.taps[1], x, co.damping)
                          + co.morph[2] * filterTapResponse(co.taps[2], x, co.damping));

        case FilterCategory::analog:
        {
            const auto chain = filterLadderResponse(x, co.stages, co.feedback, co.makeUp);
            if (shape.type == FilterType::ems) return std::abs(chain + 0.12f);
            return std::abs(chain);
        }

        case FilterCategory::resonator:
        {
            if (shape.type == FilterType::phaser)
            {
                const auto allpass = std::complex<float> {1.0f, -x} / std::complex<float> {1.0f, x};
                auto chain = std::complex<float> {1.0f, 0.0f};
                for (int i = 0; i < co.stages; ++i) chain *= allpass;
                return std::abs(0.5f * (1.0f + chain / (1.0f - co.feedback * chain)));
            }
            // The loop: a delay of one period of the comb's own frequency, and
            // the one-pole damping it passes through on the way round. Both as
            // complex gains, so the peaks land where the damper's phase lag
            // actually puts them rather than where the delay alone would.
            const auto turns = -juce::MathConstants<float>::twoPi * frequency / filterCombHz(shape);
            const auto delay = std::polar(1.0f, turns);
            const auto damper = 1.0f / std::complex<float> {1.0f, frequency / filterDampHz(shape)};
            const auto loop = co.loopGain * damper * delay;
            if (shape.type == FilterType::comb)
                return std::abs((1.0f - co.loopGain) / (1.0f - loop));
            return std::abs(0.5f * (1.0f + damper * delay / (1.0f - loop)));
        }

        case FilterCategory::character:
            switch (shape.type)
            {
                case FilterType::formant:
                {
                    std::complex<float> sum {0.0f, 0.0f};
                    for (int band = 0; band < filterFormantBands; ++band)
                    {
                        auto centre = 0.0f, gain = 0.0f;
                        filterFormantAt(shape.cutoff, co.second, band, centre, gain);
                        sum += gain * 2.0f * co.damping
                             * filterTapResponse(FilterTap::band,
                                                 frequency / juce::jmax(1.0e-4f, centre),
                                                 co.damping);
                    }
                    return std::abs(sum);
                }

                case FilterType::sampleHold:
                {
                    // A zero-order hold's own response: flat at the bottom,
                    // with a null at every multiple of the rate it is held at.
                    const auto at = juce::MathConstants<float>::pi * frequency
                                  / juce::jmax(1.0e-4f, shape.cutoff);
                    const auto zoh = at < 1.0e-4f ? 1.0f : std::abs(std::sin(at) / at);
                    return juce::jmap(co.second, zoh, 1.0f);
                }

                case FilterType::scream:
                    return std::abs(filterTapResponse(FilterTap::low, x, co.damping));

                case FilterType::reverb:
                {
                    const auto damper = 1.0f
                        / std::abs(std::complex<float> {1.0f, frequency / filterDampHz(shape)});
                    return (1.0f - co.loopGain) / juce::jmax(1.0e-4f, 1.0f - co.loopGain * damper);
                }

                case FilterType::ringMod:
                case FilterType::diffusor:
                default:
                    break;
            }
            break;
    }
    return 1.0f;
}

// Whether the curve for this type is its own transfer function, or the truest
// stand-in there is for a type that has none. The display says which, because
// a flat line on the diffusor means "this does not touch the magnitude" and
// the same flat line on a broken low pass would mean something quite different.
inline bool filterHasResponse(FilterType type)
{
    return type != FilterType::ringMod && type != FilterType::diffusor;
}
}
