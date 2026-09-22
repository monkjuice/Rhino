#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <cmath>
#include <cstdint>

// The noise module's sources, and the voice-local state each one needs.
//
// Depends on nothing of Forge's, the way ForgeFilter.h depends on nothing: the
// engine renders from this file and the tests measure it, so there is one
// description of what WHITE, PINK, BROWN and GEIGER actually are.
//
// Two properties are worth stating up front because the rest of the file falls
// out of them.
//
// **Every source runs every sample.** Only the selected one is heard, but pink
// and brown are filters and a filter that has been idle is a filter holding
// whatever it was left with. Running the lot means a source change crossfades
// between two streams that are both already warm, and costs a handful of
// multiply-adds against the twelve table reads an oscillator's unison stack
// does beside it.
//
// **The state is per voice, not per synth.** A chord is several notes and each
// of them gets hiss of its own, which is what makes the noise thicken with the
// chord rather than sit behind it as one stream shared out. Serum's noise
// oscillator is per voice for the same reason.
namespace rhino::forge
{
// White is flat, pink falls at 3 dB an octave, brown at 6, and geiger is not a
// spectrum at all but a sparse train of shaped clicks. The order is what the
// Source parameter stores, so it is appended to and never reordered.
enum class NoiseSource { white, pink, brown, geiger };

inline constexpr int noiseSourceCount = 4;

inline const char* noiseSourceName(int source)
{
    switch (source)
    {
        case 1: return "PINK";
        case 2: return "BROWN";
        case 3: return "GEIGER";
        default: break;
    }
    return "WHITE";
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

// Where the tone control pivots, and how far each half of the spectrum moves at
// the ends of it. A first-order tilt: the half below the pivot and the half
// above it are scaled against each other, so twelve o'clock is the signal
// untouched to the bit rather than a filter that happens to be flat.
inline constexpr float noiseTonePivotHz = 1000.0f;

// How often a geiger event happens. Sparse enough to be heard as separate
// clicks rather than as a texture, which is the whole character of it.
inline constexpr float noiseGeigerEventsPerSecond = 45.0f;

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

struct NoiseCoefficients;

// One channel's four generators, and the tone filter after them. Two of these
// make a voice: one per side of the image.
struct NoiseChannel
{
    // Two streams rather than one. White, pink and brown all read the first,
    // because pink and brown *are* white through a filter and reading one
    // stream is what makes that true. The event process reads the second, so
    // switching to geiger cannot change the hiss the other three are made of.
    std::uint32_t rng = 0x9e3779b9u;
    std::uint32_t events = 0x85ebca6bu;

    std::array<float, 3> pink {};
    float brown = 0.0f, blockIn = 0.0f, blockOut = 0.0f;
    float geigerFast = 0.0f, geigerSlow = 0.0f;
    float toneLow = 0.0f;

    // What each source put out this sample, indexed by NoiseSource. Written by
    // advance() and read by whichever source is selected — and by the one being
    // faded out of, which is the reason they are kept side by side rather than
    // returned one at a time.
    std::array<float, noiseSourceCount> out {};

    void start(std::uint32_t seed)
    {
        *this = {};
        // Zero is the one state a xorshift cannot leave, so it is never handed
        // one: the low bit is forced and the seed is mixed first.
        rng = noiseHash(seed) | 1u;
        events = noiseHash(seed ^ 0x5bf03635u) | 1u;
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
    float white()
    {
        return nextUnit(rng) * 2.0f - 1.0f;
    }

    void advance(const NoiseCoefficients&);
    float tone(float in, const NoiseCoefficients&, float amount);
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
    float pinkDirect = 0.0f, pinkScale = 1.0f;

    // Brown is white through a pole placed below the audible band, so
    // everything that can be heard of it is already on the 6 dB skirt. The
    // blocker after it is what stops the walk wandering off towards a rail.
    float brownPole = 0.0f, brownBlock = 0.0f, brownScale = 1.0f;

    // A click is the difference of two decays rather than an impulse: it
    // leaves zero at the sample it is struck on and rises into its peak, so
    // there is no single-sample spike to be caught by anything downstream.
    float geigerChance = 0.0f, geigerFast = 0.0f, geigerSlow = 0.0f, geigerScale = 1.0f;

    float toneG = 0.0f;
    // What the tilt does to the level of a flat spectrum at either end of the
    // knob, less one. Taking it back out is what keeps TONE a tone control
    // rather than a second level knob — see tone().
    float toneHighTrim = 0.0f, toneLowTrim = 0.0f;

    float fadeStep = 1.0f;

    void prepare(double sampleRate);
};

inline void NoiseChannel::advance(const NoiseCoefficients& c)
{
    const auto w = white();
    out[static_cast<size_t>(NoiseSource::white)] = w;

    auto sum = c.pinkDirect * w;
    for (size_t i = 0; i < pink.size(); ++i)
    {
        pink[i] = c.pinkPole[i] * pink[i] + c.pinkGain[i] * w;
        sum += pink[i];
    }
    out[static_cast<size_t>(NoiseSource::pink)] = sum * c.pinkScale;

    brown = c.brownPole * brown + w;
    // A one-pole blocker, written out rather than reusing the warp's because
    // this one runs on a signal that is almost entirely below its corner.
    blockOut = brown - blockIn + c.brownBlock * blockOut;
    blockIn = brown;
    out[static_cast<size_t>(NoiseSource::brown)] = blockOut * c.brownScale;

    auto excite = 0.0f;
    if (nextUnit(events) < c.geigerChance)
    {
        // Half the clicks are the full size and half are anywhere down to half
        // of it, which is what stops a train of identical impulses reading as a
        // tone at the event rate.
        const auto size = 0.5f + 0.5f * nextUnit(events);
        excite = (events & 0x10000u) != 0u ? size : -size;
    }
    geigerFast = c.geigerFast * geigerFast + excite;
    geigerSlow = c.geigerSlow * geigerSlow + excite;
    out[static_cast<size_t>(NoiseSource::geiger)] = (geigerSlow - geigerFast) * c.geigerScale;
}

// The tilt. Below the pivot and above it are scaled against each other, so the
// two halves always add back up to the signal that went in: at zero this
// returns its input exactly, which is what lets a patch leave TONE alone and
// know it is hearing the generator rather than a filter set to flat.
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

    // STEREO is decorrelation, not width: at nothing the two channels are the
    // same samples and the module is mono to the bit, at full they are two
    // streams that share no state. In between is an equal-power blend of the
    // two, so the level holds while the correlation falls off as the square
    // root of what is left — the same law Forge pans a source with.
    bool audible() const { return gate > 0.0f; }

    NoisePair render(const NoiseCoefficients& c, bool enabled, int wanted, float toneAmount,
                     float stereo)
    {
        wanted = juce::jlimit(0, noiseSourceCount - 1, wanted);
        if (wanted != source)
        {
            // Fading out of a source that was itself still fading in would need
            // a third stream. The one part way through is taken as where the
            // fade starts from instead, which is inaudible at six milliseconds
            // and keeps the state to two generators.
            previous = source;
            source = wanted;
            fade = 0.0f;
        }

        x.advance(c);
        y.advance(c);

        const auto chosen = [this] (const NoiseChannel& channel)
        {
            const auto now = channel.out[static_cast<size_t>(source)];
            if (fade >= 1.0f) return now;
            return now * fade + channel.out[static_cast<size_t>(previous)] * (1.0f - fade);
        };

        // The tilt is linear, so a channel filtering its own blend is the same
        // answer as filtering after the two are mixed, for state that belongs
        // to the channel that owns it.
        const auto direct = x.tone(chosen(x), c, toneAmount);
        const auto spread = y.tone(chosen(y), c, toneAmount);
        if (fade < 1.0f) fade = juce::jmin(1.0f, fade + c.fadeStep);

        gate = juce::jlimit(0.0f, 1.0f, gate + (enabled ? c.fadeStep : -c.fadeStep));

        const auto width = juce::jlimit(0.0f, 1.0f, stereo);
        const auto kept = std::sqrt(1.0f - width);
        const auto added = std::sqrt(width);
        return {direct * gate, (direct * kept + spread * added) * gate};
    }
};

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
    const auto brownCorner = 8.0;
    brownPole = static_cast<float>(std::exp(-2.0 * juce::MathConstants<double>::pi * brownCorner / rate));
    brownBlock = brownPole;

    // --- Geiger ---------------------------------------------------------------
    geigerChance = static_cast<float>(noiseGeigerEventsPerSecond / rate);
    geigerFast = static_cast<float>(std::exp(-1.0 / (rate * 0.0004)));
    geigerSlow = static_cast<float>(std::exp(-1.0 / (rate * 0.0035)));

    // --- Levels ---------------------------------------------------------------
    //
    // Measured rather than derived. Three of the four are white through
    // something, and what that something does to the level depends on the pole
    // it was just given, on the blocker after it and — for the clicks — on
    // where the difference of two decays happens to peak. A second of each,
    // from a fixed seed, is the one answer that cannot disagree with the
    // arithmetic above, and it runs here rather than anywhere near the audio
    // thread.
    //
    // The target is white's own level, so changing source changes the colour
    // and not the loudness, and so a patch written when NOISE was a hiss knob
    // is exactly as loud as it was.
    //
    // Warmed up first, and measured over long enough to settle. Brown's
    // integrator spends half a second filling from nothing, and the clicks are
    // sparse enough that a short window catches an unrepresentative number of
    // them — both would read as a source that is quiet rather than as a probe
    // that was too short.
    pinkScale = brownScale = geigerScale = 1.0f;
    {
        NoiseChannel probe;
        probe.start(0x1234567u);
        const auto warmup = static_cast<int>(rate * 0.5);
        for (int i = 0; i < warmup; ++i) probe.advance(*this);

        const auto samples = static_cast<int>(rate * 4.0);
        std::array<double, noiseSourceCount> power {};
        for (int i = 0; i < samples; ++i)
        {
            probe.advance(*this);
            for (size_t s = 0; s < power.size(); ++s)
                power[s] += static_cast<double>(probe.out[s]) * probe.out[s];
        }
        const auto measure = [&power, samples] (size_t s)
        {
            return std::sqrt(power[s] / std::max(1, samples));
        };
        const auto target = measure(static_cast<size_t>(NoiseSource::white));
        const auto against = [target, &measure] (size_t s)
        {
            const auto level = measure(s);
            return static_cast<float>(level > 1.0e-9 ? target / level : 1.0);
        };
        pinkScale = against(static_cast<size_t>(NoiseSource::pink));
        brownScale = against(static_cast<size_t>(NoiseSource::brown));
        geigerScale = against(static_cast<size_t>(NoiseSource::geiger));
    }

    // --- Tone -----------------------------------------------------------------
    //
    // The pivot, and what tilting fully either way does to the level of a flat
    // spectrum. Both ends are measured off the very filter tone() runs, by
    // walking its response across the band: a pivot this far down the spectrum
    // means nearly all of a noise source's power is on the upper half, so a
    // full bright tilt is most of 6 dB of level and a full dark one is a long
    // way below unity. Those two numbers are what tone() divides back out.
    toneG = static_cast<float>(1.0 - std::exp(-2.0 * juce::MathConstants<double>::pi
                                              * noiseTonePivotHz / rate));
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
}
}
