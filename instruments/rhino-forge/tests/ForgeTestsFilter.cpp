// The filter: what is routed into it, what each of the thirty-four types does,
// and whether the response the panel draws is the response the voice renders.
#include "ForgeTestSupport.h"
#include "ForgeTestSpectrum.h"

#include <limits>

namespace rhino::forge::tests
{
namespace
{
void filterRoutingSuite()
{
    // A source that is routed into the filter is affected by the cutoff; one
    // that is not, is not. That is the whole point of the routing chips.
    constexpr int samples = 8192;
    constexpr int settled = 1024;
    juce::AudioBuffer<float> buffer(2, samples);

    const auto peakWithCutoff = [&buffer] (bool routed, float cutoff)
    {
        rhino::forge::Processor processor;
        soloSineOnA(processor);
        setValue(processor, "filterEnable", 1.0f);
        setValue(processor, "filterType", 0.0f);
        setValue(processor, "routeA", routed ? 1.0f : 0.0f);
        setValue(processor, "resonance", 0.0f);
        setValue(processor, "cutoff", cutoff);
        renderNote(processor, buffer);
        return rms(buffer, 0, settled);
    };

    // A 220 Hz note against a 60 Hz low pass: routed, it is heavily attenuated.
    const auto routedOpen = peakWithCutoff(true, 18000.0f);
    const auto routedClosed = peakWithCutoff(true, 60.0f);
    require(routedOpen > 0.0f, "a routed source is audible with the filter open");
    require(routedClosed < routedOpen * 0.25f, "closing the cutoff attenuates a routed source");

    const auto bypassedOpen = peakWithCutoff(false, 18000.0f);
    const auto bypassedClosed = peakWithCutoff(false, 60.0f);
    require(bypassedOpen > 0.0f, "an unrouted source is still audible");
    requireClose(bypassedClosed, bypassedOpen, bypassedOpen * 0.001f,
                 "the cutoff does not touch an unrouted source");

    // Routing is per source, so closing the filter on one leaves the other.
    rhino::forge::Processor split;
    soloSineOnA(split);
    setValue(split, "subEnable", 1.0f);
    setValue(split, "subLevel", 0.6f);
    setValue(split, "filterEnable", 1.0f);
    setValue(split, "cutoff", 60.0f);
    setValue(split, "routeA", 1.0f);
    setValue(split, "routeSub", 0.0f);
    renderNote(split, buffer);
    const auto subSurvives = rms(buffer, 0, settled);
    setValue(split, "routeSub", 1.0f);
    renderNote(split, buffer);
    require(rms(buffer, 0, settled) < subSurvives * 0.7f,
            "routing the sub into a closed filter removes it, while oscillator A's routing is unchanged");

    // Drive belongs to the filter, so it only touches what is routed there.
    rhino::forge::Processor driven;
    soloSineOnA(driven);
    setValue(driven, "filterEnable", 1.0f);
    setValue(driven, "cutoff", 18000.0f);
    setValue(driven, "routeA", 0.0f);
    renderNote(driven, buffer);
    const auto undriven = rms(buffer, 0, settled);
    setValue(driven, "drive", 1.0f);
    renderNote(driven, buffer);
    requireClose(rms(buffer, 0, settled), undriven, undriven * 0.001f,
                 "drive does not touch a source that bypasses the filter");

    // Switching the filter module off bypasses the drive with it: drive is a
    // control of that module, not a master saturator that survives it.
    rhino::forge::Processor off;
    soloSineOnA(off);
    setValue(off, "routeA", 1.0f);
    setValue(off, "filterEnable", 0.0f);
    renderNote(off, buffer);
    const auto moduleOff = rms(buffer, 0, settled);
    setValue(off, "drive", 1.0f);
    renderNote(off, buffer);
    requireClose(rms(buffer, 0, settled), moduleOff, moduleOff * 0.001f,
                 "drive does nothing while the filter module is off");
}

// ------------------------------------------------------------- the type list ---

// The list is stored as an index inside every preset, so what each index means
// can never move. These are cheap checks on the table itself, and they are the
// ones that would catch an insertion into the middle of the enum.
void filterListSuite()
{
    require(static_cast<int>(rhino::forge::filterTypes().size()) == rhino::forge::filterTypeCount,
            "the type table is as long as the count says");

    // The three Forge shipped with keep their places forever: a preset saved
    // before the other thirty-one existed stores 0, 1 or 2 and has to come
    // back on the filter it was saved on.
    require(rhino::forge::filterTypeOf(0.0f) == rhino::forge::FilterType::lowPass
                && rhino::forge::filterTypeOf(1.0f) == rhino::forge::FilterType::highPass
                && rhino::forge::filterTypeOf(2.0f) == rhino::forge::FilterType::bandPass,
            "LOW, HIGH and BAND are still types 0, 1 and 2");

    // Every type is named, named distinctly, and short enough for the field it
    // is read in.
    for (int type = 0; type < rhino::forge::filterTypeCount; ++type)
    {
        const auto name = juce::String(rhino::forge::filterTypeName(type));
        require(name.isNotEmpty() && name.length() <= 8, "every type has a short name");
        for (int other = 0; other < type; ++other)
            require(name != rhino::forge::filterTypeName(other), "no two types share a name");

        // Every type gives the second field a job, which is what keeps the
        // module's layout fixed whatever is chosen, and opens it somewhere
        // inside the knob's range.
        const auto chosen = rhino::forge::filterTypeOf(static_cast<float>(type));
        require(rhino::forge::filterSecondLabel(chosen) != nullptr
                    && juce::String(rhino::forge::filterSecondLabel(chosen)).isNotEmpty(),
                "every type names its second field");
        const auto opens = rhino::forge::filterSecondInit(chosen);
        require(opens >= 0.0f && opens <= 1.0f, "and opens it inside the knob's range");
    }

    // The five basic types open that field at nothing, because a preset written
    // before the field existed loads it at its default — so the default has to
    // be the setting that leaves those three filters doing exactly what they
    // always did.
    for (const auto type : {rhino::forge::FilterType::lowPass, rhino::forge::FilterType::highPass,
                            rhino::forge::FilterType::bandPass, rhino::forge::FilterType::notch,
                            rhino::forge::FilterType::peak})
        requireClose(rhino::forge::filterSecondInit(type), 0.0f, 0.0f,
                     "a basic type opens its second field at nothing");

    // Which is only safe because nothing is genuinely a bypass of it: at FAT
    // zero the saturator has to be skipped rather than run at unity, to the
    // bit. Checked on the function rather than through a render, because it is
    // one expression and this is what it says.
    {
        auto low = 0.0f, band = 0.0f, plainLow = 0.0f, plainBand = 0.0f;
        const auto g = 0.0655f, damping = 0.4f;
        for (int i = 0; i < 64; ++i)
        {
            const auto input = std::sin(static_cast<float>(i) * 0.37f);
            const auto taps = rhino::forge::filterSvf(input, low, band, g, damping, 0.0f);
            // The same two trapezoidal integrators, written out: the output is
            // the state plus one step and the state advances by two.
            const auto high = (input - (2.0f * damping + g) * plainBand - plainLow)
                            / (1.0f + 2.0f * damping * g + g * g);
            const auto bandStep = g * high;
            const auto bandOut = bandStep + plainBand;
            plainBand = bandOut + bandStep;
            const auto lowStep = g * bandOut;
            const auto lowOut = lowStep + plainLow;
            plainLow = lowOut + lowStep;
            require(taps.low == lowOut && taps.band == bandOut && taps.high == high,
                    "FAT at nothing skips the saturator rather than running it at unity");
        }
    }

    // Every family holds something, and every type belongs to exactly one — so
    // a type added to the list cannot be left out of the menu.
    std::array<int, static_cast<size_t>(rhino::forge::filterCategoryCount)> held {};
    for (int type = 0; type < rhino::forge::filterTypeCount; ++type)
    {
        const auto category = rhino::forge::filterCategoryOf(
            rhino::forge::filterTypeOf(static_cast<float>(type)));
        ++held[static_cast<size_t>(category)];
    }
    for (int category = 0; category < rhino::forge::filterCategoryCount; ++category)
    {
        require(held[static_cast<size_t>(category)] > 0, "every family holds at least one type");
        require(juce::String(rhino::forge::filterCategoryName(
                    static_cast<rhino::forge::FilterCategory>(category))).isNotEmpty(),
                "every family is named");
    }
}

// ------------------------------------------------------- every type, rendered ---

// One sine on oscillator A, through the filter, with nothing else sounding.
rhino::forge::Patch filteredSine(int type, float cutoff, float resonance, float second)
{
    rhino::forge::Patch patch;
    patch.a.enable = 1.0f;
    patch.a.position = 0.0f;   // the sine frame
    patch.a.unison = 1.0f;
    patch.a.detune = 0.0f;
    patch.a.level = 0.75f;
    patch.b.enable = 0.0f;
    patch.subEnable = 0.0f;
    patch.noiseEnable = 0.0f;
    patch.filterEnable = 1.0f;
    patch.filterType = static_cast<float>(type);
    patch.cutoff = cutoff;
    patch.resonance = resonance;
    patch.filterFreq = second;
    patch.drive = 0.0f;
    patch.routeA = 1.0f;
    patch.envs[rhino::forge::ampEnv].attack = 0.001f;
    patch.envs[rhino::forge::ampEnv].sustain = 1.0f;
    return patch;
}

float renderPeak(const rhino::forge::Patch& patch, int note, double sampleRate, int samples = 8192)
{
    rhino::forge::Core core;
    core.initialise(sampleRate);
    core.noteOn(note, 1.0f, patch);
    auto peak = 0.0f;
    auto finite = true;
    for (int i = 0; i < samples; ++i)
    {
        auto left = 0.0f, right = 0.0f;
        core.renderSample(patch, left, right);
        if (!std::isfinite(left) || !std::isfinite(right)) { finite = false; break; }
        peak = juce::jmax(peak, std::abs(left), std::abs(right));
    }
    return finite ? peak : std::numeric_limits<float>::quiet_NaN();
}

// Every type has to render finite audio at every corner of every knob that
// shapes it, and has to render *something*: a family that is silent is a family
// nobody would report as broken because it looks like a closed filter.
//
// This is the check that matters most about a list this long. A ladder whose
// feedback runs away, a comb that reads past the end of its line and a
// self-oscillating scream that never settles all show up here and nowhere else.
void filterStabilitySuite()
{
    constexpr double rate = 48000.0;
    for (int type = 0; type < rhino::forge::filterTypeCount; ++type)
        for (const auto cutoff : {30.0f, 220.0f, 2000.0f, 18000.0f})
            for (const auto resonance : {0.0f, 0.55f, 1.0f})
                for (const auto second : {0.0f, 0.5f, 1.0f})
                {
                    const auto patch = filteredSine(type, cutoff, resonance, second);
                    const auto peak = renderPeak(patch, 57, rate);
                    if (!std::isfinite(peak) || peak > 12.0f)
                    {
                        require(false, "every type renders finite audio at every setting");
                        std::cerr << "       " << rhino::forge::filterTypeName(type)
                                  << " cutoff " << cutoff << " res " << resonance
                                  << " freq " << second << " peaked " << peak << '\n';
                    }
                }

    // Every type passes the note somewhere on the cutoff knob. Not at one
    // setting: a high pass at four kilohertz is *supposed* to remove a 220 Hz
    // note, so the claim worth making is that no type is silent wherever the
    // knob is put, which is what a family wired to nothing would be.
    for (int type = 0; type < rhino::forge::filterTypeCount; ++type)
    {
        const auto chosen = rhino::forge::filterTypeOf(static_cast<float>(type));
        const auto opens = rhino::forge::filterSecondInit(chosen);
        auto best = 0.0f;
        for (const auto cutoff : {200.0f, 700.0f, 4000.0f})
            best = juce::jmax(best, renderPeak(filteredSine(type, cutoff, 0.3f, opens), 57, rate));
        if (!(best > 0.02f))
        {
            require(false, "every type passes the note somewhere on the knob");
            std::cerr << "       " << rhino::forge::filterTypeName(type)
                      << " peaked " << best << " at best" << '\n';
        }
    }

    // And no two types are the same filter. Compared as rendered waveforms
    // rather than as levels, because two filters can be very different and
    // still peak at the same place — which is what would let a type wired to
    // the wrong branch of the switch go unnoticed, and the failure a table
    // this long invites.
    //
    // FREQ sits off centre on purpose. A morph type at its middle genuinely
    // *is* the basic type in the middle of its name — that is what the middle
    // of the knob means, and the display checks hold it to exactly that — so
    // comparing the two there would be asking a correct thing to be wrong.
    const auto signature = [rate] (int type)
    {
        rhino::forge::Core core;
        const auto patch = filteredSine(type, 700.0f, 0.45f, 0.3f);
        core.initialise(rate);
        core.noteOn(69, 1.0f, patch);
        std::vector<float> settled;
        settled.reserve(2048);
        for (int i = 0; i < 6144; ++i)
        {
            auto left = 0.0f, right = 0.0f;
            core.renderSample(patch, left, right);
            if (i >= 4096) settled.push_back(left);
        }
        return settled;
    };

    std::vector<std::vector<float>> rendered;
    for (int type = 0; type < rhino::forge::filterTypeCount; ++type)
    {
        rendered.push_back(signature(type));
        auto energy = 0.0;
        for (const auto sample : rendered.back()) energy += static_cast<double>(sample) * sample;
        if (!(energy > 1.0e-6))
        {
            require(false, "every type is audible at the setting the rest are compared at");
            std::cerr << "       " << rhino::forge::filterTypeName(type) << " is silent" << '\n';
        }
    }

    const auto distance = [] (const std::vector<float>& a, const std::vector<float>& b)
    {
        auto difference = 0.0, scale = 0.0;
        for (size_t i = 0; i < a.size(); ++i)
        {
            const auto d = static_cast<double>(a[i]) - b[i];
            difference += d * d;
            scale += juce::jmax(static_cast<double>(a[i]) * a[i], static_cast<double>(b[i]) * b[i]);
        }
        return scale > 0.0 ? std::sqrt(difference / scale) : 0.0;
    };

    for (int type = 0; type < rhino::forge::filterTypeCount; ++type)
        for (int other = 0; other < type; ++other)
        {
            const auto apart = distance(rendered[static_cast<size_t>(type)],
                                        rendered[static_cast<size_t>(other)]);
            if (apart < 1.0e-4)
            {
                require(false, "no two types render the same filter");
                std::cerr << "       " << rhino::forge::filterTypeName(type) << " and "
                          << rhino::forge::filterTypeName(other) << " are the same waveform" << '\n';
            }
        }
}

// --------------------------------------------------- the curve and the audio ---

// The display draws `filterMagnitude`. This is what makes that worth drawing:
// the gain it claims, measured off the rendered signal by fitting the peak of
// its spectrum, with and without the filter. Nothing here reads the filter's
// state, so it cannot agree with the implementation by construction — the same
// reason the tuning checks measure a rendered fundamental rather than the phase
// accumulator.
void filterResponseSuite()
{
    constexpr double rate = 48000.0;
    constexpr float cutoff = 700.0f;
    // Away from the corner in both directions, and never on it: a notch at its
    // own corner is a null, and measuring a null says nothing about the skirt
    // either side of it.
    const std::array<int, 6> notes {36, 48, 60, 72, 96, 100};

    const auto fundamentalOf = [rate] (const rhino::forge::Patch& patch, int note)
    {
        const auto spectrum = renderedSpectrum(patch, note, rate);
        const auto hz = 440.0 * std::pow(2.0, (static_cast<double>(note) - 69.0) / 12.0);
        return partialAmplitude(spectrum, hz, 1, rate);
    };

    struct Case { rhino::forge::FilterType type; float resonance, second; };
    // The families whose response is their own transfer function and whose
    // gain at one frequency is not knife-edge. Resonance stays low: a peak
    // eight times unity moves several decibels for a per-cent error in the
    // corner, which would be measuring the discretisation rather than the
    // response.
    const std::array<Case, 8> cases {{
        {rhino::forge::FilterType::lowPass, 0.0f, 0.0f},
        {rhino::forge::FilterType::highPass, 0.0f, 0.0f},
        {rhino::forge::FilterType::bandPass, 0.1f, 0.0f},
        {rhino::forge::FilterType::notch, 0.0f, 0.0f},
        {rhino::forge::FilterType::peak, 0.15f, 0.0f},
        {rhino::forge::FilterType::lowNotch, 0.1f, 0.625f},
        {rhino::forge::FilterType::morphLowBandHigh, 0.1f, 0.5f},
        {rhino::forge::FilterType::ladder, 0.0f, 0.0f},
    }};

    for (const auto& held : cases)
    {
        const auto type = static_cast<int>(held.type);
        auto open = filteredSine(type, cutoff, held.resonance, held.second);
        open.filterEnable = 0.0f;

        const auto filtered = filteredSine(type, cutoff, held.resonance, held.second);
        const rhino::forge::FilterShape shape {held.type, cutoff, held.resonance,
                                               held.second, rate};

        for (const auto note : notes)
        {
            const auto hz = static_cast<float>(440.0
                * std::pow(2.0, (static_cast<double>(note) - 69.0) / 12.0));
            const auto dry = fundamentalOf(open, note);
            const auto wet = fundamentalOf(filtered, note);
            if (dry <= 1.0e-9)
            {
                require(false, "the note is measurable before the filter is switched on");
                continue;
            }

            const auto measured = 20.0f * std::log10(juce::jmax(1.0e-9f,
                                                                static_cast<float>(wet / dry)));
            const auto drawn = 20.0f * std::log10(juce::jmax(1.0e-9f,
                                                             rhino::forge::filterMagnitude(shape, hz)));
            // Three decibels. The engine's discretisation is not the analogue
            // prototype the curve is drawn from, and the two diverge as the
            // corner climbs towards Nyquist; what is being held here is that
            // the curve is the right filter, not that it is the same to a
            // rounding error.
            if (std::abs(measured - drawn) > 3.0f)
            {
                require(false, "the curve the panel draws is the gain the voice renders");
                std::cerr << "       " << rhino::forge::filterTypeName(type) << " at " << hz
                          << " Hz: drew " << drawn << " dB, rendered " << measured << " dB\n";
            }
        }
    }
}

// ------------------------------------------------------------ the second field ---

// FREQ is one parameter with nine meanings, so what has to be checked is that
// each of them reaches the sound and reaches it the way the label says.
void filterSecondSuite()
{
    constexpr double rate = 48000.0;
    constexpr int samples = 8192;

    const auto render = [rate] (int type, float cutoff, float resonance, float second, int note)
    {
        rhino::forge::Core core;
        const auto patch = filteredSine(type, cutoff, resonance, second);
        core.initialise(rate);
        core.noteOn(note, 1.0f, patch);
        auto sum = 0.0;
        for (int i = 0; i < samples; ++i)
        {
            auto left = 0.0f, right = 0.0f;
            core.renderSample(patch, left, right);
            if (i >= 2048) sum += static_cast<double>(left) * left;
        }
        return static_cast<float>(std::sqrt(sum / static_cast<double>(samples - 2048)));
    };

    // Every type's second field reaches the sound. A knob that is labelled and
    // does nothing is worse than a knob that is hidden.
    for (int type = 0; type < rhino::forge::filterTypeCount; ++type)
    {
        const auto quiet = render(type, 700.0f, 0.5f, 0.15f, 69);
        const auto loud = render(type, 700.0f, 0.5f, 0.85f, 69);
        if (std::abs(loud - quiet) <= juce::jmax(loud, quiet) * 1.0e-4f + 1.0e-7f)
        {
            require(false, "every type's second field changes what comes out");
            std::cerr << "       " << rhino::forge::filterTypeName(type)
                      << " read " << quiet << " and " << loud << '\n';
        }
    }

    // On a dual filter it is the second corner, in octaves off the first. A
    // high pass swept up from four octaves below the cutoff to the cutoff
    // itself has to take more and more of a note sitting between them.
    {
        const auto note = 69;   // 440 Hz, inside a 700 Hz low pass
        const auto wide = render(static_cast<int>(rhino::forge::FilterType::lowHigh),
                                 700.0f, 0.0f, 0.0f, note);
        const auto narrow = render(static_cast<int>(rhino::forge::FilterType::lowHigh),
                                   700.0f, 0.0f, 0.5f, note);
        require(narrow < wide * 0.6f,
                "bringing a dual filter's high pass up to the cutoff closes the band on the note");
    }

    // On a morph type it is the response itself, and the ends are the two
    // filters the name promises: a note above the corner survives the high
    // pass and not the low pass.
    {
        const auto note = 96;   // 2093 Hz, well above a 700 Hz corner
        const auto asLow = render(static_cast<int>(rhino::forge::FilterType::morphLowBandHigh),
                                  700.0f, 0.0f, 0.0f, note);
        const auto asHigh = render(static_cast<int>(rhino::forge::FilterType::morphLowBandHigh),
                                   700.0f, 0.0f, 1.0f, note);
        require(asHigh > asLow * 4.0f, "a morph swept end to end is a low pass then a high pass");
    }

    // On a comb it is the damping in the feedback path, so opening it leaves
    // more of the ring: measured on a note off the comb's own tuning, where the
    // ring is what is left rather than the note.
    {
        const auto dark = render(static_cast<int>(rhino::forge::FilterType::comb),
                                 220.0f, 0.9f, 0.0f, 69);
        const auto bright = render(static_cast<int>(rhino::forge::FilterType::comb),
                                   220.0f, 0.9f, 1.0f, 69);
        require(bright > dark, "opening a comb's damping leaves more of its ring");
    }

    // And the field's own readout says what it is set to in the type's unit,
    // which is the other half of the label meaning anything.
    rhino::forge::Processor processor;
    setValue(processor, "cutoff", 1000.0f);
    setValue(processor, "filterType",
             static_cast<float>(rhino::forge::FilterType::lowNotch));
    require(textFor(processor, "filterFreq", 0.625f).contains("oct")
                && textFor(processor, "filterFreq", 0.625f).contains("2.00 kHz"),
            "a dual filter's FREQ reads as an offset and as the corner it lands on");
    setValue(processor, "filterType",
             static_cast<float>(rhino::forge::FilterType::morphLowBandHigh));
    requireText(textFor(processor, "filterFreq", 0.5f), "BP",
                "a morph's middle reads as the response it is on");
    requireText(textFor(processor, "filterFreq", 0.0f), "LP",
                "and its ends as the two it is between");
    setValue(processor, "filterType",
             static_cast<float>(rhino::forge::FilterType::diffusor));
    requireText(textFor(processor, "filterFreq", 1.0f),
                juce::String(rhino::forge::filterDiffuseStages),
                "a stage count reads as a count");

    // The cutoff reads as a vowel on a formant filter, because that is what it
    // is doing there — there is no corner to name.
    setValue(processor, "filterType", static_cast<float>(rhino::forge::FilterType::formant));
    require(textFor(processor, "cutoff", 30.0f).contains("VOWEL"),
            "a formant filter's cutoff reads as a vowel");
    setValue(processor, "filterType", 0.0f);
    requireText(textFor(processor, "cutoff", 1000.0f), "1.00 kHz",
                "and as a frequency on every other type");
}

// ------------------------------------------------------------- the tuning of it ---

// A comb is tuned, so it can be measured the way a note is: the peak of its
// ring against the frequency its tuning predicts. It is the one resonator whose
// pitch is a claim rather than a colour.
void filterCombTuningSuite()
{
    constexpr double rate = 48000.0;
    // Noise rather than a note, so what is measured is the comb's own
    // resonance and not the source's harmonics falling near it.
    //
    // Measured tooth by tooth rather than by looking for the tallest peak in
    // the spectrum: a comb with its damping open has a hundred teeth of nearly
    // equal height, so which one is tallest is down to the noise and says
    // nothing. Which frequencies are teeth and which are the gaps between them
    // is the actual claim.
    const auto spectrumOf = [rate] (float cutoff)
    {
        auto patch = filteredSine(static_cast<int>(rhino::forge::FilterType::comb),
                                  cutoff, 0.95f, 1.0f);
        patch.a.enable = 0.0f;
        patch.noiseEnable = 1.0f;
        patch.noiseLevel = 0.5f;
        patch.routeNoise = 1.0f;
        return renderedSpectrum(patch, 57, rate);
    };

    for (const auto cutoff : {110.0f, 220.0f, 440.0f})
    {
        const rhino::forge::FilterShape shape {rhino::forge::FilterType::comb, cutoff, 0.95f,
                                               1.0f, rate};
        const auto tuning = static_cast<double>(rhino::forge::filterCombHz(shape));
        requireClose(static_cast<float>(tuning), cutoff, 0.01f,
                     "a comb is tuned to the frequency the cutoff is holding");

        const auto spectrum = spectrumOf(cutoff);
        const auto at = [&spectrum, rate] (double hz)
        {
            return partialAmplitude(spectrum, hz, 1, rate);
        };
        // The first three teeth stand well clear of the gaps either side of
        // them, which is what says the comb is tuned to the cutoff rather than
        // to some multiple of it.
        for (int harmonic = 1; harmonic <= 3; ++harmonic)
        {
            const auto tooth = at(tuning * harmonic);
            const auto gap = at(tuning * (harmonic + 0.5));
            if (!(tooth > gap * 3.0))
            {
                require(false, "a comb's teeth stand clear of the gaps between them");
                std::cerr << "       " << cutoff << " Hz comb, harmonic " << harmonic
                          << ": tooth " << tooth << " gap " << gap << '\n';
            }
        }
    }

    // Including at the bottom of the knob, which is the case a fixed-length
    // delay line would have quietly detuned: the line is sized from the same
    // floor the cutoff is clamped to, so there is no tuning the knob can ask
    // for that it has no room for.
    for (const auto rate : {44100.0, 48000.0, 96000.0})
    {
        const rhino::forge::FilterShape low {rhino::forge::FilterType::comb, 30.0f, 0.9f,
                                             1.0f, rate};
        requireClose(rhino::forge::filterCombHz(low), 30.0f, 0.01f,
                     "a comb is tuned to the cutoff at the bottom of the knob, at any rate");
    }
}
}

// ------------------------------------------------------ the one that is not LTI ---

// The ring modulator is the type whose wiring has a crisp spectral answer, so
// it gets measured rather than described: multiplying a note by a sine puts the
// sum and the difference where the note was and takes the note itself out.
//
// It is also the check that would catch it reading the wrong oscillator or the
// wrong frequency, neither of which the finite-and-distinct checks above can
// see.
void filterRingModSuite()
{
    constexpr double rate = 48000.0;
    constexpr float modulator = 300.0f;
    constexpr int note = 69;                      // 440 Hz
    const auto carrier = 440.0;

    auto patch = filteredSine(static_cast<int>(rhino::forge::FilterType::ringMod),
                              modulator, 0.0f, 0.0f);
    const auto spectrum = renderedSpectrum(patch, note, rate);
    const auto at = [&spectrum] (double hz)
    {
        return partialAmplitude(spectrum, hz, 1, rate);
    };

    const auto difference = at(carrier - modulator);   // 140 Hz
    const auto sum = at(carrier + modulator);          // 740 Hz
    const auto original = at(carrier);                 // 440 Hz, which should be gone

    require(difference > 0.0 && sum > 0.0, "a ring modulator puts the sum and the difference out");
    // The two sidebands are the same size, because multiplying by a sine gives
    // each of them half the carrier.
    requireClose(static_cast<float>(sum), static_cast<float>(difference),
                 static_cast<float>(difference) * 0.1f,
                 "the two sidebands come out at the same level");
    // And the carrier is not among them. Twenty decibels down is a sideband's
    // skirt rather than a note.
    if (!(original < difference * 0.1))
    {
        require(false, "a ring modulator takes the note itself out");
        std::cerr << "       440 Hz read " << original << " against a sideband of "
                  << difference << '\n';
    }

    // SPREAD opens a second modulator above the first, so a second pair of
    // sidebands appears where there was none.
    patch.filterFreq = 1.0f;                            // an octave up: 600 Hz
    const auto spread = renderedSpectrum(patch, note, rate);
    const auto spreadAt = [&spread] (double hz)
    {
        return partialAmplitude(spread, hz, 1, rate);
    };
    // Against the same bin with SPREAD closed, where nothing is modulating at
    // 600 Hz and so nothing should be sitting at 1040.
    require(spreadAt(carrier + 600.0) > at(carrier + 600.0) * 4.0,
            "SPREAD opens a second modulator, and it lands an octave above the first");
    // The first modulator is still there: SPREAD adds one rather than moving it.
    require(spreadAt(carrier + modulator) > 0.0,
            "and the first modulator is still where it was");
}

void filterTests()
{
    filterRoutingSuite();
    filterListSuite();
    filterStabilitySuite();
    filterResponseSuite();
    filterSecondSuite();
    filterCombTuningSuite();
    filterRingModSuite();
}
}
