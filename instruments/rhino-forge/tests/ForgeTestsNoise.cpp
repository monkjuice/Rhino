// The noise module: what each of the nineteen sources actually is, that a voice
// hisses on its own, and that nothing about changing one of them clicks.
//
// Everything here is measured off rendered audio rather than read out of
// ForgeNoise.h. A slope check that asked the filter what its slope was could
// not disagree with it; one that renders a second of it, transforms it and
// fits a line through the octave bands can, which is the whole reason the
// checks are worth having. The same standard the tuning checks are held to.
#include "ForgeTestSpectrum.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace rhino::forge::tests
{
namespace
{
// Nothing sounding but the noise module, straight to the output rather than
// through the filter, and an envelope that is flat by the time anything is
// measured. Every source is rendered against this, so a difference between two
// measurements is a difference between two generators.
Patch noiseOnly(int source)
{
    Patch patch;
    patch.a.enable = 0.0f;
    patch.b.enable = 0.0f;
    patch.subEnable = 0.0f;
    patch.filterEnable = 0.0f;
    patch.noiseEnable = 1.0f;
    patch.noiseLevel = 1.0f;
    patch.noiseSource = static_cast<float>(source);
    patch.routeNoise = 0.0f;
    // Flat and full by the time the window opens. A decay still running under
    // the measurement would read as a spectrum sliding, and a sustain below
    // full would make two sources measured at different moments look like two
    // sources at different levels.
    patch.envs[ampEnv] = {0.001f, 0.001f, 1.0f, 0.35f};
    return patch;
}

struct Rendered
{
    std::vector<float> left, right;
};

// A patch held for as long as is asked for, past the attack. The Core is on the
// heap for the reason the engine suite's Processors are: it carries sixteen
// voices and a filter per channel, and this file builds several of them.
Rendered renderNoise(const Patch& patch, int samples, std::initializer_list<int> notes = {57},
                     double sampleRate = 48000.0, int skip = 4096)
{
    auto core = std::make_unique<Core>();
    core->initialise(sampleRate);
    for (const auto note : notes) core->noteOn(note, 1.0f, patch);
    for (int i = 0; i < skip; ++i)
    {
        auto l = 0.0f, r = 0.0f;
        core->renderSample(patch, l, r);
    }

    Rendered out;
    out.left.resize(static_cast<size_t>(samples));
    out.right.resize(static_cast<size_t>(samples));
    for (int i = 0; i < samples; ++i)
    {
        auto l = 0.0f, r = 0.0f;
        core->renderSample(patch, l, r);
        out.left[static_cast<size_t>(i)] = l;
        out.right[static_cast<size_t>(i)] = r;
    }
    return out;
}

double rmsOf(const std::vector<float>& data)
{
    auto sum = 0.0;
    for (const auto sample : data) sum += static_cast<double>(sample) * sample;
    return std::sqrt(sum / std::max<size_t>(1, data.size()));
}

double peakOf(const std::vector<float>& data)
{
    auto peak = 0.0;
    for (const auto sample : data) peak = std::max(peak, std::abs(static_cast<double>(sample)));
    return peak;
}

// The largest step between neighbouring samples, over a span of one. This is
// what a click is: everything else about noise is already a large number.
double largestStep(const std::vector<float>& data, int from, int to)
{
    auto worst = 0.0;
    for (int i = std::max(1, from); i < std::min(to, static_cast<int>(data.size())); ++i)
        worst = std::max(worst, std::abs(static_cast<double>(data[static_cast<size_t>(i)])
                                         - data[static_cast<size_t>(i - 1)]));
    return worst;
}

// How many different numbers came back. Everything between the generator and
// here is a scalar multiply, so a source that puts out sixteen levels still
// puts out sixteen once it has been panned, levelled and summed -- which is
// what makes this a fair way to ask whether something is quantised.
int distinctValues(const std::vector<float>& data)
{
    std::vector<float> sorted(data);
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    return static_cast<int>(sorted.size());
}

// The tallest bin within a few per cent of a frequency. Not peakAt, which wants
// a bin and trusts it: a resonator does not land where the arithmetic says to
// the bin, and a partial missed by two bins reads as no partial at all.
double partialAt(const std::vector<double>& magnitude, double hz, double sampleRate)
{
    const auto scale = static_cast<double>(spectrumSize) / sampleRate;
    const auto first = std::max(1, static_cast<int>(hz * 0.96 * scale));
    const auto last = std::min(static_cast<int>(magnitude.size()) - 1,
                               static_cast<int>(hz * 1.04 * scale));
    auto best = 0.0;
    for (int bin = first; bin <= last; ++bin) best = std::max(best, magnitude[static_cast<size_t>(bin)]);
    return best;
}

// How many separate excursions past a threshold there are: the number of
// events, rather than how tall they are.
//
// This exists because crest could not tell the two event sources apart, and
// measured them the wrong way round. Crackle's clicks are a fifth the length of
// geiger's, so a crackle event stands further above its own average even though
// there are twenty-five times as many of them -- crest read 5.0 against 4.7 and
// called the dense one sparser. Counting is the thing the claim was always
// about.
//
// Hysteresis, so one event with a ripple on it is one event.
int burstCount(const std::vector<float>& data, double level)
{
    const auto open = level * 1.5, close = level * 0.5;
    auto bursts = 0;
    auto inside = false;
    for (const auto sample : data)
    {
        const auto magnitude = std::abs(static_cast<double>(sample));
        if (!inside && magnitude > open) { inside = true; ++bursts; }
        else if (inside && magnitude < close) inside = false;
    }
    return bursts;
}

double correlationOf(const std::vector<float>& a, const std::vector<float>& b)
{
    auto sa = 0.0, sb = 0.0, sab = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        sa += static_cast<double>(a[i]) * a[i];
        sb += static_cast<double>(b[i]) * b[i];
        sab += static_cast<double>(a[i]) * b[i];
    }
    return sab / std::sqrt(std::max(1.0e-30, sa * sb));
}

// Mean power per bin across a band, which is the spectral density rather than
// the total energy in it. That is the reading a slope is defined against: a
// flat density is white, and an octave holding twice the bins of the one below
// it would otherwise make white look as though it rose.
double bandDensityDb(const std::vector<double>& magnitude, double low, double high, double sampleRate)
{
    const auto scale = static_cast<double>(spectrumSize) / sampleRate;
    const auto first = std::max(1, static_cast<int>(std::ceil(low * scale)));
    const auto last = std::min(static_cast<int>(magnitude.size()) - 1,
                               static_cast<int>(std::floor(high * scale)));
    auto sum = 0.0;
    auto count = 0;
    for (int bin = first; bin <= last; ++bin)
    {
        sum += magnitude[static_cast<size_t>(bin)] * magnitude[static_cast<size_t>(bin)];
        ++count;
    }
    return 10.0 * std::log10(std::max(1.0e-30, sum / std::max(1, count)));
}

// A line fitted through six octave bands rather than a ratio taken between two.
// Noise is noisy: any single pair of readings carries the variance of the two
// bands it happened to use, and a fit through the span carries a sixth of it.
//
// 250 Hz to 8 kHz because that is where the claim is meant to hold. Below it
// brown's blocker is in the way and above it the band runs out.
double slopeDbPerOctave(const std::vector<double>& magnitude, double sampleRate)
{
    constexpr int points = 6;
    constexpr double centres[points] {250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0};
    const auto edge = std::sqrt(2.0);
    auto sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
    for (int i = 0; i < points; ++i)
    {
        const auto x = std::log2(centres[i] / centres[0]);
        const auto y = bandDensityDb(magnitude, centres[i] / edge, centres[i] * edge, sampleRate);
        sx += x;
        sy += y;
        sxx += x * x;
        sxy += x * y;
    }
    return (points * sxy - sx * sy) / (points * sxx - sx * sx);
}

// --- The colours --------------------------------------------------------------

void spectrumSuite()
{
    constexpr double rate = 48000.0;

    // The claim each colour is named for, measured. White is flat, pink falls
    // at 3 dB an octave and brown at 6 — which is the whole of what tells them
    // apart, and the only thing about them a listener can check.
    const auto white = slopeDbPerOctave(renderedSpectrum(noiseOnly(0), 57, rate), rate);
    const auto pink = slopeDbPerOctave(renderedSpectrum(noiseOnly(1), 57, rate), rate);
    const auto brown = slopeDbPerOctave(renderedSpectrum(noiseOnly(2), 57, rate), rate);

    requireClose(static_cast<float>(white), 0.0f, 0.5f, "white noise is flat across the band");
    requireClose(static_cast<float>(pink), -3.0f, 0.6f, "pink noise falls at 3 dB an octave");
    requireClose(static_cast<float>(brown), -6.0f, 0.8f, "brown noise falls at 6 dB an octave");

    // The two that go the other way. Differentiating lifts a spectrum by 6 dB
    // an octave, so white becomes violet and pink becomes blue -- which is a
    // claim about arithmetic that either holds in the rendered audio or does
    // not.
    const auto blue = slopeDbPerOctave(
        renderedSpectrum(noiseOnly(static_cast<int>(NoiseSource::blue)), 57, rate), rate);
    const auto violet = slopeDbPerOctave(
        renderedSpectrum(noiseOnly(static_cast<int>(NoiseSource::violet)), 57, rate), rate);
    requireClose(static_cast<float>(blue), 3.0f, 0.6f, "blue noise rises at 3 dB an octave");
    requireClose(static_cast<float>(violet), 6.0f, 0.8f, "violet noise rises at 6 dB an octave");

    // And that they are ordered, which is the thing that would still be wrong
    // if all five slopes drifted together.
    require(violet > blue && blue > white && white > pink && pink > brown,
            "the five sloped colours are ordered");

    // Grey is the one colour that is not a slope. It is weighted to sound flat
    // rather than measure flat, which means lifting both ends of the band
    // against the middle -- so what it is held to is that shape, not a line
    // through it.
    const auto greyBand = renderedSpectrum(
        noiseOnly(static_cast<int>(NoiseSource::grey)), 57, rate);
    const auto greyLow = bandDensityDb(greyBand, 80.0, 200.0, rate);
    const auto greyMid = bandDensityDb(greyBand, 1500.0, 3500.0, rate);
    const auto greyHigh = bandDensityDb(greyBand, 8000.0, 14000.0, rate);
    require(greyLow > greyMid + 4.0 && greyHigh > greyMid + 1.0,
            "grey lifts both ends of the band against the middle");

    // The slope holds at another rate too. The published pink coefficients are
    // for 44.1 kHz, so a filter that used them as printed would sit at the
    // wrong frequencies here and the slope would bend.
    const auto pinkAt96 = slopeDbPerOctave(renderedSpectrum(noiseOnly(1), 57, 96000.0), 96000.0);
    requireClose(static_cast<float>(pinkAt96), -3.0f, 0.6f,
                 "pink noise keeps its slope at another sample rate");

}

// --- Every source, whatever it is ---------------------------------------------
//
// The checks that have to hold for all nineteen. A source that fails one of
// these is broken however good it sounds, and a loop is the only way to be sure
// the nineteenth got the same attention as the first.
void everySourceSuite()
{
    const auto reference = rmsOf(renderNoise(noiseOnly(static_cast<int>(NoiseSource::white)),
                                             48000).left);

    for (int source = 0; source < noiseSourceCount; ++source)
    {
        const auto name = juce::String(noiseSourceFullName(source));
        const auto rendered = renderNoise(noiseOnly(source), 48000);

        require(peakOf(rendered.left) > 0.0, (name + " makes sound at all").toRawUTF8());
        require(peakOf(rendered.left) < 1.0, (name + " stays inside full scale").toRawUTF8());

        auto finite = true;
        for (const auto sample : rendered.left)
            if (!std::isfinite(sample)) { finite = false; break; }
        require(finite, (name + " renders finite samples").toRawUTF8());

        // Changing source changes the colour, not the loudness. The table that
        // holds this is measured at prepare rather than derived, so this is
        // also the check that the measurement happened and landed.
        const auto level = rmsOf(rendered.left);
        require(level > reference * 0.7 && level < reference * 1.4,
                (name + " is rendered at about white's level").toRawUTF8());

        // Mono to the bit at no width, for every source and not only for the
        // ones built symmetrically.
        require(rendered.left == rendered.right,
                (name + " is the same in both channels at no width").toRawUTF8());
    }
}

// --- The characters -----------------------------------------------------------
//
// One check each for the thing a source exists to do, chosen so that a source
// which had quietly become an ordinary hiss would fail it.
void characterSuite()
{
    constexpr double rate = 48000.0;

    // BIT holds and quantises at once. Quantising is the half that is easy to
    // lose -- a decimator alone still puts out thousands of distinct values --
    // so what is counted is how many different numbers came back.
    const auto bit = renderNoise(noiseOnly(static_cast<int>(NoiseSource::bit)), 24000);
    const auto white = renderNoise(noiseOnly(static_cast<int>(NoiseSource::white)), 24000);
    require(distinctValues(bit.left) < 64, "bit crush quantises to a handful of levels");
    require(distinctValues(white.left) > 1000, "white is not quantised, which is the contrast");

    // ALPHA is a shift register: one bit, held. Two values and no more.
    const auto alpha = renderNoise(noiseOnly(static_cast<int>(NoiseSource::alpha)), 24000);
    require(distinctValues(alpha.left) <= 4, "alpha is a shift register's two levels");

    // HUM is mains, so the tallest thing in it is the mains frequency and not
    // the note being played.
    const auto hum = renderedSpectrum(noiseOnly(static_cast<int>(NoiseSource::hardwareHum)),
                                      57, rate);
    const auto mains = loudestPeak(hum, rate);
    requireClose(static_cast<float>(mains.frequency), 50.0f, 2.0f,
                 "hardware hum peaks at the mains frequency");

    // METAL is a struck object rather than a note, which is a claim about where
    // its partials sit: a harmonic series would put one at twice the first.
    const auto metal = renderedSpectrum(noiseOnly(static_cast<int>(NoiseSource::metallic)),
                                        57, rate);
    const auto first = loudestPeak(metal, rate);
    require(first.frequency > 400.0 && first.frequency < 1000.0,
            "the metallic bank rings where it is tuned");
    const auto inharmonic = partialAt(metal, first.frequency * 1.71, rate);
    const auto octave = partialAt(metal, first.frequency * 2.0, rate);
    require(inharmonic > octave * 2.0,
            "the metallic bank's partials are inharmonic rather than an octave apart");

    // POLY HP takes the body out at the source rather than turning it down, so
    // the difference between it and POLY is in the bottom of the band and not
    // across the whole of it. That is the brief's requirement that the two be
    // separate sources rather than one source and the tone knob.
    const auto poly = renderedSpectrum(noiseOnly(static_cast<int>(NoiseSource::vintagePoly)),
                                       57, rate);
    const auto polyHp = renderedSpectrum(noiseOnly(static_cast<int>(NoiseSource::vintagePolyHp)),
                                         57, rate);
    const auto lowGap = bandDensityDb(poly, 60.0, 180.0, rate)
                      - bandDensityDb(polyHp, 60.0, 180.0, rate);
    const auto topGap = std::abs(bandDensityDb(poly, 3000.0, 6000.0, rate)
                                 - bandDensityDb(polyHp, 3000.0, 6000.0, rate));
    require(lowGap > 12.0, "the high-passed poly drops the body rather than the lot");
    require(topGap < lowGap * 0.5, "and leaves the top where the plain one has it");

    // The two event sources are the same process at settings far enough apart
    // to be two sources. Both are sparser than hiss; only one is countable.
    const auto geiger = renderNoise(noiseOnly(static_cast<int>(NoiseSource::geiger)), 48000);
    const auto crackle = renderNoise(noiseOnly(static_cast<int>(NoiseSource::crackle)), 48000);
    const auto crest = [] (const std::vector<float>& data)
    {
        return peakOf(data) / std::max(1.0e-9, rmsOf(data));
    };
    //
    // Both stand far above continuous hiss, which is what makes them events
    // rather than a spectrum: white's crest is about 1.7 and each of these is
    // nearer 5.
    const auto hiss = crest(white.left);
    require(crest(geiger.left) > hiss * 2.0, "geiger is events where white is continuous");
    require(crest(crackle.left) > hiss * 2.0, "so is crackle");

    // What tells the two apart is how many of them there are, which is the one
    // thing crest does not measure. A second of audio, so a count is a rate.
    const auto geigerRate = burstCount(geiger.left, rmsOf(geiger.left));
    const auto crackleRate = burstCount(crackle.left, rmsOf(crackle.left));
    require(crackleRate > geigerRate * 4,
            "crackle is a surface where geiger is countable");

    // And geiger strikes at about the rate it says it does. Counting the events
    // in the rendered audio is the only check that the process runs at the rate
    // the constant names rather than at some multiple of it.
    require(geigerRate > noiseGeigerEventsPerSecond * 0.5
                && geigerRate < noiseGeigerEventsPerSecond * 1.5,
            "geiger strikes at about the rate it declares");
}

// --- What the panel lists -----------------------------------------------------
//
// The stored order and the shown order are two different orders, and the only
// place they meet is a pair of functions. These are the checks that keep that
// pair honest; they render nothing.
void orderSuite()
{
    std::array<int, noiseSourceCount> seen {};
    for (int position = 0; position < noiseSourceCount; ++position)
    {
        const auto source = noiseSourceAt(position);
        require(source >= 0 && source < noiseSourceCount,
                "every position in the shown order names a real source");
        ++seen[static_cast<size_t>(source)];
        requireClose(static_cast<float>(noiseSourcePosition(source)),
                     static_cast<float>(position), 0.0001f,
                     "a source's position round-trips back to the source");
    }
    for (int source = 0; source < noiseSourceCount; ++source)
        require(seen[static_cast<size_t>(source)] == 1,
                (juce::String(noiseSourceFullName(source))
                 + " appears exactly once in the shown order").toRawUTF8());

    // A family is a run. If it were not, the arrows would walk out of a family
    // and back into it, and the menu's groups would not be the same thing as
    // the field's neighbours.
    auto changes = 0;
    for (int position = 1; position < noiseSourceCount; ++position)
        if (noiseCategoryOf(static_cast<NoiseSource>(noiseSourceAt(position)))
            != noiseCategoryOf(static_cast<NoiseSource>(noiseSourceAt(position - 1))))
            ++changes;
    require(changes == noiseCategoryCount - 1,
            "each family is one unbroken run of the shown order");

    // The four this module opened with have not moved, which is the whole of
    // what a preset written before the other fifteen existed depends on.
    requireText(noiseSourceName(0), "WHITE", "white is still nothing");
    requireText(noiseSourceName(1), "PINK", "pink is still one");
    requireText(noiseSourceName(2), "BROWN", "brown is still two");
    requireText(noiseSourceName(3), "GEIGER", "geiger is still three");

    // Every source has both names and neither is empty, because a field with a
    // blank on it is a source nobody can choose.
    for (int source = 0; source < noiseSourceCount; ++source)
    {
        require(juce::String(noiseSourceName(source)).isNotEmpty(),
                "every source has a short name");
        require(juce::String(noiseSourceFullName(source)).isNotEmpty(),
                "every source has a long name");
    }
}

void geigerSuite()
{
    const auto clicks = renderNoise(noiseOnly(3), 48000);
    const auto hiss = renderNoise(noiseOnly(0), 48000);

    // Sparse, which is the whole character of it: the same power arriving in
    // occasional events rather than continuously means a much taller peak
    // against the same average.
    const auto clickCrest = peakOf(clicks.left) / std::max(1.0e-9, rmsOf(clicks.left));
    const auto hissCrest = peakOf(hiss.left) / std::max(1.0e-9, rmsOf(hiss.left));
    require(clickCrest > hissCrest * 2.0, "geiger is sparse where white is continuous");

    // Shaped rather than struck. A click is the difference of two decays, so it
    // leaves zero at the sample it starts on and rises into its peak over the
    // best part of a millisecond — nothing in it is a single-sample spike,
    // whatever the peak reaches.
    const auto step = largestStep(clicks.left, 0, static_cast<int>(clicks.left.size()));
    require(step < peakOf(clicks.left) * 0.25,
            "a geiger click is shaped rather than a one-sample spike");

    require(peakOf(clicks.left) < 1.0, "geiger stays inside full scale");
}

// --- Stereo -------------------------------------------------------------------

void stereoSuite()
{
    // At nothing, the two channels are the same samples. Not merely correlated:
    // the same numbers, so summing the module to mono cannot cancel anything
    // and a patch written before STEREO existed is unchanged.
    auto patch = noiseOnly(0);
    patch.noiseStereo = 0.0f;
    const auto mono = renderNoise(patch, 24000);
    require(mono.left == mono.right, "at no width both channels are the same samples");

    // At the top they share no state, so they measure as uncorrelated.
    patch.noiseStereo = 1.0f;
    const auto wide = renderNoise(patch, 48000);
    require(std::abs(correlationOf(wide.left, wide.right)) < 0.05,
            "at full width the two channels are uncorrelated");

    // And in between, the correlation is what the equal-power blend says it is:
    // the square root of what is left of the correlated part.
    patch.noiseStereo = 0.5f;
    const auto half = renderNoise(patch, 48000);
    requireClose(static_cast<float>(correlationOf(half.left, half.right)),
                 std::sqrt(0.5f), 0.05f, "width blends the correlation predictably");

    // Equal power, so opening the width does not also turn the module up. Both
    // channels hold their level across the whole sweep.
    const auto narrowRight = rmsOf(mono.right);
    const auto wideRight = rmsOf(wide.right);
    require(wideRight > narrowRight * 0.9 && wideRight < narrowRight * 1.1,
            "widening does not change the level of either channel");
}

// --- The voice ----------------------------------------------------------------

void voiceSuite()
{
    const auto patch = noiseOnly(0);

    // Every note hisses on its own. Two voices of one stream shared out would
    // add as amplitudes and come out twice as loud; two independent ones add as
    // powers and come out by the square root of two, which is what a chord
    // thickening rather than doubling sounds like.
    const auto one = rmsOf(renderNoise(patch, 48000, {57}).left);
    const auto two = rmsOf(renderNoise(patch, 48000, {57, 64}).left);
    requireClose(static_cast<float>(two / std::max(1.0e-9, one)), std::sqrt(2.0f), 0.08f,
                 "two voices hiss independently rather than together");

    // And the same phrase rendered twice is the same audio. A generator seeded
    // from a clock would be as random as this one and would make an offline
    // bounce differ from itself, which is the one place noise must not be
    // random at all.
    const auto first = renderNoise(patch, 8192, {57, 64});
    const auto second = renderNoise(patch, 8192, {57, 64});
    require(first.left == second.left && first.right == second.right,
            "the same notes render the same noise every time");
}

// --- Tone ---------------------------------------------------------------------

void toneSuite()
{
    constexpr double rate = 48000.0;
    auto patch = noiseOnly(0);

    // Flat at nothing. This is the same measurement the colour checks make, and
    // it is here to say that the tilt is genuinely out of the way rather than a
    // filter that happens to be set near flat.
    patch.noiseTone = 0.0f;
    const auto neutral = slopeDbPerOctave(renderedSpectrum(patch, 57, rate), rate);
    requireClose(static_cast<float>(neutral), 0.0f, 0.5f, "tone at nothing leaves white flat");

    // Dark tilts it down and bright tilts it up, both by about the 6 dB an
    // octave a first-order tilt is worth at the ends.
    patch.noiseTone = -1.0f;
    const auto dark = slopeDbPerOctave(renderedSpectrum(patch, 57, rate), rate);
    patch.noiseTone = 1.0f;
    const auto bright = slopeDbPerOctave(renderedSpectrum(patch, 57, rate), rate);
    require(dark < neutral - 2.0 && bright > neutral + 0.5,
            "tone tilts the spectrum either way about its pivot");

    // A tone control rather than a second level knob. Tilting a flat spectrum
    // is mostly a level change unless the trim measured at prepare takes it
    // back out, and a knob that changed the loudness by 10 dB on the way to
    // changing the colour would be unusable.
    const auto flatLevel = rmsOf(renderNoise(noiseOnly(0), 48000).left);
    patch.noiseTone = -1.0f;
    const auto darkLevel = rmsOf(renderNoise(patch, 48000).left);
    patch.noiseTone = 1.0f;
    const auto brightLevel = rmsOf(renderNoise(patch, 48000).left);
    //
    // Two decibels either way is the standard, and it is met with a great deal
    // to spare: measured at 48 kHz the two ends land within one per cent of the
    // middle. The room is left for another pivot or another rate, not because
    // the trim needs it.
    const auto within = [flatLevel] (double level)
    {
        return level > flatLevel * 0.8 && level < flatLevel * 1.25;
    };
    require(within(darkLevel) && within(brightLevel),
            "the tilt holds its level within 2 dB across the knob");
}

// --- Nothing clicks -----------------------------------------------------------

// A patch rendered with something changed part way through, so the join can be
// measured against the two steady stretches either side of it.
Rendered renderChange(const Patch& before, const Patch& after, int half)
{
    auto core = std::make_unique<Core>();
    core->initialise(48000.0);
    core->noteOn(57, 1.0f, before);
    for (int i = 0; i < 4096; ++i)
    {
        auto l = 0.0f, r = 0.0f;
        core->renderSample(before, l, r);
    }

    Rendered out;
    out.left.resize(static_cast<size_t>(half * 2));
    out.right.resize(static_cast<size_t>(half * 2));
    for (int i = 0; i < half * 2; ++i)
    {
        auto l = 0.0f, r = 0.0f;
        core->renderSample(i < half ? before : after, l, r);
        out.left[static_cast<size_t>(i)] = l;
        out.right[static_cast<size_t>(i)] = r;
    }
    return out;
}

void clickSuite()
{
    constexpr int half = 12000;

    // Brown to geiger is the change most able to click. Brown wanders slowly
    // and so is rarely anywhere near zero when you leave it; geiger is silence
    // with occasional clicks in it. Dropped rather than crossed over, the join
    // would be a step of most of the module's peak in a single sample — and
    // neither source has steps of its own to hide it in, which is exactly why
    // this pair is the one measured.
    const auto changed = renderChange(noiseOnly(2), noiseOnly(3), half);
    const auto settled = std::max(largestStep(changed.left, 1000, half - 1000),
                                  largestStep(changed.left, half + 1000,
                                              static_cast<int>(changed.left.size())));
    const auto join = largestStep(changed.left, half - 200, half + 600);
    require(join < settled * 3.0, "changing source crosses over rather than stepping");

    // The module's own power switch, which is a ramp for the same reason. Hiss
    // arriving at full level in one sample is a click whichever source it is.
    auto off = noiseOnly(2);
    off.noiseEnable = 0.0f;
    const auto stopped = renderChange(noiseOnly(2), off, half);
    const auto running = largestStep(stopped.left, 1000, half - 1000);
    require(largestStep(stopped.left, half - 200, half + 600) < running * 3.0,
            "switching the module off ramps down rather than stopping dead");

    // And it does reach silence, rather than ramping to something small. The
    // ramp is six milliseconds; a tenth of a second later there must be nothing
    // at all.
    const auto tail = std::vector<float>(stopped.left.begin() + half + 4800, stopped.left.end());
    require(peakOf(tail) == 0.0, "a switched-off module renders exact silence");

    // The other way round, which is the one a player does by hand: switching
    // the module on part way through a held note.
    auto quiet = noiseOnly(0);
    quiet.noiseEnable = 0.0f;
    const auto started = renderChange(quiet, noiseOnly(0), half);
    require(largestStep(started.left, half - 200, half + 600)
                < largestStep(started.left, half + 2000, static_cast<int>(started.left.size())) * 1.5,
            "switching the module on ramps up rather than starting dead");

    // And then every source, both ways, against white. Nineteen sources make
    // three hundred and forty-two possible changes and there is no sense in
    // rendering all of them, but a source that steps will step against anything
    // -- so each one is entered and left once, which catches it either way.
    //
    // A source arriving is the harder direction: its stage is reset as it comes
    // in, so anything that started from its own silence rather than from the
    // colour feeding it would show up here as a join steeper than the settled
    // signal either side.
    for (int source = 1; source < noiseSourceCount; ++source)
    {
        const auto name = juce::String(noiseSourceFullName(source));
        for (int direction = 0; direction < 2; ++direction)
        {
            const auto from = direction == 0 ? 0 : source;
            const auto to = direction == 0 ? source : 0;
            const auto moved = renderChange(noiseOnly(from), noiseOnly(to), half);
            const auto steady = std::max(largestStep(moved.left, 1500, half - 1500),
                                         largestStep(moved.left, half + 1500,
                                                     static_cast<int>(moved.left.size())));
            const auto seam = largestStep(moved.left, half - 200, half + 600);
            require(seam < steady * 3.0,
                    (name + (direction == 0 ? " arrives without a step"
                                            : " departs without a step")).toRawUTF8());
        }
    }
}

// --- What the panel and the host see ------------------------------------------

void parameterSuite()
{
    auto processorStorage = std::make_unique<Processor>();
    auto& processor = *processorStorage;

    // Every source the engine has is a choice a host can name, in the engine's
    // own order, so the index automation writes is the source that renders.
    const auto* parameter = dynamic_cast<const juce::AudioParameterChoice*>(
        processor.state.getParameter("noiseSource"));
    require(parameter != nullptr, "the noise source is a choice rather than a stepped float");
    if (parameter != nullptr)
    {
        require(parameter->choices.size() == noiseSourceCount,
                "the source field offers every source the engine has");
        // In the engine's order, and by the long name. The order matters
        // because the value a host writes is the index the engine renders; the
        // name matters because a lane with room for "Vintage Poly HP" should
        // not be reading "POLY HP".
        for (int source = 0; source < noiseSourceCount; ++source)
            requireText(parameter->choices[source], noiseSourceFullName(source),
                        "the source field names the engine's own sources");
    }

    // The tilt says which way it is leaning as well as how far, and says that
    // the middle is nothing rather than a percentage that happens to be zero.
    requireText(textFor(processor, "noiseTone", 0.0f), "FLAT", "a centred tilt reads as flat");
    requireText(textFor(processor, "noiseTone", -0.5f), "DARK 50 %", "a dark tilt reads as dark");
    requireText(textFor(processor, "noiseTone", 1.0f), "BRIGHT 100 %",
                "a bright tilt reads as bright");

    // Both continuous controls are reachable from the matrix, and the source is
    // deliberately not: sweeping four unrelated generators is a stutter.
    const auto& list = destinations();
    require(static_cast<int>(list.size()) == destinationCount,
            "the destination list is as long as it says it is");
    requireText(list[static_cast<size_t>(noiseToneDestination)].id, "noiseTone",
                "the tilt is a modulation destination");
    requireText(list[static_cast<size_t>(noiseStereoDestination)].id, "noiseStereo",
                "the width is a modulation destination");
    for (const auto& entry : list)
        require(juce::String(entry.id) != "noiseSource",
                "the source itself is not a modulation destination");

    // The module opens where it always stood: white, flat, and one stream in
    // both ears, so a patch written before any of this existed is unchanged.
    requireClose(value(processor, "noiseSource"), 0.0f, 0.0001f, "the module opens on white");
    requireClose(value(processor, "noiseTone"), 0.0f, 0.0001f, "the module opens flat");
    requireClose(value(processor, "noiseStereo"), 0.0f, 0.0001f, "the module opens mono");
}
}

void noiseTests()
{
    orderSuite();
    spectrumSuite();
    everySourceSuite();
    characterSuite();
    geigerSuite();
    stereoSuite();
    voiceSuite();
    toneSuite();
    clickSuite();
    parameterSuite();
}
}
