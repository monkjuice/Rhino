#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

// Warp: what an oscillator does to its own table on the way out of it.
//
// The shape of this file follows the Serum 2 manual's warp chapter (pp. 49-54):
// a mode chosen from a menu grouped by category, and one knob setting how deep
// that mode goes. Each oscillator carries two of the pair, applied in order, so
// a patch can bend a wave and then fold it.
//
// Everything here is a pure function of a phase, a depth and — for the three
// modes that genuinely need one — a few floats of state. Nothing allocates and
// nothing here knows what a Wavetable is: a warp is handed a `read` that turns
// a phase into a sample, which is what lets one implementation serve the voice
// (reading a band-limited table) and the panel (drawing the table as authored).
//
// --- On aliasing ---------------------------------------------------------------
//
// Bending a phase or folding a sample makes harmonics that were not in the
// table, and Forge does not oversample. What it does instead is read a duller
// copy of the table when a warp is going to brighten it: every mode declares
// how much extra bandwidth it asks for at the depth it is set to, and the
// oscillator picks its band-limited level for a note that much higher. That is
// the same trade Serum makes before it oversamples, and it is why a deep SYNC
// sounds softer than the ratio alone would suggest. It reduces aliasing rather
// than removing it; the modes that make a step in the waveform — FLIP and
// QUANTIZE — are the ones where that shows.
namespace rhino::forge
{
// How many warp stages one oscillator has. Two, as Serum has: enough to bend a
// wave and then colour it, which is the pair of things a patch actually asks
// for, and few enough that the row fits under the knobs it belongs to.
inline constexpr int warpSlots = 2;

// The groups the menu is built from, in the order it shows them, which is the
// order the manual's own menu figure has. A category is a fact about a mode
// rather than about the panel, so it lives here beside the modes.
//
// FM and PD are separate families and not two names for one thing. FM moves the
// carrier's *frequency*, so a deep setting bends the note; PD moves its *phase*,
// so the note stays exactly where it was however deep it goes. The manual draws
// the same line -- "this is similar to FM except that the phase is modulated
// instead of the frequency" -- and they sound nothing alike.
enum class WarpCategory { off, sync, alt, filter, distortion, fm, pd, am, rm };
inline constexpr int warpCategoryCount = 9;

inline const char* warpCategoryName(WarpCategory category)
{
    switch (category)
    {
        case WarpCategory::sync:       return "SYNC";
        case WarpCategory::alt:        return "ALT WARP";
        case WarpCategory::filter:     return "FILTER";
        case WarpCategory::distortion: return "DISTORTION";
        case WarpCategory::fm:         return "FM";
        case WarpCategory::pd:         return "PD";
        case WarpCategory::am:         return "AM";
        case WarpCategory::rm:         return "RM";
        case WarpCategory::off:        break;
    }
    return "OFF";
}

// Where a mode gets the signal it modulates with. Forge has three: the other
// oscillator, the sub, and the noise -- plus a stage's own output, which only
// PD offers, exactly as the manual's list does. Serum names its two filters as
// sources as well; Forge has one filter and it sits downstream of both
// oscillators, so pointing an oscillator at it would be a loop.
enum class WarpSource { none, otherOscillator, sub, noise, self };

// Appended to, never inserted into: a warp mode is stored as an index into this
// list, so every index already written into a preset has to keep meaning what
// it meant — the same rule the modulation sources and destinations live under.
enum class WarpMode
{
    off,
    sync,
    // Alt Warp: the phase-domain family. All of these move where in the cycle
    // the table is read, which is what "warp" meant before it meant anything
    // else.
    bendUp, bendDown, bendBoth, pwm, asym, flip, mirror, quantize, oddEven,
    // Filter: fewer harmonics rather than more. Pitch-tracked, so the colour
    // stays where it was put as the note moves.
    lowPass, highPass, bandPass,
    // Distortion: the sample-domain family, shaping what came out of the table
    // rather than where it was read.
    tube, softClip, hardClip, diode, linearFold, sineFold, rectify, tapeSat,
    // PD: phase distortion from another source in the voice. These four were
    // built first and were called FM at the time, which was the wrong name for
    // them -- they move the phase, and the manual calls that PD. The names
    // changed and the places did not, because a mode is stored as an index.
    pdOsc, pdSub, pdNoise, pdSelf,
    // FM proper: the carrier's frequency rather than its phase. Linear keeps
    // the note where it was put; exponential does not, and is the brighter and
    // harsher of the two for exactly that reason.
    fmOsc, fmSub, fmNoise,
    fmExpOsc, fmExpSub, fmExpNoise,
    // AM and RM: the carrier's amplitude. AM keeps it and rides it; RM replaces
    // it outright, which is what puts the carrier's own pitch out of the sound.
    amOsc, amSub, amNoise,
    rmOsc, rmSub, rmNoise,
};

inline constexpr int warpModeCount = 38;

struct WarpInfo
{
    const char* name;
    WarpCategory category;
};

inline const std::array<WarpInfo, warpModeCount>& warpModes()
{
    static const std::array<WarpInfo, warpModeCount> table {{
        {"OFF", WarpCategory::off},
        {"SYNC", WarpCategory::sync},

        {"BEND +", WarpCategory::alt},   {"BEND -", WarpCategory::alt},
        {"BEND +/-", WarpCategory::alt}, {"PWM", WarpCategory::alt},
        {"ASYM", WarpCategory::alt},     {"FLIP", WarpCategory::alt},
        {"MIRROR", WarpCategory::alt},   {"QUANTIZE", WarpCategory::alt},
        {"ODD/EVEN", WarpCategory::alt},

        {"LPF", WarpCategory::filter},   {"HPF", WarpCategory::filter},
        {"BPF", WarpCategory::filter},

        {"TUBE", WarpCategory::distortion},      {"SOFT CLIP", WarpCategory::distortion},
        {"HARD CLIP", WarpCategory::distortion}, {"DIODE", WarpCategory::distortion},
        {"LIN FOLD", WarpCategory::distortion},  {"SINE FOLD", WarpCategory::distortion},
        {"RECTIFY", WarpCategory::distortion},   {"TAPE SAT", WarpCategory::distortion},

        {"PD OSC", WarpCategory::pd},   {"PD SUB", WarpCategory::pd},
        {"PD NOISE", WarpCategory::pd}, {"PD SELF", WarpCategory::pd},

        {"FM OSC", WarpCategory::fm},     {"FM SUB", WarpCategory::fm},
        {"FM NOISE", WarpCategory::fm},
        {"FM EXP OSC", WarpCategory::fm}, {"FM EXP SUB", WarpCategory::fm},
        {"FM EXP NOISE", WarpCategory::fm},

        {"AM OSC", WarpCategory::am},   {"AM SUB", WarpCategory::am},
        {"AM NOISE", WarpCategory::am},

        {"RM OSC", WarpCategory::rm},   {"RM SUB", WarpCategory::rm},
        {"RM NOISE", WarpCategory::rm},
    }};
    return table;
}

// Which signal a mode modulates with, or nothing for the modes that read only
// the table. One answer for all four families, so a source added to one of them
// is a source added to every one that wants it.
inline WarpSource warpSourceOf(WarpMode mode)
{
    switch (mode)
    {
        case WarpMode::pdOsc:
        case WarpMode::fmOsc:
        case WarpMode::fmExpOsc:
        case WarpMode::amOsc:
        case WarpMode::rmOsc:      return WarpSource::otherOscillator;
        case WarpMode::pdSub:
        case WarpMode::fmSub:
        case WarpMode::fmExpSub:
        case WarpMode::amSub:
        case WarpMode::rmSub:      return WarpSource::sub;
        case WarpMode::pdNoise:
        case WarpMode::fmNoise:
        case WarpMode::fmExpNoise:
        case WarpMode::amNoise:
        case WarpMode::rmNoise:    return WarpSource::noise;
        case WarpMode::pdSelf:     return WarpSource::self;
        default: break;
    }
    return WarpSource::none;
}

inline WarpMode warpModeOf(float value)
{
    return static_cast<WarpMode>(juce::jlimit(0, warpModeCount - 1, juce::roundToInt(value)));
}

inline const WarpInfo& warpInfo(WarpMode mode)
{
    return warpModes()[static_cast<size_t>(juce::jlimit(0, warpModeCount - 1, static_cast<int>(mode)))];
}

inline const char* warpModeName(int mode)
{
    return warpModes()[static_cast<size_t>(juce::jlimit(0, warpModeCount - 1, mode))].name;
}

inline WarpCategory warpCategoryOf(WarpMode mode) { return warpInfo(mode).category; }

// Whether a mode moves the carrier's frequency rather than reading its table
// somewhere else. These are the only modes that reach the phase increment, and
// therefore the only ones whose depth knob can move the note.
inline bool warpBendsPitch(WarpMode mode) { return warpCategoryOf(mode) == WarpCategory::fm; }

inline bool warpIsExponential(WarpMode mode)
{
    return mode == WarpMode::fmExpOsc || mode == WarpMode::fmExpSub || mode == WarpMode::fmExpNoise;
}

// Whether the depth knob beside a mode does anything. Only OFF leaves it with
// nothing to set, and the panel greys it there rather than hiding it: the knob
// comes back the moment the field next to it moves.
inline bool warpUsesDepth(WarpMode mode) { return mode != WarpMode::off; }

// Where on the knob a mode does nothing.
//
// Most modes do nothing at nothing, and go one way as the knob opens. Four of
// them go both ways instead, so their nothing is in the middle -- which is what
// the manual means when it says a setting of twelve o'clock on the warp knob
// represents no change. Double-clicking the depth returns it here rather than
// to the parameter's own default, so a mode's neutral is somewhere the hand can
// actually get back to.
//
// MIRROR answers the middle too and is the one mode that is never neutral: it
// folds the cycle whatever the knob says. The middle is where it stops bending
// what it has folded, which is as close to nothing as it has.
inline float warpNeutralDepth(WarpMode mode)
{
    switch (mode)
    {
        case WarpMode::bendBoth:
        case WarpMode::asym:
        case WarpMode::mirror:
        case WarpMode::oddEven: return 0.5f;
        default: break;
    }
    return 0.0f;
}

// Which modes reshape the waveform in a way the panel can draw. The three
// filters run against time and the four FM modes read something outside the
// oscillator, so neither family is a picture the display can honestly show —
// exactly the distinction the Serum manual draws when it says the 2D view shows
// Sync, Alt Warp and Distortion.
inline bool warpShapesWaveform(WarpMode mode)
{
    const auto category = warpCategoryOf(mode);
    return category == WarpCategory::sync || category == WarpCategory::alt
        || category == WarpCategory::distortion;
}

// What a mode reads besides the table, so the oscillator only works out the
// sources the patch actually asks for.
inline bool warpReadsOtherOscillator(WarpMode mode)
{
    return warpSourceOf(mode) == WarpSource::otherOscillator;
}
inline bool warpReadsSub(WarpMode mode) { return warpSourceOf(mode) == WarpSource::sub; }
inline bool warpReadsNoise(WarpMode mode) { return warpSourceOf(mode) == WarpSource::noise; }

// --- The state three of the modes keep ---------------------------------------
//
// A filter is a filter: it has to remember what it was given. PD SELF feeds a
// stage its own last output. Everything else here is a pure function of the
// phase, and reads none of this.
struct WarpState
{
    // The filter modes' two poles.
    float low = 0.0f, band = 0.0f;
    // The last two samples PD SELF put out. Two rather than one because the
    // average of the pair is what goes back round -- see the mode itself.
    float last = 0.0f, before = 0.0f;
};

// One warp slot, resolved for the sample about to be rendered: which mode, how
// deep, and whatever that mode needs worked out once for the whole unison stack
// rather than once per member of it — which is what keeps the two exponentials
// a filter mode costs out of the inner loop.
struct WarpStage
{
    WarpMode mode = WarpMode::off;
    float amount = 0.0f;
    float g = 0.0f, g2 = 0.0f;
    float modulator = 0.0f;
};

// --- The arithmetic ----------------------------------------------------------

inline float warpFrac(float x) noexcept { return x - std::floor(x); }

// A monotone bend of the unit interval, with no transcendental in it: at k = 0
// it is the identity, positive k pulls the curve below the diagonal and
// negative k above it. Both ends are pinned, so a bent cycle is still exactly
// one cycle and the wave still joins up with itself.
inline float warpBend(float u, float k) noexcept
{
    const auto clamped = juce::jlimit(0.0f, 1.0f, u);
    return clamped * (1.0f - k) / (1.0f - k * clamped);
}

// How steep that bend gets, which is how much bandwidth it asks for: the
// derivative at whichever end of the interval is the steep one.
inline float warpBendSlope(float k) noexcept
{
    const auto magnitude = juce::jlimit(0.0f, 0.95f, std::abs(k));
    return 1.0f / (1.0f - magnitude);
}

// The strongest bend a knob reaches. Short of 1 because the curve is a ratio
// and 1 is where its denominator runs out.
inline constexpr float warpBendLimit = 0.95f;

// A knob whose middle is "no change", for the modes the manual describes that
// way: a setting of 12 o'clock is neutral and either side of it bends the
// opposite direction.
inline float warpSignedAmount(float amount) noexcept
{
    return (juce::jlimit(0.0f, 1.0f, amount) - 0.5f) * 2.0f * warpBendLimit;
}

// PWM's break point: where the first half of the cycle ends. Half is neutral.
inline float warpDuty(float amount) noexcept
{
    return 0.5f - juce::jlimit(0.0f, 1.0f, amount) * 0.45f;
}

// How many steps QUANTIZE holds the read at. A knob at nothing leaves enough of
// them that the staircase is below the table's own resolution.
inline float warpSteps(float amount) noexcept
{
    const auto open = 1.0f - juce::jlimit(0.0f, 1.0f, amount);
    return std::floor(2.0f + open * open * 254.0f);
}

// How hard a distortion mode is driven. One is clean, and the top of the knob
// is enough to take a sine well into a square.
inline float warpDrive(float amount) noexcept
{
    return 1.0f + juce::jlimit(0.0f, 1.0f, amount) * 15.0f;
}

// How far FM moves the read, in cycles. Four at the top, which is deep enough
// for the metallic end of the range and shallow enough that the first half of
// the knob is still musical.
//
// Feedback is on a scale of its own, a sixteenth of that. A stage reading its
// own last output is a loop, and a loop deep enough to push the read a whole
// cycle does not settle on a waveform at all -- it goes to noise, and takes the
// note with it. A quarter of a cycle at the top of the knob runs from a saw
// through a hard edge and only reaches chaos at the very end, which is the
// range feedback is actually reached for.
inline constexpr float warpFeedbackScale = 0.0625f;

inline float warpFmIndex(WarpMode mode, float amount) noexcept
{
    const auto cycles = juce::jlimit(0.0f, 1.0f, amount) * 4.0f;
    return mode == WarpMode::pdSelf ? cycles * warpFeedbackScale : cycles;
}

// How far linear FM may push the carrier's frequency, as a multiple of the note.
// Four means the top of the knob reaches five times the note at the peak of the
// modulator and zero at its trough -- which is where the clamp the manual
// mentions comes in.
inline constexpr float warpFmDepth = 4.0f;

// How far exponential FM may push it, in octaves either way. Four is enough for
// the rapid sweeps the manual describes as brighter and harsher, and it is the
// reason exponential does not hold the note where linear does: the mean of an
// exponential sweep is not the pitch it swept from.
inline constexpr float warpFmOctaves = 4.0f;

// What an FM stage is doing to the carrier's frequency this sample, as a
// multiple of it. One for every mode that is not FM, so the two stages simply
// multiply and nothing downstream needs a special case.
//
// Linear clamps at zero rather than running the frequency negative. Serum's
// does the same and says why: a clamp is the traditional FM every classic
// digital synth had, and it is the sound people reach for. PD is where a
// modulation that genuinely passes through zero lives, because a phase pushed
// backwards simply reads backwards.
inline float warpPitchFactor(WarpMode mode, float amount, float modulator) noexcept
{
    if (!warpBendsPitch(mode)) return 1.0f;
    const auto depth = juce::jlimit(0.0f, 1.0f, amount);
    if (warpIsExponential(mode))
        return std::exp2(depth * warpFmOctaves * modulator);
    return juce::jmax(0.0f, 1.0f + depth * warpFmDepth * modulator);
}

// Two saturators with genuinely different knees, so the modes built on them do
// not all sound like one. The first is asymptotic and never quite reaches full
// scale; the second rolls off more gently and is what the tape mode wants.
inline float warpRational(float x) noexcept { return x / (1.0f + std::abs(x)); }
inline float warpAlgebraic(float x) noexcept { return x / std::sqrt(1.0f + x * x); }

// A triangle fold: the signal reflected back on itself every time it leaves
// full scale, which is what a wavefolder is. Period four in its input, so a
// drive of four has folded it twice.
inline float warpFold(float x) noexcept
{
    return 1.0f - 2.0f * std::abs(2.0f * warpFrac(0.25f * x + 0.25f) - 1.0f);
}

// A one-pole coefficient for a cutoff that follows the note: the filter modes
// are described as filtering the waveform rather than the voice, so the colour
// has to stay where it was put as the pitch moves.
//
// Deliberately not clamped at the top. A one-pole cannot run away however high
// its corner is put -- the coefficient simply approaches one, which is the
// filter passing what it was given -- and clamping would have stopped the
// highest notes reaching the open end of the knob.
inline float warpFilterCoefficient(float cutoffHz, double sampleRate) noexcept
{
    return 1.0f - std::exp(-juce::MathConstants<float>::twoPi * juce::jmax(0.0f, cutoffHz)
                           / static_cast<float>(juce::jmax(1.0, sampleRate)));
}

// Which harmonic of the note a filter mode sits on. Seven octaves of travel,
// which reaches from below the fundamental to past where a table has anything
// left.
inline float warpFilterHarmonic(float amount) noexcept
{
    return std::pow(2.0f, juce::jlimit(0.0f, 1.0f, amount) * 7.0f);
}

// The waveshapers, at full strength. What the depth knob does with them is the
// next function's business: every one of these is what the mode sounds like
// with the knob all the way round, and none of them is the identity.
inline float warpShape(WarpMode mode, float amount, float x)
{
    const auto drive = warpDrive(amount);
    switch (mode)
    {
        // Valve: a soft knee that is not the same either side of zero, which is
        // where its even harmonics come from.
        case WarpMode::tube:
        {
            const auto bias = 0.25f * amount;
            return (warpRational(drive * (x + bias)) - warpRational(drive * bias))
                 / warpRational(drive);
        }

        // A genuine soft clipper: exactly linear at small signals, flat once
        // the knee is reached.
        case WarpMode::softClip:
        {
            const auto u = juce::jlimit(-1.0f, 1.0f, drive * x);
            return 1.5f * u - 0.5f * u * u * u;
        }

        case WarpMode::hardClip:
            return juce::jlimit(-1.0f, 1.0f, drive * x);

        // Diode clipping, asymmetric because the two halves of the pair it
        // comes from are not the same part.
        case WarpMode::diode:
        {
            const auto side = x >= 0.0f ? drive : drive * 0.4f;
            const auto sign = x >= 0.0f ? 1.0f : -1.0f;
            return sign * (1.0f - 1.0f / (1.0f + side * std::abs(x)))
                 / (1.0f - 1.0f / (1.0f + drive));
        }

        case WarpMode::linearFold:
            return warpFold(drive * x);

        // The same folding through a sine, which rounds every corner the
        // linear one leaves square.
        case WarpMode::sineFold:
            return std::sin(drive * x * juce::MathConstants<float>::halfPi);

        // One half of the wave flipped into the other. It leaves an offset
        // behind by construction; the oscillator blocks that once, after both
        // stages, rather than every mode having to worry about it.
        case WarpMode::rectify:
            return 2.0f * std::abs(x) - 1.0f;

        // Tape: the gentlest of the knees, and the only one whose approach to
        // full scale is gradual enough to hear as compression rather than as
        // clipping.
        case WarpMode::tapeSat:
            return warpAlgebraic(drive * x) / warpAlgebraic(drive);

        default: break;
    }
    return x;
}

// One stage worked out for this sample. `hz` is the note the oscillator is
// actually playing, which is what the filter modes track and what nothing else
// here reads.
inline WarpStage warpStageFor(float mode, float amount, float hz, double sampleRate)
{
    WarpStage stage;
    stage.mode = warpModeOf(mode);
    stage.amount = juce::jlimit(0.0f, 1.0f, amount);
    const auto note = juce::jmax(1.0f, hz);
    switch (stage.mode)
    {
        // The corner travels with the knob as well as the crossfade, so the
        // knob reads as "how much filtering" from both ends at once: a little
        // is a high corner barely blended in, a lot is the fundamental alone.
        case WarpMode::lowPass:
            stage.g = warpFilterCoefficient(note * warpFilterHarmonic(1.0f - stage.amount), sampleRate);
            break;
        case WarpMode::highPass:
            stage.g = warpFilterCoefficient(note * 0.25f * warpFilterHarmonic(stage.amount), sampleRate);
            break;
        // A band is the pair: everything under the upper corner, less
        // everything under the lower one. They move together, two octaves
        // apart, so the knob sweeps a band rather than widening one.
        case WarpMode::bandPass:
            stage.g = warpFilterCoefficient(note * 2.0f * warpFilterHarmonic(stage.amount), sampleRate);
            stage.g2 = warpFilterCoefficient(note * 0.5f * warpFilterHarmonic(stage.amount), sampleRate);
            break;
        default: break;
    }
    return stage;
}

// How much brighter than the note a stage is going to read, so the oscillator
// can pick a band-limited copy that can stand it. Capped, because past a point
// the copies have so little left in them that a deeper one costs tone without
// buying quiet.
inline constexpr float warpHeadroomCeiling = 16.0f;

inline float warpHeadroom(const WarpStage& stage)
{
    const auto a = stage.amount;
    switch (stage.mode)
    {
        case WarpMode::off:        return 1.0f;
        case WarpMode::sync:       return 1.0f + a * 7.0f;
        case WarpMode::bendUp:
        case WarpMode::bendDown:   return warpBendSlope(a * warpBendLimit);
        case WarpMode::bendBoth:
        case WarpMode::asym:       return warpBendSlope(warpSignedAmount(a));
        case WarpMode::pwm:        return 0.5f / juce::jmax(0.05f, warpDuty(a));
        // A step in the waveform is broadband whatever it was made from, so
        // there is a limit to what reading a duller copy buys. Enough to take
        // the edge off, and no more.
        case WarpMode::flip:       return 1.0f + a * 2.0f;
        case WarpMode::mirror:     return 2.0f * warpBendSlope(warpSignedAmount(a));
        case WarpMode::quantize:   return 2.0f;
        case WarpMode::oddEven:    return 1.0f;
        // Taking harmonics away needs no headroom at all.
        case WarpMode::lowPass:
        case WarpMode::highPass:
        case WarpMode::bandPass:   return 1.0f;
        case WarpMode::pdOsc:
        case WarpMode::pdSub:
        case WarpMode::pdNoise:
        case WarpMode::pdSelf:     return 1.0f + warpFmIndex(stage.mode, a) * 2.0f;
        // FM asks for the highest frequency it will reach, because that is
        // literally the rate the table is about to be read at.
        case WarpMode::fmOsc:
        case WarpMode::fmSub:
        case WarpMode::fmNoise:    return 1.0f + a * warpFmDepth;
        case WarpMode::fmExpOsc:
        case WarpMode::fmExpSub:
        case WarpMode::fmExpNoise: return std::exp2(a * warpFmOctaves);
        // A product of two signals is as wide as the two of them together.
        case WarpMode::amOsc:
        case WarpMode::amSub:
        case WarpMode::amNoise:
        case WarpMode::rmOsc:
        case WarpMode::rmSub:
        case WarpMode::rmNoise:    return 1.0f + a;
        default: break;
    }
    // Everything left is a waveshaper. Starting it from a duller copy does not
    // stop it making harmonics, but it does stop it making them out of ones
    // that were already near Nyquist.
    return 1.0f + a * 4.0f;
}

// How far into the master cycle SYNC crossfades to the cycle that is about to
// start. Without it the slave jumps from wherever it had reached to the top of
// the table on every master wrap, and that step is a click and a spray of
// aliasing. Two percent is short enough to stay a sync and long enough to
// join — which is the same control Serum exposes as the WARP Var fader.
inline constexpr float warpSyncWindow = 0.02f;

// --- Reading one stage --------------------------------------------------------
//
// `read` turns a phase into a sample. The voice hands in a band-limited table
// read; the panel hands in the table as authored; a second stage hands in the
// first stage. That last one is the whole reason this is a template: two stages
// chain by one calling the other, with no buffer between them.
//
// One consequence worth knowing: ODD/EVEN reads twice, so a filter stage
// underneath one runs at twice the rate and lands an octave above where its
// knob says. It is the only combination that does this, and it is a shift in
// colour rather than anything broken.
template <typename Read>
float warpRead(const WarpStage& stage, float phase, WarpState& state, Read&& read)
{
    const auto a = stage.amount;
    switch (stage.mode)
    {
        case WarpMode::off:
            break;

        // An internal oscillator running at a whole-number-ish multiple of the
        // note and restarting with it. The ratio is continuous rather than
        // stepped: the harmonious ratios the manual describes are the ones you
        // land on, not the only ones you can reach.
        case WarpMode::sync:
        {
            const auto ratio = 1.0f + a * 7.0f;
            const auto now = read(warpFrac(phase * ratio));
            if (phase <= 1.0f - warpSyncWindow) return now;
            const auto t = (phase - (1.0f - warpSyncWindow)) / warpSyncWindow;
            const auto smooth = t * t * (3.0f - 2.0f * t);
            // Where the slave will be once the master has wrapped, which is
            // what the window walks onto.
            const auto next = read(warpFrac((phase - 1.0f) * ratio));
            return now + (next - now) * smooth;
        }

        // Both halves of the duty cycle bent the same way, independently, which
        // is what makes this a pinch rather than a lean.
        case WarpMode::bendUp:
        case WarpMode::bendDown:
        case WarpMode::bendBoth:
        {
            const auto k = stage.mode == WarpMode::bendUp    ? a * warpBendLimit
                         : stage.mode == WarpMode::bendDown  ? -a * warpBendLimit
                                                             : warpSignedAmount(a);
            const auto half = phase < 0.5f ? 0.0f : 0.5f;
            const auto u = phase < 0.5f ? phase * 2.0f : phase * 2.0f - 1.0f;
            return read(half + 0.5f * warpBend(u, k));
        }

        // The classic pulse width: the first half of the wave given more or
        // less of the cycle than the second.
        case WarpMode::pwm:
        {
            const auto duty = warpDuty(a);
            return read(phase < duty ? phase * 0.5f / duty
                                     : 0.5f + (phase - duty) * 0.5f / (1.0f - duty));
        }

        // The whole cycle leaned one way, rather than each half of it bent
        // separately. Twelve o'clock is no change.
        case WarpMode::asym:
            return read(warpBend(phase, warpSignedAmount(a)));

        // An instantaneous polarity flip, with the knob saying where in the
        // cycle it happens. At nothing the flip sits on the wrap, where it
        // cannot be heard.
        case WarpMode::flip:
            return phase >= 1.0f - a ? -read(phase) : read(phase);

        // The second half of the cycle is the first half backwards, which
        // doubles the wave into the cycle and is why this always does
        // something. The knob bends both halves, as ASYM does to one.
        case WarpMode::mirror:
        {
            const auto u = phase < 0.5f ? phase * 2.0f : 2.0f - phase * 2.0f;
            return read(warpBend(u, warpSignedAmount(a)));
        }

        // Sample and hold on the read rather than on the output, so the
        // aliasing follows the pitch instead of sitting at one frequency for
        // every note — which is the difference the manual draws between this
        // and a redux effect.
        case WarpMode::quantize:
        {
            const auto steps = warpSteps(a);
            return read(std::floor(phase * steps) / steps);
        }

        // The halves of the cycle summed and differenced: the sum is the even
        // harmonics, the difference the odd ones. At twelve o'clock the two go
        // back together and nothing has changed.
        case WarpMode::oddEven:
        {
            const auto here = read(phase);
            const auto across = read(warpFrac(phase + 0.5f));
            const auto odd = (here - across) * 0.5f;
            const auto even = (here + across) * 0.5f;
            return odd * (2.0f - 2.0f * a) + even * (2.0f * a);
        }

        // The three filters and the eight waveshapers all work on what came out
        // of the table rather than on where it was read, and all of them
        // crossfade from the wave as it was to the mode at full strength. That
        // is what makes a knob at nothing mean nothing for every one of them:
        // choose a mode, then open the knob, rather than having to close the
        // knob before choosing. The filter state runs either way, so sweeping
        // the knob never starts a filter part-way through a note.
        //
        // The cost is that half a filter reads as a shelf rather than as a
        // corner. That is the right trade here: a warp is a depth control, and
        // a depth control that does something at zero is a trap.
        case WarpMode::lowPass:
        {
            const auto input = read(phase);
            state.low += stage.g * (input - state.low);
            return input + (state.low - input) * a;
        }

        case WarpMode::highPass:
        {
            const auto input = read(phase);
            state.low += stage.g * (input - state.low);
            return input - state.low * a;
        }

        case WarpMode::bandPass:
        {
            const auto input = read(phase);
            state.low += stage.g * (input - state.low);
            state.band += stage.g2 * (state.low - state.band);
            return input + (state.low - state.band - input) * a;
        }

        case WarpMode::tube:
        case WarpMode::softClip:
        case WarpMode::hardClip:
        case WarpMode::diode:
        case WarpMode::linearFold:
        case WarpMode::sineFold:
        case WarpMode::rectify:
        case WarpMode::tapeSat:
        {
            const auto input = read(phase);
            return input + (warpShape(stage.mode, a, input) - input) * a;
        }

        // Phase distortion: the read is pushed along the cycle by the modulator
        // rather than the cycle being made to run faster. The note therefore
        // stays exactly where it was however deep this goes, and a phase pushed
        // backwards simply reads backwards -- there is no negative frequency
        // for the oscillator to stop or reflect at, which is the whole problem
        // thru-zero exists to solve.
        case WarpMode::pdOsc:
        case WarpMode::pdSub:
        case WarpMode::pdNoise:
            return read(warpFrac(phase + warpFmIndex(stage.mode, a) * stage.modulator));

        // FM reaches the phase increment rather than the read, so there is
        // nothing to do here -- see warpPitchFactor, which the oscillator
        // applies to the rate the cycle runs at.
        case WarpMode::fmOsc:
        case WarpMode::fmSub:
        case WarpMode::fmNoise:
        case WarpMode::fmExpOsc:
        case WarpMode::fmExpSub:
        case WarpMode::fmExpNoise:
            break;

        // Amplitude modulation rides the carrier: at full depth the modulator
        // opens and closes it between silence and twice its level, and the
        // carrier itself is still in there, which is what makes AM a tremolo
        // taken up to audio rate rather than a new sound.
        case WarpMode::amOsc:
        case WarpMode::amSub:
        case WarpMode::amNoise:
            return read(phase) * (1.0f - a + a * (1.0f + stage.modulator));

        // Ring modulation replaces it instead. The carrier's own pitch leaves
        // the sound entirely and what is left is the two sidebands, which is
        // why RM is the clangorous one and AM is not.
        case WarpMode::rmOsc:
        case WarpMode::rmSub:
        case WarpMode::rmNoise:
        {
            const auto input = read(phase);
            return input + (input * stage.modulator - input) * a;
        }

        // The same thing driven by this stage's own output.
        //
        // The average of the last two samples goes back round rather than the
        // last one alone. A loop fed its own previous sample can settle into
        // flipping sign every sample -- an oscillation at half the sample rate,
        // which is not a sound, it is a fault -- and averaging the pair puts a
        // zero exactly there. It is the oldest trick in feedback FM.
        //
        // It does not stop the loop going chaotic at the top of the knob, and
        // it is not meant to: a feedback control is reached for precisely to
        // run from a saw to a hard edge to noise, and the depth this table is
        // read at decides where along that the noise starts. A saw reaches it
        // sooner than a sine would.
        case WarpMode::pdSelf:
        {
            const auto fed = 0.5f * (state.last + state.before);
            const auto shaped = read(warpFrac(phase + warpFmIndex(stage.mode, a) * fed));
            state.before = state.last;
            state.last = shaped;
            return shaped;
        }
    }
    return read(phase);
}

// --- Blocking what a warp leaves behind ---------------------------------------
//
// RECTIFY, TUBE and DIODE are all asymmetric on purpose, and an asymmetric
// shaper puts a constant offset into the signal. A constant offset in an
// oscillator is a thump on every note and a permanent bias the filter has to
// carry, so it is taken out once per oscillator after both stages rather than
// left for the mixer to inherit. Five Hertz is far enough below the lowest note
// Forge plays that nothing musical reaches it.
inline constexpr float warpDcBlockHz = 5.0f;

struct WarpDcBlocker
{
    float lastIn = 0.0f, lastOut = 0.0f;

    float process(float input, float coefficient) noexcept
    {
        lastOut = input - lastIn + coefficient * lastOut;
        lastIn = input;
        return lastOut;
    }
};

inline float warpDcCoefficient(double sampleRate) noexcept
{
    return std::exp(-juce::MathConstants<float>::twoPi * warpDcBlockHz
                    / static_cast<float>(juce::jmax(1.0, sampleRate)));
}
}
