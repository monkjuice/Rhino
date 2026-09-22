// The noise module: what each of the four sources actually is, that a voice
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

// --- The four sources ---------------------------------------------------------

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

    // And that they are ordered, which is the thing that would still be wrong
    // if all three slopes drifted together.
    require(white > pink && pink > brown, "the three colours are ordered by slope");

    // The slope holds at another rate too. The published pink coefficients are
    // for 44.1 kHz, so a filter that used them as printed would sit at the
    // wrong frequencies here and the slope would bend.
    const auto pinkAt96 = slopeDbPerOctave(renderedSpectrum(noiseOnly(1), 57, 96000.0), 96000.0);
    requireClose(static_cast<float>(pinkAt96), -3.0f, 0.6f,
                 "pink noise keeps its slope at another sample rate");

    // Every source comes out at about the level of the one before it, so
    // changing the source changes the colour and not the loudness.
    const auto reference = rmsOf(renderNoise(noiseOnly(0), 48000).left);
    for (int source = 1; source < noiseSourceCount; ++source)
    {
        const auto level = rmsOf(renderNoise(noiseOnly(source), 48000).left);
        require(level > reference * 0.7 && level < reference * 1.4,
                "every source is rendered at about white's level");
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
        for (int source = 0; source < noiseSourceCount; ++source)
            requireText(parameter->choices[source], noiseSourceName(source),
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
    spectrumSuite();
    geigerSuite();
    stereoSuite();
    voiceSuite();
    toneSuite();
    clickSuite();
    parameterSuite();
}
}
