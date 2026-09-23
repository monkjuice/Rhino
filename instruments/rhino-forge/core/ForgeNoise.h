#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <vector>

// The noise module's sources, and the voice-local state each one needs.
//
// Depends on nothing of Forge's, the way ForgeFilter.h depends on nothing: the
// engine renders from this file and the tests measure it, so there is one
// description of what each source actually is.
//
// Three properties are worth stating up front because the rest of the file
// falls out of them.
//
// **A source is a colour through a character.** White, pink and brown are the
// three spectra everything else is built from, and they are the slow part:
// brown's pole sits at 8 Hz, so a brown that has been idle takes a tenth of a
// second to mean anything. Those three therefore run every sample whatever is
// selected. What sits on top of them — a resonator bank, a decimator, a tilt,
// an event process — is fast, so only the live one runs, plus the one being
// faded out of.
//
// That split is what lets the list grow. Nineteen sources all running at once
// would be nineteen times the arithmetic to hear one of them; nineteen sources
// sharing three warm colours is two character stages however long the list
// gets.
//
// **A character stage is reset when it takes a new source.** The slot coming in
// starts clean rather than inheriting a filter that was doing something else,
// because state left over from another source is state nothing has accounted
// for. It costs almost nothing to warm up: the stage is fed a colour that never
// went cold. METAL is the one that audibly blooms, over about the time its own
// resonators take to ring up, which is what a struck resonator does anyway.
//
// **The state is per voice, not per synth.** A chord is several notes and each
// of them gets hiss of its own, which is what makes the noise thicken with the
// chord rather than sit behind it as one stream shared out. Serum's noise
// oscillator is per voice for the same reason.
namespace rhino::forge
{
// Every source the module offers.
//
// **Appended to, never reordered.** The Source parameter stores an index into
// this, so every value already written into a preset has to keep meaning what
// it meant — which is why the four this module opened with are still 0 to 3, and
// why GEIGER sits among the colours rather than beside CRACKLE where it
// belongs. What the panel *shows* is ordered properly; see noiseSourceOrder.
enum class NoiseSource
{
    white, pink, brown, geiger,
    blue, violet, grey,
    vintagePoly, vintagePolyHp, mono101, tapeHiss, hardwareHum,
    bright, bit, alpha,
    metallic,
    vinyl, wind,
    crackle
};

inline constexpr int noiseSourceCount = 19;

// The families the menu is grouped by, in the order it reads them. A family is
// how a source is actually chosen — you know you want something analog before
// you know which — exactly as the filter's six families are.
enum class NoiseCategory { colour, analog, digital, inharmonic, organic, transient };

inline constexpr int noiseCategoryCount = 6;

inline const char* noiseCategoryName(NoiseCategory category)
{
    switch (category)
    {
        case NoiseCategory::analog:     return "ANALOG";
        case NoiseCategory::digital:    return "DIGITAL";
        case NoiseCategory::inharmonic: return "INHARMONIC";
        case NoiseCategory::organic:    return "ORGANIC";
        case NoiseCategory::transient:  return "TRANSIENT";
        case NoiseCategory::colour:     break;
    }
    return "COLOUR";
}

inline NoiseCategory noiseCategoryOf(NoiseSource source)
{
    switch (source)
    {
        case NoiseSource::vintagePoly:
        case NoiseSource::vintagePolyHp:
        case NoiseSource::mono101:
        case NoiseSource::tapeHiss:
        case NoiseSource::hardwareHum:  return NoiseCategory::analog;
        case NoiseSource::bright:
        case NoiseSource::bit:
        case NoiseSource::alpha:        return NoiseCategory::digital;
        case NoiseSource::metallic:     return NoiseCategory::inharmonic;
        case NoiseSource::vinyl:
        case NoiseSource::wind:         return NoiseCategory::organic;
        case NoiseSource::geiger:
        case NoiseSource::crackle:      return NoiseCategory::transient;
        case NoiseSource::white:
        case NoiseSource::pink:
        case NoiseSource::brown:
        case NoiseSource::blue:
        case NoiseSource::violet:
        case NoiseSource::grey:         break;
    }
    return NoiseCategory::colour;
}

// The short name the field wears, and the long one the menu and a host's
// automation lane read. Two names for the same reason the LFO shapes have two —
// "TRI" against "Triangle": the plate is two columns of twenty-four and will
// not hold "Vintage Poly HP", while a menu has all the room in the world and a
// host's lane should say what the thing is rather than abbreviate it.
inline const char* noiseSourceName(int source)
{
    switch (static_cast<NoiseSource>(source))
    {
        case NoiseSource::pink:          return "PINK";
        case NoiseSource::brown:         return "BROWN";
        case NoiseSource::geiger:        return "GEIGER";
        case NoiseSource::blue:          return "BLUE";
        case NoiseSource::violet:        return "VIOLET";
        case NoiseSource::grey:          return "GREY";
        case NoiseSource::vintagePoly:   return "POLY";
        case NoiseSource::vintagePolyHp: return "POLY HP";
        case NoiseSource::mono101:       return "MONO";
        case NoiseSource::tapeHiss:      return "TAPE";
        case NoiseSource::hardwareHum:   return "HUM";
        case NoiseSource::bright:        return "BRIGHT";
        case NoiseSource::bit:           return "BIT";
        case NoiseSource::alpha:         return "ALPHA";
        case NoiseSource::metallic:      return "METAL";
        case NoiseSource::vinyl:         return "VINYL";
        case NoiseSource::wind:          return "WIND";
        case NoiseSource::crackle:       return "CRACKLE";
        case NoiseSource::white:         break;
    }
    return "WHITE";
}

// The long name. The analog sources are named for what they sound like rather
// than for the hardware whose character they borrow: Forge has a licence to
// none of those names, and a neutral one that describes the sound is more use
// to somebody reading a menu than a model number they may not know. Nothing
// here is a recording of anything — every one of them is generated.
inline const char* noiseSourceFullName(int source)
{
    switch (static_cast<NoiseSource>(source))
    {
        case NoiseSource::pink:          return "Pink";
        case NoiseSource::brown:         return "Brown";
        case NoiseSource::geiger:        return "Geiger";
        case NoiseSource::blue:          return "Blue";
        case NoiseSource::violet:        return "Violet";
        case NoiseSource::grey:          return "Grey";
        case NoiseSource::vintagePoly:   return "Vintage Poly";
        case NoiseSource::vintagePolyHp: return "Vintage Poly HP";
        case NoiseSource::mono101:       return "Mono Lead";
        case NoiseSource::tapeHiss:      return "Tape Hiss";
        case NoiseSource::hardwareHum:   return "Hardware Hum";
        case NoiseSource::bright:        return "Bright";
        case NoiseSource::bit:           return "Bit Crush";
        case NoiseSource::alpha:         return "Alpha";
        case NoiseSource::metallic:      return "Metallic";
        case NoiseSource::vinyl:         return "Vinyl";
        case NoiseSource::wind:          return "Wind";
        case NoiseSource::crackle:       return "Crackle";
        case NoiseSource::white:         break;
    }
    return "White";
}

// The order the panel reads the list in, which is by family and is *not* the
// order the values are stored in. Stepping the field's arrows walks this, so
// the sources either side of the one you are on are its neighbours rather than
// whatever happened to be added next.
inline const std::array<int, noiseSourceCount>& noiseSourceOrder()
{
    static const std::array<int, noiseSourceCount> order {{
        static_cast<int>(NoiseSource::white),         static_cast<int>(NoiseSource::pink),
        static_cast<int>(NoiseSource::brown),         static_cast<int>(NoiseSource::blue),
        static_cast<int>(NoiseSource::violet),        static_cast<int>(NoiseSource::grey),
        static_cast<int>(NoiseSource::vintagePoly),   static_cast<int>(NoiseSource::vintagePolyHp),
        static_cast<int>(NoiseSource::mono101),       static_cast<int>(NoiseSource::tapeHiss),
        static_cast<int>(NoiseSource::hardwareHum),
        static_cast<int>(NoiseSource::bright),        static_cast<int>(NoiseSource::bit),
        static_cast<int>(NoiseSource::alpha),
        static_cast<int>(NoiseSource::metallic),
        static_cast<int>(NoiseSource::vinyl),         static_cast<int>(NoiseSource::wind),
        static_cast<int>(NoiseSource::geiger),        static_cast<int>(NoiseSource::crackle),
    }};
    return order;
}

// Where a stored source sits in that order, so the field can say which of
// nineteen it is showing without the two lists having to agree on anything but
// their contents.
inline int noiseSourcePosition(int source)
{
    const auto& order = noiseSourceOrder();
    for (int i = 0; i < noiseSourceCount; ++i)
        if (order[static_cast<size_t>(i)] == source) return i;
    return 0;
}

inline int noiseSourceAt(int position)
{
    return noiseSourceOrder()[static_cast<size_t>(
        juce::jlimit(0, noiseSourceCount - 1, position))];
}

inline NoiseSource noiseSourceOf(float value)
{
    return static_cast<NoiseSource>(
        juce::jlimit(0, noiseSourceCount - 1, juce::roundToInt(value)));
}

// How long a source change takes to cross over. Long enough that brown — which
// wanders slowly and so is rarely near zero when you switch away from it — hands
// over without a step, short enough that the change still reads as immediate.
inline constexpr float noiseFadeSeconds = 0.006f;

// Where the tone control pivots. A first-order tilt: the half of the spectrum
// below it and the half above it are scaled against each other, so twelve
// o'clock is the signal untouched to the bit rather than a filter that happens
// to be flat.
inline constexpr float noiseTonePivotHz = 1000.0f;

// How often the two event sources strike.
//
// The two numbers are far enough apart to be two sources rather than one
// source at two speeds, and that gap is the whole point: geiger is sparse
// enough that the clicks are countable, crackle dense enough that they stop
// being events at all.
//
// Both are peaky -- a crest factor near five against continuous hiss's one and
// three quarters -- and crackle is if anything the peakier of the two, because
// its clicks are a fifth as long. So peakiness is not what separates them and
// the test does not ask it to: it counts events, and finds twenty-five times
// as many in one as the other.
inline constexpr float noiseGeigerEventsPerSecond = 45.0f;
inline constexpr float noiseCrackleEventsPerSecond = 1200.0f;

// How many resonators the metallic bank rings.
inline constexpr int noiseResonatorCount = 5;

// A pair of samples, because everything in this file is stereo from the point
// the two generators are mixed.
struct NoisePair
{
    float left = 0.0f, right = 0.0f;
};

// A 32-bit mix, so two channels seeded from one note are not neighbouring
// streams. The same one ForgeVoiceParts.h uses on the unison stack, repeated
// here rather than included so this header goes on depending on nothing.
inline std::uint32_t noiseHash(std::uint32_t x)
{
    x *= 2654435761u;
    x ^= x >> 15;
    x *= 2246822519u;
    x ^= x >> 13;
    return x;
}

// One pole's coefficient for a corner at this frequency: the g in
// y += g * (x - y). Used for every plain filter in the file, because a source's
// colour is a matter of where a handful of corners sit rather than of how steep
// any one of them is.
inline float noisePole(double hz, double rate)
{
    return static_cast<float>(
        1.0 - std::exp(-2.0 * juce::MathConstants<double>::pi * std::max(0.1, hz)
                       / std::max(1000.0, rate)));
}

// A two-pole resonator, which is how the metallic bank, the bright sheen and
// the wind's moving band are all built. It holds the two samples behind it and
// nothing else; the coefficients are the caller's, because three sources drive
// this with three different ideas of how sharp it should be.
struct NoiseResonator
{
    float y1 = 0.0f, y2 = 0.0f;

    float run(float feedback, float damping, float gain, float x)
    {
        const auto y = gain * x + feedback * y1 - damping * y2;
        y2 = y1;
        y1 = y;
        return y;
    }
};

// One resonator's coefficients, worked out from a frequency and how close to
// the unit circle its poles sit.
struct NoiseResonance
{
    float feedback = 0.0f, damping = 0.0f, gain = 0.0f;

    void set(double hz, double radius, double rate)
    {
        const auto omega = 2.0 * juce::MathConstants<double>::pi
                         * juce::jlimit(20.0, rate * 0.45, hz) / rate;
        const auto r = juce::jlimit(0.0, 0.9995, radius);
        feedback = static_cast<float>(2.0 * r * std::cos(omega));
        damping = static_cast<float>(r * r);
        // Roughly unity at the peak whatever the radius, so sharpening a
        // resonator changes how it rings rather than how loud it is.
        gain = static_cast<float>((1.0 - r * r) * std::sin(omega));
    }
};

struct NoiseCoefficients;

// The three spectra every source is built from, this sample.
struct NoiseColours
{
    float white = 0.0f, pink = 0.0f, brown = 0.0f;
};

// The always-running part of one channel: the generator and the two filters
// that turn it into pink and brown. Cheap, and never reset by a source change,
// which is what makes a character stage's own reset free.
struct NoiseCore
{
    std::uint32_t rng = 0x9e3779b9u;
    std::array<float, 3> pink {};
    float brown = 0.0f, blockIn = 0.0f, blockOut = 0.0f;

    void start(std::uint32_t seed)
    {
        *this = {};
        // Zero is the one state a xorshift cannot leave, so it is never handed
        // one: the low bit is forced and the seed is mixed first.
        rng = noiseHash(seed) | 1u;
    }

    static float nextUnit(std::uint32_t& state)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float>(state & 0xffffu) / 65536.0f;
    }

    // The same uniform white the module has always put out: -1 to 1, taken from
    // sixteen bits of a xorshift.
    float white() { return nextUnit(rng) * 2.0f - 1.0f; }

    NoiseColours advance(const NoiseCoefficients&);
};

// Everything one source does to a colour, and the state it needs to do it.
//
// One struct for all nineteen rather than one per source: a slot holds whatever
// is selected, so the state has to be sized for the largest of them either way,
// and nineteen little structs behind a variant would be the same bytes with a
// dispatch on top. The fields say which sources use them.
struct NoiseCharacter
{
    // General one-pole states. Every source that shapes a colour with plain
    // corners takes what it needs from here, in the order it needs them.
    std::array<float, 4> lp {};
    // The sample before, for the two differentiators — BLUE and VIOLET.
    float previous = 0.0f;
    // The bank METAL rings, and the single one BRIGHT and WIND each use.
    std::array<NoiseResonator, noiseResonatorCount> resonator {};
    // BIT's held sample and the countdown to the next one; ALPHA's shift
    // register, its held sample and its own countdown.
    float held = 0.0f;
    int holdLeft = 0;
    std::uint32_t shift = 0x7fffu;
    float shiftHeld = 0.0f;
    int shiftLeft = 0;
    // HUM's cycle, and the two slow ones TAPE wobbles with and WIND sweeps on.
    float phase = 0.0f;
    std::array<float, 2> slowPhase {};
    // The event process GEIGER, CRACKLE and VINYL all strike.
    std::uint32_t events = 0x85ebca6bu;
    float fast = 0.0f, slow = 0.0f;

    void start(std::uint32_t seed)
    {
        *this = {};
        events = noiseHash(seed ^ 0x5bf03635u) | 1u;
        shift = noiseHash(seed ^ 0x27d4eb2fu) & 0x7fffu;
        if (shift == 0u) shift = 0x7fffu;
    }

    float render(NoiseSource, const NoiseColours&, const NoiseCoefficients&);

private:
    // A shaped click, struck at the given chance and shaped by the given pair
    // of decays. The difference of two decays rather than an impulse: it leaves
    // zero at the sample it is struck on and rises into its peak, so there is
    // no single-sample spike to be caught by anything downstream.
    float strike(float chance, float fastDecay, float slowDecay, float spread);
};

// Everything that depends on the sample rate, worked out once when the engine
// is prepared. Nothing here is touched from the audio thread.
struct NoiseCoefficients
{
    // Three one-poles summed with a little of the input, which is the standard
    // economy pink filter. The published coefficients are for 44.1 kHz; they
    // are retuned below, so the slope is 3 dB an octave at whatever rate is
    // actually running rather than only at the one they were printed for.
    std::array<float, 3> pinkPole {}, pinkGain {};
    float pinkDirect = 0.0f;

    // Brown is white through a pole placed below the audible band, so
    // everything that can be heard of it is already on the 6 dB skirt. The
    // blocker after it is what stops the walk wandering off towards a rail.
    float brownPole = 0.0f, brownBlock = 0.0f;

    // The corners each shaped source bends its colour at.
    float greyLow = 0.0f, greyHigh = 0.0f;
    float polyBody = 0.0f, polyTop = 0.0f, polyHighPass = 0.0f;
    float monoLow = 0.0f, monoHigh = 0.0f;
    float tapeLift = 0.0f;
    float brightCut = 0.0f;
    float vinylRumble = 0.0f;
    float windBody = 0.0f;

    // The event processes, and the decays that shape one of their clicks.
    float geigerChance = 0.0f, geigerFast = 0.0f, geigerSlow = 0.0f;
    float crackleChance = 0.0f, crackleFast = 0.0f, crackleSlow = 0.0f;

    // BIT's hold in samples and the step it quantises to; ALPHA's hold, which
    // quantises nothing.
    int bitHold = 1;
    float bitStep = 1.0f;
    int alphaHold = 1;

    // HUM's cycle, and the slow ones TAPE and WIND move on.
    float humStep = 0.0f;
    std::array<float, 2> tapeStep {}, windStep {};

    // METAL's bank, BRIGHT's sheen and the two ends of WIND's sweep.
    std::array<NoiseResonance, noiseResonatorCount> metal {};
    NoiseResonance bright {};
    std::array<NoiseResonance, 2> wind {};

    // What each source has to be multiplied by to come out at white's level.
    // Measured rather than derived — see noiseScalesFor.
    std::array<float, noiseSourceCount> scale {};

    float toneG = 0.0f;
    // What the tilt does to the level of a flat spectrum at either end of the
    // knob, less one. Taking it back out is what keeps TONE a tone control
    // rather than a second level knob — see tone().
    float toneHighTrim = 0.0f, toneLowTrim = 0.0f;

    float fadeStep = 1.0f;

    void prepare(double sampleRate);
};

inline NoiseColours NoiseCore::advance(const NoiseCoefficients& c)
{
    NoiseColours out;
    out.white = white();

    auto sum = c.pinkDirect * out.white;
    for (size_t i = 0; i < pink.size(); ++i)
    {
        pink[i] = c.pinkPole[i] * pink[i] + c.pinkGain[i] * out.white;
        sum += pink[i];
    }
    out.pink = sum;

    brown = c.brownPole * brown + out.white;
    // A one-pole blocker, written out rather than reusing the warp's because
    // this one runs on a signal that is almost entirely below its corner.
    blockOut = brown - blockIn + c.brownBlock * blockOut;
    blockIn = brown;
    out.brown = blockOut;
    return out;
}

inline float NoiseCharacter::strike(float chance, float fastDecay, float slowDecay, float spread)
{
    auto excite = 0.0f;
    if (NoiseCore::nextUnit(events) < chance)
    {
        // Clicks of differing size, which is what stops a train of identical
        // impulses reading as a tone at the event rate. How much they differ is
        // the source's business: geiger's vary widely, a vinyl surface much
        // less.
        const auto size = 1.0f - spread * NoiseCore::nextUnit(events);
        excite = (events & 0x10000u) != 0u ? size : -size;
    }
    fast = fastDecay * fast + excite;
    slow = slowDecay * slow + excite;
    return slow - fast;
}

// One source, rendered from the colours it is built on.
//
// Nothing here normalises its own level: every branch returns whatever its
// arithmetic happens to give, and the one multiply that brings the nineteen of
// them to a common loudness is applied outside, from a table measured at
// prepare. That is what lets a source be adjusted by ear without also having to
// re-derive a gain for it.
inline float NoiseCharacter::render(NoiseSource source, const NoiseColours& colour,
                                    const NoiseCoefficients& c)
{
    switch (source)
    {
        case NoiseSource::white: return colour.white;
        case NoiseSource::pink:  return colour.pink;
        case NoiseSource::brown: return colour.brown;

        // Differentiating lifts a spectrum by 6 dB an octave, so white becomes
        // violet and pink — already 3 down — becomes blue. Both are the cheapest
        // things in the file and the two brightest.
        case NoiseSource::violet:
        {
            const auto out = colour.white - previous;
            previous = colour.white;
            return out;
        }
        case NoiseSource::blue:
        {
            const auto out = colour.pink - previous;
            previous = colour.pink;
            return out;
        }

        // Flat to the ear rather than flat to a meter: an equal-loudness
        // weighting lifts both ends of the band against the middle, because the
        // middle is where hearing is most sensitive and a flat spectrum
        // therefore sounds mid-heavy. Two corners and a scaled middle is a
        // coarse version of that curve, and a coarse version is audibly the
        // right shape.
        case NoiseSource::grey:
        {
            lp[0] += c.greyLow * (colour.white - lp[0]);
            lp[1] += c.greyHigh * (colour.white - lp[1]);
            const auto top = colour.white - lp[1];
            return colour.white * 0.35f + lp[0] * 3.2f + top * 1.25f;
        }

        // The analog five. Each is white bent at a couple of corners and, for
        // one of them, leaned on until it is not quite linear any more — which
        // between them is most of what makes one piece of hardware's noise floor
        // sound unlike another's.
        case NoiseSource::vintagePoly:
        {
            lp[0] += c.polyTop * (colour.white - lp[0]);
            lp[1] += c.polyBody * (colour.white - lp[1]);
            return lp[0] * 0.85f + lp[1] * 1.6f;
        }
        case NoiseSource::vintagePolyHp:
        {
            // The same soft top, with the body taken out rather than turned
            // down. A high pass on the source is not the same thing as the
            // module's tilt: this one keeps the roll-off above it, so what is
            // left is air rather than a brightened version of the whole.
            lp[0] += c.polyTop * (colour.white - lp[0]);
            lp[1] += c.polyHighPass * (lp[0] - lp[1]);
            return lp[0] - lp[1];
        }
        case NoiseSource::mono101:
        {
            lp[0] += c.monoHigh * (colour.white - lp[0]);
            lp[1] += c.monoLow * (colour.white - lp[1]);
            const auto band = lp[0] - lp[1];
            return std::tanh((colour.white * 0.45f + band * 1.9f) * 1.35f);
        }
        case NoiseSource::tapeHiss:
        {
            lp[0] += c.tapeLift * (colour.pink - lp[0]);
            const auto lift = colour.pink - lp[0];
            // Wow and flutter, which on a noise floor is not a pitch wobble but
            // a level one: two slow cycles beating against each other, deep
            // enough to be heard breathing and no deeper.
            slowPhase[0] += c.tapeStep[0];
            slowPhase[1] += c.tapeStep[1];
            if (slowPhase[0] >= 1.0f) slowPhase[0] -= 1.0f;
            if (slowPhase[1] >= 1.0f) slowPhase[1] -= 1.0f;
            const auto wobble = 1.0f
                + 0.06f * std::sin(slowPhase[0] * juce::MathConstants<float>::twoPi)
                + 0.03f * std::sin(slowPhase[1] * juce::MathConstants<float>::twoPi);
            return (colour.pink * 0.75f + lift * 1.5f) * wobble;
        }
        case NoiseSource::hardwareHum:
        {
            phase += c.humStep;
            if (phase >= 1.0f) phase -= 1.0f;
            const auto angle = phase * juce::MathConstants<float>::twoPi;
            const auto s = std::sin(angle), k = std::cos(angle);
            // The harmonics from the one sine rather than from four of them: a
            // mains hum is a fundamental with a stack of partials on it, and
            // the double- and triple-angle identities are that stack for the
            // price of a single call.
            const auto second = 2.0f * s * k;
            const auto third = s * (3.0f - 4.0f * s * s);
            const auto fourth = 2.0f * second * (1.0f - 2.0f * s * s);
            return s + second * 0.38f + third * 0.22f + fourth * 0.11f
                 + colour.white * 0.10f;
        }

        // The digital three. Two of them are what a converter does badly, and
        // one is what a shift register sounds like.
        case NoiseSource::bright:
        {
            lp[0] += c.brightCut * (colour.white - lp[0]);
            const auto top = colour.white - lp[0];
            // A sheen rather than a peak: enough resonance to put a gloss over
            // the top of the band, not enough to whistle.
            const auto sheen = resonator[0].run(c.bright.feedback, c.bright.damping,
                                                c.bright.gain, top);
            return top * 0.9f + sheen * 2.2f;
        }
        case NoiseSource::bit:
        {
            if (--holdLeft <= 0)
            {
                holdLeft = c.bitHold;
                // Held *and* quantised. Either alone is a mild effect; the two
                // together are the sound of a converter running out of both
                // kinds of resolution at once.
                held = std::round(colour.white / c.bitStep) * c.bitStep;
            }
            return held;
        }
        case NoiseSource::alpha:
        {
            if (--shiftLeft <= 0)
            {
                shiftLeft = c.alphaHold;
                // A fifteen-bit maximal-length register, clocked well inside
                // the band. Its spectrum is flat but its steps are not random
                // in the way a longer generator's are, and the hold between
                // clocks puts a sinc over the top — which together is the
                // glassy, slightly hollow hiss a cheap digital source has.
                const auto feedback = ((shift ^ (shift >> 1)) & 1u) != 0u;
                shift = (shift >> 1) | (feedback ? 0x4000u : 0u);
                shiftHeld = (shift & 1u) != 0u ? 1.0f : -1.0f;
            }
            return shiftHeld;
        }

        // Five resonators at ratios that are deliberately not a harmonic
        // series. A harmonic set rung by noise is a pitch; an inharmonic one is
        // an object being struck, which is what this is for.
        case NoiseSource::metallic:
        {
            auto sum = 0.0f;
            for (size_t i = 0; i < resonator.size(); ++i)
                sum += resonator[i].run(c.metal[i].feedback, c.metal[i].damping,
                                        c.metal[i].gain, colour.white);
            return sum;
        }

        // A surface rather than a spectrum: the bed the record is made of, the
        // dirt on it, and the rumble of the table under both.
        case NoiseSource::vinyl:
        {
            // A twelfth of crackle's density: a record's surface is dirt you
            // can pick out, where static is a wall of it.
            const auto dirt = strike(c.crackleChance * 0.12f, c.crackleFast, c.crackleSlow, 0.75f);
            lp[0] += c.vinylRumble * (colour.brown - lp[0]);
            return colour.pink * 0.30f + dirt * 3.0f + lp[0] * 0.55f;
        }
        case NoiseSource::wind:
        {
            // A band that moves, which is the whole of what tells wind from
            // filtered hiss. Two slow cycles again, beating so the sweep never
            // repeats on an audible period.
            slowPhase[0] += c.windStep[0];
            slowPhase[1] += c.windStep[1];
            if (slowPhase[0] >= 1.0f) slowPhase[0] -= 1.0f;
            if (slowPhase[1] >= 1.0f) slowPhase[1] -= 1.0f;
            const auto sweep = 0.5f
                + 0.35f * std::sin(slowPhase[0] * juce::MathConstants<float>::twoPi)
                + 0.15f * std::sin(slowPhase[1] * juce::MathConstants<float>::twoPi);
            const auto blend = juce::jlimit(0.0f, 1.0f, sweep);
            // The two ends of the sweep are two sets of coefficients and the
            // band is interpolated between them, so the corner moves smoothly
            // without a trigonometric call per sample.
            const auto feedback = c.wind[0].feedback + (c.wind[1].feedback - c.wind[0].feedback) * blend;
            const auto damping = c.wind[0].damping + (c.wind[1].damping - c.wind[0].damping) * blend;
            const auto gain = c.wind[0].gain + (c.wind[1].gain - c.wind[0].gain) * blend;
            const auto band = resonator[0].run(feedback, damping, gain, colour.pink);
            lp[0] += c.windBody * (colour.brown - lp[0]);
            return band * 2.6f + lp[0] * 0.7f;
        }

        // The two event sources. The same process at two very different
        // settings: one you can count and one you cannot.
        case NoiseSource::crackle:
            return strike(c.crackleChance, c.crackleFast, c.crackleSlow, 0.8f);
        case NoiseSource::geiger:
            break;
    }
    return strike(c.geigerChance, c.geigerFast, c.geigerSlow, 0.5f);
}

// One channel: the colours, the two character slots, and the tilt after them.
struct NoiseChannel
{
    NoiseCore core;
    // The live stage and the one being faded out of. Which is which is the
    // voice's business — both channels have to agree, or the image would come
    // apart for six milliseconds on every source change.
    std::array<NoiseCharacter, 2> stage {};
    float toneLow = 0.0f;

    void start(std::uint32_t seed)
    {
        core.start(seed);
        toneLow = 0.0f;
        for (size_t i = 0; i < stage.size(); ++i)
            stage[i].start(seed + static_cast<std::uint32_t>(i) * 0x9e3779b9u);
    }

    float tone(float in, const NoiseCoefficients&, float amount);
};

// The tilt. Below the pivot and above it are scaled against each other, so the
// two halves always add back up to the signal that went in: at zero this
// returns its input exactly, which is what lets a patch leave TONE alone and
// know it is hearing the generator rather than the generator plus a filter.
inline float NoiseChannel::tone(float in, const NoiseCoefficients& c, float amount)
{
    toneLow += c.toneG * (in - toneLow);
    const auto t = juce::jlimit(-1.0f, 1.0f, amount);
    // Taken early, and not only to save the arithmetic. Splitting a signal and
    // adding the halves back together is not the identity in floating point, so
    // a patch that leaves TONE alone would otherwise be hearing the generator
    // plus an ulp of rounding rather than the generator. The filter above still
    // runs, so opening the knob from here starts from a warm pole.
    if (t == 0.0f) return in;
    const auto high = in - toneLow;
    const auto tilted = toneLow * (1.0f - t) + high * (1.0f + t);
    // Tilting a flat spectrum is mostly a level change, because nearly all of
    // its power is above a pivot this low. The trim measured at prepare takes
    // that back out, so sweeping the knob changes the colour rather than the
    // loudness.
    const auto trim = 1.0f + (t >= 0.0f ? c.toneHighTrim : c.toneLowTrim) * std::abs(t);
    return tilted / trim;
}

// One voice's noise: two channels, which source it is on, and the crossfade out
// of the one it was on before.
struct NoiseVoice
{
    NoiseChannel x, y;
    int source = 0;
    int previous = 0;
    // Which of the two character slots the live source is in. They swap on
    // every change rather than the new one always landing in slot zero, so the
    // outgoing stage keeps running where it is instead of being copied.
    int live = 0;
    float fade = 1.0f;
    // How far the module's own power switch has opened. Hiss arriving at full
    // level in one sample is a click, and so is hiss stopping in one, so the
    // switch is a ramp over the same six milliseconds a source change takes.
    // It is also why a switched-off module goes on being rendered until the
    // ramp has actually reached the bottom -- see audible().
    float gate = 0.0f;

    // A new note. The two channels are seeded apart so the image is not one
    // stream doubled, and the seed is the caller's business — see
    // Core::startVoice, which counts notes so a rendered phrase is the same
    // twice and two notes in it are still different.
    void start(std::uint32_t seed)
    {
        x.start(seed);
        y.start(seed ^ 0xa5a5a5a5u);
        // A fade the last note was part way through is finished rather than
        // inherited: both generators are fresh, so there is nothing left to
        // cross over from.
        previous = source;
        fade = 1.0f;
    }

    bool audible() const { return gate > 0.0f; }

    // STEREO is decorrelation, not width: at nothing the two channels are the
    // same samples and the module is mono to the bit, at full they are two
    // streams that share no state. In between is an equal-power blend of the
    // two, so the level holds while the correlation falls off as the square
    // root of what is left — the same law Forge pans a source with.
    NoisePair render(const NoiseCoefficients& c, bool enabled, int wanted, float toneAmount,
                     float stereo)
    {
        wanted = juce::jlimit(0, noiseSourceCount - 1, wanted);
        if (wanted != source)
        {
            // Fading out of a source that was itself still fading in would need
            // a third stage. The one part way through is taken as where the
            // fade starts from instead, which is inaudible at six milliseconds
            // and keeps the state to two.
            previous = source;
            source = wanted;
            live ^= 1;
            fade = 0.0f;
            // The slot coming in starts clean. It is fed a colour that never
            // went cold, so there is nothing to warm up but its own corners.
            const auto slot = static_cast<size_t>(live);
            x.stage[slot].start(noiseHash(static_cast<std::uint32_t>(wanted) * 2654435761u
                                          + x.core.rng));
            y.stage[slot].start(noiseHash(static_cast<std::uint32_t>(wanted) * 40503u
                                          + y.core.rng));
        }

        const auto colourX = x.core.advance(c);
        const auto colourY = y.core.advance(c);

        const auto now = static_cast<NoiseSource>(source);
        const auto was = static_cast<NoiseSource>(previous);
        const auto nowScale = c.scale[static_cast<size_t>(source)];
        const auto wasScale = c.scale[static_cast<size_t>(previous)];
        const auto liveSlot = static_cast<size_t>(live);
        const auto deadSlot = static_cast<size_t>(live ^ 1);
        const auto crossing = fade < 1.0f;

        const auto chosen = [&] (NoiseChannel& channel, const NoiseColours& colour)
        {
            const auto sound = channel.stage[liveSlot].render(now, colour, c) * nowScale;
            if (!crossing) return sound;
            const auto going = channel.stage[deadSlot].render(was, colour, c) * wasScale;
            return sound * fade + going * (1.0f - fade);
        };

        // The tilt is linear, so a channel filtering its own blend is the same
        // answer as filtering after the two are mixed, for state that belongs
        // to the channel that owns it.
        const auto direct = x.tone(chosen(x, colourX), c, toneAmount);
        const auto spread = y.tone(chosen(y, colourY), c, toneAmount);
        if (crossing) fade = juce::jmin(1.0f, fade + c.fadeStep);

        gate = juce::jlimit(0.0f, 1.0f, gate + (enabled ? c.fadeStep : -c.fadeStep));

        const auto width = juce::jlimit(0.0f, 1.0f, stereo);
        const auto kept = std::sqrt(1.0f - width);
        const auto added = std::sqrt(width);
        return {direct * gate, (direct * kept + spread * added) * gate};
    }
};

// What every source has to be multiplied by to arrive at white's level.
//
// Measured rather than derived, because three of the colours and every one of
// the characters is white through something, and what that something does to
// the level depends on the poles it was just given, on the blocker after it, on
// where a difference of two decays happens to peak and on how hard a saturator
// is being leaned on. Three seconds of each, from a fixed seed, is the one
// answer that cannot disagree with the arithmetic above.
//
// It depends on nothing but the sample rate, and it is tens of milliseconds of
// work, so it is done once and shared. A second plugin instance at the same
// rate pays nothing, and neither do the twenty-odd Cores a test run builds.
// The lock is taken only from whatever thread prepares a synth; nothing here is
// reachable from the audio callback.
inline std::array<float, noiseSourceCount> noiseScalesFor(double rate, const NoiseCoefficients& c)
{
    struct Entry
    {
        double rate;
        std::array<float, noiseSourceCount> scale;
    };
    static std::mutex guard;
    static std::vector<Entry> measured;

    const std::lock_guard<std::mutex> lock(guard);
    for (const auto& entry : measured)
        if (entry.rate == rate) return entry.scale;

    // Warmed up first, and measured over long enough to settle. Brown's
    // integrator spends a while filling from nothing, and the clicks are sparse
    // enough that a short window catches an unrepresentative number of them —
    // both would read as a source that is quiet rather than as a probe that was
    // too short.
    const auto warmup = static_cast<int>(rate * 0.3);
    const auto samples = static_cast<int>(rate * 3.0);

    std::array<double, noiseSourceCount> level {};
    for (int source = 0; source < noiseSourceCount; ++source)
    {
        NoiseCore core;
        NoiseCharacter character;
        core.start(0x1234567u);
        character.start(0x1234567u);
        const auto which = static_cast<NoiseSource>(source);

        for (int i = 0; i < warmup; ++i) character.render(which, core.advance(c), c);

        auto power = 0.0;
        for (int i = 0; i < samples; ++i)
        {
            const auto sample = character.render(which, core.advance(c), c);
            power += static_cast<double>(sample) * sample;
        }
        level[static_cast<size_t>(source)] = std::sqrt(power / std::max(1, samples));
    }

    // The target is white's own level, so changing source changes the colour
    // and not the loudness — and so a patch written when NOISE was a hiss knob
    // is exactly as loud as it was.
    std::array<float, noiseSourceCount> scales {};
    const auto target = level[static_cast<size_t>(NoiseSource::white)];
    for (int source = 0; source < noiseSourceCount; ++source)
    {
        const auto own = level[static_cast<size_t>(source)];
        scales[static_cast<size_t>(source)] =
            static_cast<float>(own > 1.0e-9 ? target / own : 1.0);
    }

    measured.push_back({rate, scales});
    return scales;
}

inline void NoiseCoefficients::prepare(double sampleRate)
{
    const auto rate = std::max(8000.0, sampleRate);
    fadeStep = static_cast<float>(1.0 / (rate * noiseFadeSeconds));

    // --- Pink -----------------------------------------------------------------
    //
    // Retuned rather than used as printed. A pole is a frequency, and a pole
    // written for 44.1 kHz sits at a different frequency at 96 — which would
    // bend the slope the filter exists to hold. Raising it to the ratio of the
    // rates moves it back, and the gain beside it is rescaled to keep the
    // section's response at DC where it was.
    constexpr double referenceRate = 44100.0;
    constexpr std::array<double, 3> poles {0.99765, 0.96300, 0.57000};
    constexpr std::array<double, 3> gains {0.0990460, 0.2965164, 1.0526913};
    const auto exponent = referenceRate / rate;
    for (size_t i = 0; i < poles.size(); ++i)
    {
        const auto pole = std::pow(poles[i], exponent);
        pinkPole[i] = static_cast<float>(pole);
        pinkGain[i] = static_cast<float>(gains[i] * (1.0 - pole) / (1.0 - poles[i]));
    }
    pinkDirect = 0.1848f;

    // --- Brown ----------------------------------------------------------------
    //
    // The pole is placed at 8 Hz, below anything that will be listened to, so
    // the whole audible band is on the 6 dB skirt rather than only the top of
    // it. The blocker sits at the same frequency: an integrator fed noise walks,
    // and a walk with nothing holding it down eventually leaves the range.
    brownPole = static_cast<float>(std::exp(-2.0 * juce::MathConstants<double>::pi * 8.0 / rate));
    brownBlock = brownPole;

    // --- The corners every shaped source bends at -----------------------------
    greyLow = noisePole(250.0, rate);
    greyHigh = noisePole(5000.0, rate);
    polyBody = noisePole(420.0, rate);
    polyTop = noisePole(7000.0, rate);
    polyHighPass = noisePole(380.0, rate);
    monoLow = noisePole(450.0, rate);
    monoHigh = noisePole(1800.0, rate);
    tapeLift = noisePole(4000.0, rate);
    brightCut = noisePole(3000.0, rate);
    vinylRumble = noisePole(55.0, rate);
    windBody = noisePole(160.0, rate);

    // --- The event processes --------------------------------------------------
    geigerChance = static_cast<float>(noiseGeigerEventsPerSecond / rate);
    geigerFast = static_cast<float>(std::exp(-1.0 / (rate * 0.0004)));
    geigerSlow = static_cast<float>(std::exp(-1.0 / (rate * 0.0035)));
    crackleChance = static_cast<float>(noiseCrackleEventsPerSecond / rate);
    crackleFast = static_cast<float>(std::exp(-1.0 / (rate * 0.00008)));
    crackleSlow = static_cast<float>(std::exp(-1.0 / (rate * 0.0006)));

    // --- The two that hold a sample -------------------------------------------
    //
    // Both are set in Hertz rather than in samples, so a patch sounds the same
    // at 44.1 and at 96 rather than the crush getting twice as fine.
    bitHold = juce::jmax(1, juce::roundToInt(rate / 4200.0));
    bitStep = 2.0f / 15.0f;
    alphaHold = juce::jmax(1, juce::roundToInt(rate / 15000.0));

    // --- The cycles -----------------------------------------------------------
    humStep = static_cast<float>(50.0 / rate);
    tapeStep[0] = static_cast<float>(0.7 / rate);
    tapeStep[1] = static_cast<float>(5.3 / rate);
    windStep[0] = static_cast<float>(0.11 / rate);
    windStep[1] = static_cast<float>(0.043 / rate);

    // --- The resonators -------------------------------------------------------
    //
    // Ratios that share no common factor, so what rings is a struck object
    // rather than a note. A harmonic series here would be a pitch, which is the
    // one thing an inharmonic source must not be.
    constexpr std::array<double, noiseResonatorCount> ratios {1.0, 1.71, 2.43, 3.17, 4.29};
    for (size_t i = 0; i < ratios.size(); ++i)
        metal[i].set(680.0 * ratios[i], 0.9965, rate);
    bright.set(9000.0, 0.86, rate);
    wind[0].set(220.0, 0.986, rate);
    wind[1].set(1300.0, 0.986, rate);

    // --- Tone -----------------------------------------------------------------
    //
    // The pivot, and what tilting fully either way does to the level of a flat
    // spectrum. Both ends are measured off the very filter tone() runs, by
    // walking its response across the band: a pivot this far down the spectrum
    // means nearly all of a noise source's power is on the upper half, so a
    // full bright tilt is most of 6 dB of level and a full dark one is a long
    // way below unity. Those two numbers are what tone() divides back out.
    toneG = noisePole(noiseTonePivotHz, rate);
    {
        const auto pole = 1.0 - static_cast<double>(toneG);
        constexpr int bins = 512;
        auto powerHigh = 0.0, powerLow = 0.0;
        for (int bin = 0; bin < bins; ++bin)
        {
            // The centre of each of an even split of the band, so neither DC
            // nor Nyquist is weighted as though it were a whole bin wide.
            const auto omega = juce::MathConstants<double>::pi * (bin + 0.5) / bins;
            // One pole, evaluated where it actually is: g / (1 - p e^-jw).
            const auto realPart = 1.0 - pole * std::cos(omega);
            const auto imagPart = pole * std::sin(omega);
            const auto denominator = realPart * realPart + imagPart * imagPart;
            const auto lowReal = static_cast<double>(toneG) * realPart / denominator;
            const auto lowImag = static_cast<double>(toneG) * imagPart / denominator;
            // The two halves at full tilt: twice the one, none of the other.
            const auto highReal = 2.0 * (1.0 - lowReal), highImag = -2.0 * lowImag;
            powerHigh += highReal * highReal + highImag * highImag;
            powerLow += 4.0 * (lowReal * lowReal + lowImag * lowImag);
        }
        toneHighTrim = static_cast<float>(std::sqrt(powerHigh / bins) - 1.0);
        toneLowTrim = static_cast<float>(std::sqrt(powerLow / bins) - 1.0);
    }

    // --- Levels ---------------------------------------------------------------
    //
    // Last, because every other coefficient above is what the probe renders
    // through. All ones on the way in, so nothing it measures is scaled by a
    // table that is still being worked out.
    scale.fill(1.0f);
    scale = noiseScalesFor(rate, *this);
}
}
