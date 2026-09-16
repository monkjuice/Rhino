#include "../src/ForgeProcessor.h"
#include "../ui/ForgeLayout.h"
#include <iostream>

// One binary, three CTest cases selected by argv, matching Theta's own test
// convention. Run with no argument to execute all three.
namespace
{
int failures = 0;

void require(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

void requireClose(float actual, float expected, float tolerance, const char* message)
{
    if (std::abs(actual - expected) <= tolerance) return;
    std::cerr << "FAIL: " << message << " (expected " << expected << ", got " << actual << ")\n";
    ++failures;
}

void requireText(const juce::String& actual, const juce::String& expected, const char* message)
{
    if (actual == expected) return;
    std::cerr << "FAIL: " << message << " (expected \"" << expected << "\", got \"" << actual << "\")\n";
    ++failures;
}

void setValue(theta::forge::Processor& processor, const char* id, float plainValue)
{
    auto* parameter = processor.state.getParameter(id);
    if (parameter == nullptr) { require(false, id); return; }
    parameter->setValueNotifyingHost(parameter->convertTo0to1(plainValue));
}

float value(const theta::forge::Processor& processor, const char* id)
{
    const auto* raw = processor.state.getRawParameterValue(id);
    if (raw == nullptr) { require(false, id); return 0.0f; }
    return raw->load();
}

juce::String textFor(const theta::forge::Processor& processor, const char* id, float plainValue)
{
    auto* parameter = processor.state.getParameter(id);
    if (parameter == nullptr) { require(false, id); return {}; }
    return parameter->getText(parameter->convertTo0to1(plainValue), 32);
}

// Render a held note and report the loudest sample. Used to prove a source is
// silent rather than merely quiet.
float peakForNote(theta::forge::Processor& processor, int samples = 4096)
{
    processor.prepareToPlay(48000.0, samples);
    juce::AudioBuffer<float> buffer(2, samples);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 57, 1.0f), 0);
    processor.processBlock(buffer, midi);
    return buffer.getMagnitude(0, samples);
}

// Render a held note into a caller-owned buffer, for checks that need to look
// at the waveform rather than only its peak.
void renderNote(theta::forge::Processor& processor, juce::AudioBuffer<float>& buffer, int note = 57)
{
    processor.prepareToPlay(48000.0, buffer.getNumSamples());
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), 0);
    processor.processBlock(buffer, midi);
}

// Counted on the settled part of the render, past the envelope attack.
int zeroCrossings(const juce::AudioBuffer<float>& buffer, int channel, int from)
{
    auto crossings = 0;
    for (int i = from + 1; i < buffer.getNumSamples(); ++i)
        if ((buffer.getSample(channel, i - 1) < 0.0f) != (buffer.getSample(channel, i) < 0.0f))
            ++crossings;
    return crossings;
}

float rms(const juce::AudioBuffer<float>& buffer, int channel, int from)
{
    auto sum = 0.0;
    const auto count = buffer.getNumSamples() - from;
    for (int i = from; i < buffer.getNumSamples(); ++i)
        sum += static_cast<double>(buffer.getSample(channel, i)) * buffer.getSample(channel, i);
    return count > 0 ? static_cast<float>(std::sqrt(sum / count)) : 0.0f;
}

// High-frequency content relative to overall level. Amplitude-independent, so
// it measures how open a filter is without being fooled by a quieter note.
float brightness(const juce::AudioBuffer<float>& buffer, int channel, int from)
{
    auto edges = 0.0, total = 0.0;
    for (int i = from + 1; i < buffer.getNumSamples(); ++i)
    {
        const auto sample = static_cast<double>(buffer.getSample(channel, i));
        const auto step = sample - buffer.getSample(channel, i - 1);
        edges += step * step;
        total += sample * sample;
    }
    return total > 0.0 ? static_cast<float>(std::sqrt(edges / total)) : 0.0f;
}

bool allSamplesFinite(const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (!std::isfinite(buffer.getSample(channel, i))) return false;
    return true;
}

// ---------------------------------------------------------------- layout ---

void layoutSuite()
{
    theta::forge::Processor processor;
    const auto bounds = juce::Rectangle<int>(0, 0, 1260, 1060);
    const auto content = theta::forge::ui::contentBounds(bounds);
    const auto& modules = theta::forge::ui::modules();
    require(!modules.empty(), "the panel declares at least one module");

    // Every id the layout names must resolve. This is the guard that keeps a
    // declarative layout honest: a typo here would otherwise be a silent
    // missing knob rather than a build or test failure.
    for (const auto& module : modules)
    {
        if (module.enableId != nullptr)
            require(processor.state.getParameter(module.enableId) != nullptr,
                    "a module's enable id names a real parameter");
        for (const auto& row : module.rows)
            for (const auto& control : row.controls)
                require(processor.state.getParameter(control.id) != nullptr,
                        "a module's control id names a real parameter");
        if (module.display == theta::forge::ui::Display::oscillator)
        {
            const auto* source = theta::forge::ui::displaySourceId(module);
            require(source != nullptr && processor.state.getParameter(source) != nullptr,
                    "an oscillator display reads a real parameter");
        }
    }

    // A control that greys out under another must name a parameter that exists,
    // or the dependency silently never fires.
    for (const auto& module : modules)
        for (const auto& row : module.rows)
            for (const auto& control : row.controls)
            {
                if (control.disabledBy != nullptr)
                    require(processor.state.getParameter(control.disabledBy) != nullptr,
                            "a control's disabling parameter exists");
                if (control.enabledBy != nullptr)
                    require(processor.state.getParameter(control.enabledBy) != nullptr,
                            "a control's enabling parameter exists");
                // Both at once would be a control that is never live under one
                // setting and never live under the other.
                require(control.disabledBy == nullptr || control.enabledBy == nullptr,
                        "a control is gated one way or the other, not both");
            }

    // Polyphony means nothing in mono, and the panel has to say so.
    auto polyIsGated = false;
    for (const auto& module : modules)
        for (const auto& row : module.rows)
            for (const auto& control : row.controls)
                if (juce::String(control.id) == "polyphony")
                    polyIsGated = control.disabledBy != nullptr
                        && juce::String(control.disabledBy) == "mono";
    require(polyIsGated, "the polyphony knob greys out while mono is on");

    // Conversely, every parameter should be reachable from the panel. A
    // parameter nothing displays is either a bug or dead weight.
    for (const auto* raw : processor.getParameters())
    {
        const auto* withId = dynamic_cast<const juce::AudioProcessorParameterWithID*>(raw);
        if (withId == nullptr) continue;
        auto found = false;
        for (const auto& module : modules)
        {
            if (module.enableId != nullptr && withId->paramID == module.enableId) found = true;
            for (const auto& row : module.rows)
                for (const auto& control : row.controls)
                    if (withId->paramID == control.id) found = true;
        }
        require(found, "every parameter appears somewhere on the panel");
        if (!found) std::cerr << "       orphan parameter: " << withId->paramID << '\n';
    }

    for (size_t i = 0; i < modules.size(); ++i)
    {
        const auto area = theta::forge::ui::moduleBounds(bounds, modules[i]);
        require(!area.isEmpty(), "a module occupies a non-empty rectangle");
        require(content.contains(area), "a module stays inside the content area");

        const auto controls = theta::forge::ui::controlArea(area, modules[i]);
        require(controls.getHeight() > 30, "a module leaves usable height for its controls");
        require(area.withTrimmedTop(theta::forge::ui::headerHeight).contains(controls),
                "controls stay clear of the module header, so labels cannot collide with the title");

        // Rows within a module must tile their area without overlapping either.
        for (int r = 0; r < static_cast<int>(modules[i].rows.size()); ++r)
        {
            const auto row = theta::forge::ui::rowBounds(area, modules[i], r);
            require(controls.contains(row), "a control row stays inside its module");
            for (int s = r + 1; s < static_cast<int>(modules[i].rows.size()); ++s)
                require(!row.intersects(theta::forge::ui::rowBounds(area, modules[i], s)),
                        "no two control rows in a module overlap");
        }

        // Two modules may share a rectangle as long as no tab shows them both:
        // that is exactly what the oscillators and the matrix do.
        for (size_t j = i + 1; j < modules.size(); ++j)
            if (theta::forge::ui::sharePage(modules[i], modules[j]))
                require(!area.intersects(theta::forge::ui::moduleBounds(bounds, modules[j])),
                        "no two modules shown together overlap");
    }

    // Every tab has to put something on screen, and the tabs themselves have to
    // stay clear of each other in the title bar.
    for (const auto page : theta::forge::ui::tabPages)
    {
        auto shown = 0;
        for (const auto& module : modules)
            if (module.page == page) ++shown;
        require(shown > 0, "every tab shows at least one module of its own");
    }
    for (int i = 1; i < theta::forge::ui::tabCount; ++i)
        require(!theta::forge::ui::tabBounds(i - 1).intersects(theta::forge::ui::tabBounds(i)),
                "no two tabs overlap");

    // A table names its columns once, above its rows, so every row has to have
    // the same controls in the same order as the first or the titles lie.
    for (const auto& module : modules)
    {
        if (module.columnHeaderHeight <= 0) continue;
        const auto area = theta::forge::ui::moduleBounds(bounds, module);
        const auto titles = theta::forge::ui::columnTitleBounds(area, module);
        require(!titles.isEmpty(), "a table reserves a strip for its column titles");
        require(!titles.intersects(theta::forge::ui::controlArea(area, module)),
                "a table's column titles sit clear of its rows");
        for (int r = 0; r < static_cast<int>(module.rows.size()); ++r)
            require(!theta::forge::ui::rowGutterBounds(area, module, r)
                         .intersects(theta::forge::ui::rowBounds(area, module, r)),
                    "a table's row numbers sit clear of its controls");
        const auto& first = module.rows.front().controls;
        for (const auto& row : module.rows)
        {
            require(row.controls.size() == first.size(), "every table row has the same columns");
            for (size_t c = 0; c < row.controls.size() && c < first.size(); ++c)
                require(row.controls[c].style == first[c].style && row.controls[c].weight == first[c].weight,
                        "a table column keeps its style and its width down every row");
        }
    }

    // Every knob on the panel is the same size, whichever module it sits in.
    const auto diameter = theta::forge::ui::uniformKnobDiameter(bounds);
    require(diameter >= 48, "the shared knob diameter stays usable");
    for (const auto& module : modules)
    {
        const auto area = theta::forge::ui::moduleBounds(bounds, module);
        for (int r = 0; r < static_cast<int>(module.rows.size()); ++r)
        {
            const auto& row = module.rows[static_cast<size_t>(r)];
            for (int i = 0; i < static_cast<int>(row.controls.size()); ++i)
            {
                const auto block = theta::forge::ui::controlBlock(area, module, r, i, diameter);
                require(!block.isEmpty(), "every control gets a non-empty rectangle");
                require(area.contains(block), "every control stays inside its module");
                if (row.controls[static_cast<size_t>(i)].style != theta::forge::ui::Style::knob)
                    continue;
                if (module.compactKnobs)
                {
                    // A compact module opts out of the shared size on purpose,
                    // but its knobs still have to be smaller, not larger, and
                    // still have to be usable.
                    require(block.getWidth() <= diameter, "a compact knob is no larger than the shared diameter");
                    require(block.getWidth() >= 24, "a compact knob stays usable");
                }
                else
                {
                    require(block.getWidth() == diameter, "every ordinary knob is drawn at the shared diameter");
                }
            }
        }
    }

    // The proportions have to survive the whole resize range, not just the
    // default size.
    for (const auto size : {juce::Point<int>(1140, 980), juce::Point<int>(1900, 1500)})
    {
        const auto resized = juce::Rectangle<int>(0, 0, size.x, size.y);
        for (const auto& module : modules)
        {
            const auto area = theta::forge::ui::moduleBounds(resized, module);
            require(theta::forge::ui::contentBounds(resized).contains(area),
                    "a module stays inside the content area at every allowed size");
            require(theta::forge::ui::controlArea(area, module).getHeight() > 24,
                    "controls stay usable at every allowed size");
        }
        require(theta::forge::ui::uniformKnobDiameter(resized) >= 40,
                "knobs stay usable at every allowed size");
    }
}

// --------------------------------------------------------------- presets ---

void presetSuite()
{
    theta::forge::Processor processor;
    const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("theta-forge-preset-test", {}, true);
    require(directory.createDirectory(), "temporary preset directory can be created");
    const auto preset = directory.getChildFile("Round Trip.forgepreset");

    setValue(processor, "cutoff", 1320.0f);
    setValue(processor, "release", 2.5f);
    setValue(processor, "noiseEnable", 1.0f);
    setValue(processor, "oscBEnable", 0.0f);
    require(processor.savePreset(preset, "Round Trip").wasOk(), "preset saves");

    setValue(processor, "cutoff", 9000.0f);
    setValue(processor, "release", 0.1f);
    setValue(processor, "noiseEnable", 0.0f);
    setValue(processor, "oscBEnable", 1.0f);
    require(processor.loadPreset(preset).wasOk(), "preset loads");
    requireClose(value(processor, "cutoff"), 1320.0f, 1.0f, "preset restores cutoff");
    requireClose(value(processor, "release"), 2.5f, 0.001f, "preset restores release");
    requireClose(value(processor, "noiseEnable"), 1.0f, 0.001f, "preset restores an enabled module");
    requireClose(value(processor, "oscBEnable"), 0.0f, 0.001f, "preset restores a disabled module");

    // Format 1 described a synth that no longer exists and is deliberately not
    // accepted.
    const auto version1 = directory.getChildFile("Old.forgepreset");
    require(version1.replaceWithText("<ThetaForgePreset formatVersion=\"1\"><ThetaForgeState/></ThetaForgePreset>"),
            "format 1 fixture writes");
    require(processor.loadPreset(version1).failed(), "a format 1 preset is refused");

    const auto future = directory.getChildFile("Future.forgepreset");
    require(future.replaceWithText("<ThetaForgePreset formatVersion=\"99\"><ThetaForgeState/></ThetaForgePreset>"),
            "future-version fixture writes");
    require(processor.loadPreset(future).failed(), "a newer preset version is refused");

    // Within format 2, a preset is reconciled against the parameters that exist
    // when it opens. This is what keeps presets working while Forge's controls
    // are still being built out milestone by milestone: entries Forge no longer
    // has are dropped, and controls the preset predates return to their
    // defaults instead of inheriting the previous patch.
    setValue(processor, "cutoff", 2000.0f);
    setValue(processor, "noiseEnable", 1.0f);
    const auto partial = directory.getChildFile("Partial.forgepreset");
    require(partial.replaceWithText(
                "<ThetaForgePreset formatVersion=\"2\"><ThetaForgeState>"
                "<PARAM id=\"cutoff\" value=\"5000.0\"/>"
                "<PARAM id=\"retiredKnob\" value=\"0.5\"/>"
                "</ThetaForgeState></ThetaForgePreset>"),
            "partial fixture writes");
    require(processor.loadPreset(partial).wasOk(), "a preset missing parameters still loads");
    requireClose(value(processor, "cutoff"), 5000.0f, 1.0f, "a partial preset restores what it names");
    requireClose(value(processor, "noiseEnable"), 0.0f, 0.001f,
                 "a parameter the preset omits returns to its default, not the previous patch's value");
    requireClose(value(processor, "release"), 0.35f, 0.001f,
                 "an omitted float parameter returns to its default");

    const auto resaved = directory.getChildFile("Resaved.forgepreset");
    require(processor.savePreset(resaved, "Resaved").wasOk(), "a reconciled preset saves");
    const auto text = resaved.loadFileAsString();
    require(text.contains("formatVersion=\"2\""), "a saved preset declares format 2");
    require(!text.contains("retiredKnob"), "an unknown entry is dropped, not carried as ballast");
    require(text.contains("oscAEnable"), "the module enables are saved");
    require(text.contains("release"), "an omitted parameter is written back out at its default");

    const auto invalid = directory.getChildFile("Invalid.forgepreset");
    require(invalid.replaceWithText("<NotForge />"), "invalid fixture writes");
    const auto before = value(processor, "cutoff");
    require(processor.loadPreset(invalid).failed(), "a foreign preset is rejected");
    require(value(processor, "cutoff") == before, "a rejected preset leaves state untouched");

    // The readout bug: a SliderAttachment overwrites any formatter the editor
    // installs, so the formatting has to belong to the parameter itself.
    requireText(textFor(processor, "cutoff", 7800.0f), "7.80 kHz", "cutoff reads as kHz");
    requireText(textFor(processor, "cutoff", 440.0f), "440 Hz", "a low cutoff reads as Hz");
    requireText(textFor(processor, "sustain", 0.75f), "75 %", "sustain reads as a percentage");
    requireText(textFor(processor, "attack", 0.01f), "10 ms", "a short attack reads in milliseconds");
    requireText(textFor(processor, "release", 2.5f), "2.50 s", "a long release reads in seconds");
    requireText(textFor(processor, "oscBSemitone", 7.0f), "+7 st", "tune reads as signed semitones");
    requireText(textFor(processor, "oscAUnison", 4.0f), "4", "unison reads as a plain count");
    requireText(textFor(processor, "mod1Depth", -0.5f), "-50 %", "a bipolar depth keeps its sign");
    require(!textFor(processor, "cutoff", 7800.0f).contains("7800.0004"),
            "no knob falls back to a raw float readout");

    directory.deleteRecursively();
}

// ---------------------------------------------------------------- engine ---

// Reduce a processor to one clean sine from oscillator A, so the waveform can
// be measured directly.
void soloSineOnA(theta::forge::Processor& processor)
{
    for (const auto* id : {"oscBEnable", "subEnable", "noiseEnable", "filterEnable"})
        setValue(processor, id, 0.0f);
    setValue(processor, "oscAEnable", 1.0f);
    setValue(processor, "oscAPosition", 0.0f);
    setValue(processor, "oscAUnison", 1.0f);
    setValue(processor, "oscADetune", 0.0f);
    setValue(processor, "oscAPan", 0.0f);
    setValue(processor, "oscALevel", 1.0f);
    setValue(processor, "oscAOctave", 0.0f);
    setValue(processor, "oscASemitone", 0.0f);
    setValue(processor, "oscAFine", 0.0f);
    setValue(processor, "drive", 0.0f);
    setValue(processor, "attack", 0.001f);
}

void oscillatorSuite()
{
    // Tuning is one multiplier built from three controls. Check the arithmetic
    // directly before checking that the voice honours it.
    theta::forge::Oscillator osc;
    requireClose(theta::forge::tuningRatio(osc), 1.0f, 0.0001f, "an untuned oscillator plays at pitch");
    osc.octave = 1.0f;
    requireClose(theta::forge::tuningRatio(osc), 2.0f, 0.0001f, "one octave doubles the frequency");
    osc.octave = 0.0f; osc.semitone = 12.0f;
    requireClose(theta::forge::tuningRatio(osc), 2.0f, 0.0001f, "twelve semitones doubles the frequency");
    osc.semitone = 0.0f; osc.fine = 100.0f;
    requireClose(theta::forge::tuningRatio(osc), std::pow(2.0f, 1.0f / 12.0f), 0.0001f,
                 "one hundred cents is one semitone");
    osc.octave = -1.0f; osc.semitone = 12.0f; osc.fine = 0.0f;
    requireClose(theta::forge::tuningRatio(osc), 1.0f, 0.0001f, "octave and semitone cancel");

    // And the voice honours it: an octave up doubles the zero-crossing rate.
    constexpr int samples = 8192;
    constexpr int settled = 1024;
    juce::AudioBuffer<float> buffer(2, samples);

    theta::forge::Processor atPitch;
    soloSineOnA(atPitch);
    renderNote(atPitch, buffer);
    const auto baseCrossings = zeroCrossings(buffer, 0, settled);
    require(baseCrossings > 0, "a solo sine crosses zero");

    theta::forge::Processor anOctaveUp;
    soloSineOnA(anOctaveUp);
    setValue(anOctaveUp, "oscAOctave", 1.0f);
    renderNote(anOctaveUp, buffer);
    requireClose(static_cast<float>(zeroCrossings(buffer, 0, settled)),
                 static_cast<float>(baseCrossings * 2), static_cast<float>(baseCrossings) * 0.05f,
                 "an octave up doubles the oscillator's frequency");

    theta::forge::Processor sevenSemis;
    soloSineOnA(sevenSemis);
    setValue(sevenSemis, "oscASemitone", 7.0f);
    renderNote(sevenSemis, buffer);
    requireClose(static_cast<float>(zeroCrossings(buffer, 0, settled)),
                 baseCrossings * std::pow(2.0f, 7.0f / 12.0f), static_cast<float>(baseCrossings) * 0.05f,
                 "seven semitones is a fifth");

    // Pan law: hard left puts nothing in the right channel.
    theta::forge::Processor panned;
    soloSineOnA(panned);
    setValue(panned, "oscAPan", -1.0f);
    renderNote(panned, buffer);
    const auto left = rms(buffer, 0, settled);
    const auto right = rms(buffer, 1, settled);
    require(left > 0.0f, "a hard-left oscillator still feeds the left channel");
    require(right < left * 0.01f, "a hard-left oscillator is absent from the right channel");

    // Centred, an equal-power pan puts the same energy in both channels.
    theta::forge::Processor centred;
    soloSineOnA(centred);
    renderNote(centred, buffer);
    requireClose(rms(buffer, 0, settled), rms(buffer, 1, settled), 0.0001f,
                 "a centred oscillator is equal in both channels");

    // Level scales the oscillator directly, now that it owns one.
    const auto fullLevel = rms(buffer, 0, settled);
    theta::forge::Processor halfLevel;
    soloSineOnA(halfLevel);
    setValue(halfLevel, "oscALevel", 0.5f);
    renderNote(halfLevel, buffer);
    requireClose(rms(buffer, 0, settled), fullLevel * 0.5f, fullLevel * 0.02f,
                 "halving an oscillator's level halves its output");

    // Widening the stack changes the sound without changing the level. Power
    // normalisation cannot be exact against a detuned stack, so this allows a
    // few dB rather than asserting equality.
    theta::forge::Processor single;
    soloSineOnA(single);
    setValue(single, "oscADetune", 0.3f);
    setValue(single, "oscAUnison", 1.0f);
    renderNote(single, buffer);
    const auto oneVoice = rms(buffer, 0, settled);

    theta::forge::Processor stacked;
    soloSineOnA(stacked);
    setValue(stacked, "oscADetune", 0.3f);
    setValue(stacked, "oscAUnison", 8.0f);
    renderNote(stacked, buffer);
    const auto eightVoices = rms(buffer, 0, settled);
    require(oneVoice > 0.0f && eightVoices > 0.0f, "both stack sizes make sound");
    const auto decibels = juce::Decibels::gainToDecibels(eightVoices / oneVoice);
    require(std::abs(decibels) < 4.0f, "stacking voices does not change the oscillator's level");
    if (std::abs(decibels) >= 4.0f)
        std::cerr << "       unison level shift: " << decibels << " dB\n";
}

void filterRoutingSuite()
{
    // A source that is routed into the filter is affected by the cutoff; one
    // that is not, is not. That is the whole point of the routing chips.
    constexpr int samples = 8192;
    constexpr int settled = 1024;
    juce::AudioBuffer<float> buffer(2, samples);

    const auto peakWithCutoff = [&buffer] (bool routed, float cutoff)
    {
        theta::forge::Processor processor;
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
    theta::forge::Processor split;
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
    theta::forge::Processor driven;
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

    // Each filter type keeps the output finite and does something different.
    theta::forge::Processor typed;
    soloSineOnA(typed);
    setValue(typed, "filterEnable", 1.0f);
    setValue(typed, "routeA", 1.0f);
    setValue(typed, "cutoff", 1000.0f);
    std::vector<float> levels;
    for (auto type = 0.0f; type <= 2.0f; type += 1.0f)
    {
        setValue(typed, "filterType", type);
        renderNote(typed, buffer);
        require(allSamplesFinite(buffer), "every filter type renders finite audio");
        levels.push_back(rms(buffer, 0, settled));
    }
    require(levels[0] > levels[1], "a 220 Hz note passes the low pass more than the high pass");
    require(levels[0] > 0.0f, "the low pass passes something");

    // Switching the filter module off bypasses the drive with it: drive is a
    // control of that module, not a master saturator that survives it.
    theta::forge::Processor off;
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

void envelopeSuite()
{
    // ENV 1 is hardwired to amplitude and is the one envelope Forge has, so its
    // timing is the timing of every note.
    constexpr double rate = 48000.0;
    constexpr int block = 64;
    enum Stage { idle, attack, decay, sustain, release };

    theta::forge::Processor processor;
    soloSineOnA(processor);
    setValue(processor, "attack", 0.1f);
    setValue(processor, "decay", 0.1f);
    setValue(processor, "sustain", 0.5f);
    setValue(processor, "release", 0.2f);
    processor.prepareToPlay(rate, block);

    juce::AudioBuffer<float> buffer(2, block);
    const auto advance = [&] (int samples, const juce::MidiBuffer& midi = {})
    {
        auto events = midi;
        for (auto rendered = 0; rendered < samples; rendered += block)
        {
            processor.processBlock(buffer, events);
            events.clear();
        }
    };

    juce::MidiBuffer noteOn;
    noteOn.addEvent(juce::MidiMessage::noteOn(1, 57, 1.0f), 0);

    // A tenth of the way into a 100 ms attack, the envelope is a tenth up.
    advance(static_cast<int>(rate * 0.01), noteOn);
    require(processor.envelopeStage() == attack, "a new note starts in attack");
    requireClose(processor.envelopeLevel(), 0.1f, 0.03f, "the attack climbs linearly");

    // Past the attack, into decay, and on to the sustain level.
    // 110 ms in: the 100 ms attack has finished and 10 ms of a 100 ms decay has
    // run, so the level has fallen a tenth of the way from 1.0 towards 0.5.
    advance(static_cast<int>(rate * 0.1));
    require(processor.envelopeStage() == decay, "the envelope reaches decay after the attack time");
    requireClose(processor.envelopeLevel(), 0.95f, 0.02f, "decay falls from the peak towards sustain");

    advance(static_cast<int>(rate * 0.12));
    require(processor.envelopeStage() == sustain, "the envelope settles into sustain after the decay time");
    requireClose(processor.envelopeLevel(), 0.5f, 0.02f, "sustain holds at the sustain level");

    advance(static_cast<int>(rate * 0.2));
    require(processor.envelopeStage() == sustain, "sustain holds for as long as the note is held");
    requireClose(processor.envelopeLevel(), 0.5f, 0.02f, "sustain does not drift");

    juce::MidiBuffer noteOff;
    noteOff.addEvent(juce::MidiMessage::noteOff(1, 57), 0);
    advance(static_cast<int>(rate * 0.1), noteOff);
    require(processor.envelopeStage() == release, "releasing the note enters release");
    requireClose(processor.envelopeLevel(), 0.25f, 0.03f, "release falls from the held level");

    advance(static_cast<int>(rate * 0.15));
    require(processor.envelopeStage() == idle, "the envelope reaches idle after the release time");
    requireClose(processor.envelopeLevel(), 0.0f, 0.001f, "an idle envelope is silent");

    // Released mid-attack, the fall starts from the level actually reached,
    // not from the sustain level the note never got to.
    theta::forge::Processor early;
    soloSineOnA(early);
    setValue(early, "attack", 1.0f);
    setValue(early, "decay", 0.1f);
    setValue(early, "sustain", 0.9f);
    setValue(early, "release", 0.2f);
    early.prepareToPlay(rate, block);

    const auto advanceEarly = [&] (int samples, const juce::MidiBuffer& midi = {})
    {
        auto events = midi;
        for (auto rendered = 0; rendered < samples; rendered += block)
        {
            early.processBlock(buffer, events);
            events.clear();
        }
    };

    advanceEarly(static_cast<int>(rate * 0.2), noteOn);
    const auto reached = early.envelopeLevel();
    require(early.envelopeStage() == attack, "a long attack is still climbing after 200 ms");
    requireClose(reached, 0.2f, 0.03f, "a fifth of the way up a one-second attack");

    advanceEarly(static_cast<int>(rate * 0.1), noteOff);
    require(early.envelopeStage() == release, "a note released mid-attack enters release");
    require(early.envelopeLevel() < reached,
            "a note released mid-attack falls rather than continuing to climb");
    requireClose(early.envelopeLevel(), reached * 0.5f, 0.03f,
                 "the fall starts from the level actually reached, not from sustain");

    advanceEarly(static_cast<int>(rate * 0.15));
    require(early.envelopeStage() == idle,
            "a release from a partial attack still takes the full release time");
}

bool identical(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples()) return false;
    for (int channel = 0; channel < a.getNumChannels(); ++channel)
        for (int i = 0; i < a.getNumSamples(); ++i)
            if (a.getSample(channel, i) != b.getSample(channel, i)) return false;
    return true;
}

// Destination indices, matching theta::forge::destinations().
enum Destination { destOff = 0, destAPitch = 5, destSub = 11, destCutoff = 13 };
enum Source { srcOff = 0, srcEnv1 = 1, srcLfo1 = 2, srcVelocity = 3 };

void setSlot(theta::forge::Processor& processor, int slot, float source, float destination, float depth)
{
    const auto id = [slot] (const char* suffix) { return "mod" + juce::String(slot) + suffix; };
    setValue(processor, id("Source").toRawUTF8(), source);
    setValue(processor, id("Dest").toRawUTF8(), destination);
    setValue(processor, id("Depth").toRawUTF8(), depth);
}

// A patch with a filter that is closed enough for cutoff modulation to be
// plainly audible, and no other slot interfering.
void closedFilterOnA(theta::forge::Processor& processor)
{
    soloSineOnA(processor);
    setValue(processor, "oscAPosition", 1.0f);   // square: harmonics for the filter to remove
    setValue(processor, "filterEnable", 1.0f);
    setValue(processor, "routeA", 1.0f);
    setValue(processor, "cutoff", 300.0f);
    setValue(processor, "resonance", 0.0f);
    for (int slot = 1; slot <= theta::forge::modSlotCount; ++slot)
        setSlot(processor, slot, srcOff, destOff, 0.0f);
}

void modulationSuite()
{
    constexpr int samples = 8192;
    constexpr int settled = 1024;
    juce::AudioBuffer<float> plain(2, samples), modulated(2, samples);

    // A slot at zero depth must cost nothing at all, not merely almost nothing:
    // an idle matrix may not colour the sound.
    theta::forge::Processor bare;
    closedFilterOnA(bare);
    renderNote(bare, plain);

    theta::forge::Processor wired;
    closedFilterOnA(wired);
    setSlot(wired, 1, srcLfo1, destCutoff, 0.0f);
    renderNote(wired, modulated);
    require(identical(plain, modulated), "a slot at zero depth renders bit-identically to no slot");

    // Pointed at nothing, a slot with depth is equally inert.
    theta::forge::Processor unpointed;
    closedFilterOnA(unpointed);
    setSlot(unpointed, 1, srcLfo1, destOff, 1.0f);
    renderNote(unpointed, modulated);
    require(identical(plain, modulated), "a slot with no destination renders bit-identically");

    // With depth, the envelope opens the filter and more gets through.
    theta::forge::Processor swept;
    closedFilterOnA(swept);
    setSlot(swept, 1, srcEnv1, destCutoff, 1.0f);
    renderNote(swept, modulated);
    const auto closed = rms(plain, 0, settled);
    const auto opened = rms(modulated, 0, settled);
    require(opened > closed * 1.2f, "an envelope pointed at the cutoff opens the filter");

    // Two slots on one destination sum, rather than one winning.
    theta::forge::Processor halves;
    closedFilterOnA(halves);
    setSlot(halves, 1, srcEnv1, destCutoff, 0.5f);
    setSlot(halves, 2, srcEnv1, destCutoff, 0.5f);
    juce::AudioBuffer<float> summed(2, samples);
    renderNote(halves, summed);
    require(identical(modulated, summed), "two half-depth slots sum to one full-depth slot");

    // Switching a slot's source off restores the unmodulated render exactly.
    setSlot(swept, 1, srcOff, destCutoff, 1.0f);
    renderNote(swept, modulated);
    require(identical(plain, modulated), "switching a slot's source off restores the plain render");

    // What the knobs draw is published from the same reading the voice renders
    // with, so a ring that moves is proof the engine moved the value, not a
    // second guess at it from the UI.
    theta::forge::Processor watched;
    closedFilterOnA(watched);
    require(watched.modulationOffset(destCutoff) == 0.0f,
            "an idle matrix publishes no offset for a knob to draw");

    setSlot(watched, 1, srcEnv1, destCutoff, 1.0f);
    renderNote(watched, modulated);
    const auto published = watched.modulationOffset(destCutoff);
    require(published > 0.0f, "an envelope pointed at the cutoff publishes an offset to draw");
    require(published <= 1.0f, "a unipolar source at full depth cannot publish past full travel");
    // At full depth from ENV 1 the offset is the envelope itself, so the ring on
    // the cutoff knob and the curve on ENV 1's display cannot disagree.
    require(published == watched.envelopeLevel(),
            "the offset drawn on a knob is the same reading ENV 1's own display draws");
    require(watched.modulationOffset(destSub) == 0.0f,
            "a destination nothing points at publishes nothing");

    // Pointing the same slot somewhere else has to release the knob it left.
    setSlot(watched, 1, srcEnv1, destSub, 1.0f);
    renderNote(watched, modulated);
    require(watched.modulationOffset(destCutoff) == 0.0f,
            "a destination a slot has left goes back to publishing nothing");

    // A source only reaches a destination through a voice, so with nothing
    // sounding there is no modulated value and the knobs have nothing to
    // animate. This holds for a macro too, which is the case that looks most
    // like it ought to be an exception: the hand is on the macro, but until a
    // note is played the macro is moving nothing.
    theta::forge::Processor idle;
    closedFilterOnA(idle);
    setSlot(idle, 1, static_cast<float>(theta::forge::ModSource::macro1), destCutoff, 1.0f);
    setValue(idle, "macro1", 0.5f);
    idle.prepareToPlay(48000.0, samples);
    juce::AudioBuffer<float> silence(2, samples);
    juce::MidiBuffer noNotes;
    silence.clear();
    idle.processBlock(silence, noNotes);
    require(idle.modulationOffset(destCutoff) == 0.0f,
            "nothing sounding publishes no offset, so the rings stay still");

    // And the same patch under a note does publish, so the check above is
    // measuring silence rather than a routing that was never live.
    renderNote(idle, modulated);
    requireClose(idle.modulationOffset(destCutoff), 0.5f, 0.001f,
                 "the same macro publishes its offset once a note is sounding");

    // Depth clamps at the destination's own limits instead of running past them.
    theta::forge::Processor slammed;
    closedFilterOnA(slammed);
    setValue(slammed, "cutoff", 18000.0f);
    setSlot(slammed, 1, srcEnv1, destCutoff, 1.0f);
    renderNote(slammed, modulated);
    require(allSamplesFinite(modulated), "modulation past a parameter's top stays finite");
    theta::forge::Processor atTop;
    closedFilterOnA(atTop);
    setValue(atTop, "cutoff", 18000.0f);
    renderNote(atTop, plain);
    require(identical(plain, modulated),
            "modulating a parameter already at its maximum changes nothing");

    // Velocity is a source like any other, and a softer note modulates less.
    const auto atVelocity = [&] (float velocity)
    {
        theta::forge::Processor processor;
        closedFilterOnA(processor);
        setSlot(processor, 1, srcVelocity, destCutoff, 1.0f);
        processor.prepareToPlay(48000.0, samples);
        juce::AudioBuffer<float> buffer(2, samples);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 57, velocity), 0);
        processor.processBlock(buffer, midi);
        // Measured as brightness rather than level, so this cannot be
        // satisfied merely by a hard note being louder than a soft one.
        return brightness(buffer, 0, settled);
    };
    require(atVelocity(1.0f) > atVelocity(0.25f) * 1.1f,
            "a harder note opens a velocity-driven filter further");

    // Pitch is reachable now that Semitone is continuous, which is what
    // replaced the old hardwired LFO-to-pitch knob.
    theta::forge::Processor bent;
    soloSineOnA(bent);
    for (int slot = 1; slot <= theta::forge::modSlotCount; ++slot)
        setSlot(bent, slot, srcOff, destOff, 0.0f);
    renderNote(bent, plain);
    const auto atPitch = zeroCrossings(plain, 0, settled);
    setSlot(bent, 1, srcEnv1, destAPitch, 1.0f);
    renderNote(bent, modulated);
    require(zeroCrossings(modulated, 0, settled) > atPitch,
            "an envelope pointed at pitch raises the note");

    // Macros are sources like any other, and reach their target only through
    // the matrix.
    const auto firstMacro = static_cast<float>(theta::forge::ModSource::macro1);
    require(theta::forge::modSourceCount == static_cast<int>(theta::forge::ModSource::macro1)
                + theta::forge::macroCount,
            "every macro is offered as a source");

    theta::forge::Processor byMacro;
    closedFilterOnA(byMacro);
    setValue(byMacro, "macro1", 1.0f);
    renderNote(byMacro, plain);
    require(rms(plain, 0, settled) > 0.0f, "a macro alone changes nothing until it is routed");

    setSlot(byMacro, 1, firstMacro, destCutoff, 1.0f);
    renderNote(byMacro, modulated);
    require(brightness(modulated, 0, settled) > brightness(plain, 0, settled) * 1.1f,
            "a macro turned up opens the filter it is pointed at");

    setValue(byMacro, "macro1", 0.0f);
    renderNote(byMacro, modulated);
    require(identical(plain, modulated), "a macro at zero leaves its target exactly where it was");

    // And a source that never moves still behaves: NOTE is constant per voice.
    theta::forge::Processor byNote;
    closedFilterOnA(byNote);
    setValue(byNote, "subEnable", 1.0f);
    setValue(byNote, "subLevel", 0.0f);
    renderNote(byNote, plain, 36);
    setSlot(byNote, 1, 4.0f, destSub, 1.0f);
    renderNote(byNote, modulated, 36);
    require(!identical(plain, modulated), "the note source reaches its destination");
    require(rms(modulated, 0, settled) > rms(plain, 0, settled),
            "note-driven modulation adds the sub it was pointed at");
}

// A host that reports one fixed tempo and nothing else, so a synced LFO can be
// checked against a BPM the test controls.
class FixedTempo final : public juce::AudioPlayHead
{
public:
    explicit FixedTempo(double beatsPerMinute) : bpm(beatsPerMinute) {}

    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override
    {
        juce::AudioPlayHead::PositionInfo info;
        info.setBpm(bpm);
        return info;
    }

    double bpm = 120.0;
};

void lfoSuite()
{
    using theta::forge::LfoShape;
    constexpr int samples = 8192;

    // Every shape has to stay inside the bounds a source promises, and has to
    // come back to where it started after exactly one cycle. A source that ran
    // past one would push a destination further than its depth allows.
    for (int shape = 0; shape < theta::forge::lfoShapeCount; ++shape)
    {
        const auto which = static_cast<LfoShape>(shape);
        auto extreme = 0.0f;
        for (int i = 0; i <= 512; ++i)
        {
            const auto phase = static_cast<float>(i) / 512.0f;
            const auto at = theta::forge::lfoWave(which, phase, 1.0f);
            require(std::isfinite(at), "an LFO shape is finite everywhere");
            extreme = std::max(extreme, std::abs(at));
        }
        require(extreme <= 1.0f, "an LFO shape stays inside plus or minus one");
        require(extreme > 0.9f, "an LFO shape uses the range it is given");
    }

    // Sine and triangle join up at the cycle boundary. A saw and a square jump
    // a full swing there instead, and that jump is the shape rather than a
    // fault — checked so neither can be quietly smoothed away.
    for (const auto continuous : {LfoShape::sine, LfoShape::triangle})
        requireClose(theta::forge::lfoWave(continuous, 0.0f, 0.0f),
                     theta::forge::lfoWave(continuous, 1.0f, 0.0f), 0.0001f,
                     "a continuous LFO shape joins up across the cycle");
    for (const auto stepped : {LfoShape::saw, LfoShape::square})
        require(std::abs(theta::forge::lfoWave(stepped, 0.0f, 0.0f)
                         - theta::forge::lfoWave(stepped, 0.999f, 0.0f)) > 1.5f,
                "a saw and a square jump a full swing at the cycle boundary");

    // Sample and hold is its held step and nothing else: it does not move
    // within a cycle, which is what makes it a step rather than a ramp.
    for (const auto phase : {0.0f, 0.2f, 0.75f, 0.99f})
        requireClose(theta::forge::lfoWave(LfoShape::sampleHold, phase, -0.4f), -0.4f, 0.0001f,
                     "sample and hold holds its step for the whole cycle");

    // Shape is a real choice, not a relabelled sine: the four continuous shapes
    // have to differ from each other somewhere.
    for (int a = 0; a < 4; ++a)
        for (int b = a + 1; b < 4; ++b)
        {
            auto differs = false;
            for (int i = 0; i < 64; ++i)
            {
                const auto phase = static_cast<float>(i) / 64.0f;
                if (std::abs(theta::forge::lfoWave(static_cast<LfoShape>(a), phase, 0.0f)
                             - theta::forge::lfoWave(static_cast<LfoShape>(b), phase, 0.0f)) > 0.01f)
                    differs = true;
            }
            require(differs, "no two LFO shapes are the same curve");
        }

    // Free-running, the rate is the knob.
    theta::forge::Processor free;
    setValue(free, "lfoSync", 0.0f);
    setValue(free, "lfoRate", 3.0f);
    requireClose(free.lfoRateHz(), 3.0f, 0.001f, "an unsynced LFO runs at its rate knob");

    // Synced, the rate is a division of the host's tempo and the knob stops
    // mattering. 1/4 at 120 BPM is two beats a second, so two cycles a second.
    theta::forge::Processor synced;
    setValue(synced, "lfoSync", 1.0f);
    setValue(synced, "lfoRate", 3.0f);
    setValue(synced, "lfoDivision", 2.0f);
    FixedTempo tempo(120.0);
    synced.setPlayHead(&tempo);
    juce::AudioBuffer<float> buffer(2, samples);
    juce::MidiBuffer midi;
    synced.prepareToPlay(48000.0, samples);
    synced.processBlock(buffer, midi);
    requireClose(synced.lfoRateHz(), 2.0f, 0.001f, "a synced LFO divides the host tempo");

    // And it follows the tempo rather than latching the first one it saw.
    tempo.bpm = 60.0;
    synced.processBlock(buffer, midi);
    requireClose(synced.lfoRateHz(), 1.0f, 0.001f, "a synced LFO tracks a tempo change");

    // A longer division is a slower cycle, in proportion.
    setValue(synced, "lfoDivision", 0.0f);   // 1/1, a bar of four beats
    requireClose(synced.lfoRateHz(), 0.25f, 0.001f, "a whole-bar division is four beats long");

    // The phase the display draws has to be the one the voice is reading, and
    // it has to move.
    theta::forge::Processor running;
    setValue(running, "lfoSync", 0.0f);
    setValue(running, "lfoRate", 1.0f);
    running.prepareToPlay(48000.0, samples);
    juce::MidiBuffer none;
    buffer.clear();
    running.processBlock(buffer, none);
    const auto first = running.lfoPhase();
    running.processBlock(buffer, none);
    const auto second = running.lfoPhase();
    require(first >= 0.0f && first < 1.0f, "the published LFO phase stays inside one cycle");
    require(second != first, "the published LFO phase advances with the blocks");

    synced.setPlayHead(nullptr);
}

void voicingSuite()
{
    // Mono collapses to a single voice, so polyphony stops meaning anything.
    // The panel greys the POLY knob out to say so; this checks the engine
    // agrees rather than the two drifting apart.
    constexpr int samples = 8192;
    constexpr int settled = 1024;
    juce::AudioBuffer<float> buffer(2, samples);

    const auto chordLevel = [&buffer] (bool mono, float polyphony)
    {
        theta::forge::Processor processor;
        soloSineOnA(processor);
        setValue(processor, "mono", mono ? 1.0f : 0.0f);
        setValue(processor, "polyphony", polyphony);
        setValue(processor, "glide", 0.0f);
        processor.prepareToPlay(48000.0, samples);
        juce::MidiBuffer midi;
        for (const auto note : {52, 57, 61})
            midi.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), 0);
        processor.processBlock(buffer, midi);
        return rms(buffer, 0, settled);
    };

    const auto polyThree = chordLevel(false, 8.0f);
    const auto monoThree = chordLevel(true, 8.0f);
    require(polyThree > 0.0f && monoThree > 0.0f, "both voicings make sound");
    require(monoThree < polyThree * 0.8f, "mono plays one note where poly plays three");

    // And polyphony genuinely does nothing while mono is on.
    requireClose(chordLevel(true, 1.0f), monoThree, monoThree * 0.001f,
                 "polyphony does not affect a mono patch");
    requireClose(chordLevel(true, 16.0f), monoThree, monoThree * 0.001f,
                 "raising polyphony does not affect a mono patch either");

    // While in poly it very much does.
    require(chordLevel(false, 1.0f) < polyThree * 0.8f, "polyphony limits a polyphonic patch");
}

void engineSuite()
{
    theta::forge::Processor processor;

    // A silent patch really is silent: with every source switched off, nothing
    // reaches the output however the level knobs are set.
    for (const auto* id : {"oscAEnable", "oscBEnable", "subEnable", "noiseEnable"})
        setValue(processor, id, 0.0f);
    setValue(processor, "subLevel", 1.0f);
    setValue(processor, "noiseLevel", 1.0f);
    require(peakForNote(processor) == 0.0f, "every source disabled renders silence");

    // Each source is independently audible.
    setValue(processor, "subEnable", 1.0f);
    const auto subOnly = peakForNote(processor);
    require(subOnly > 0.0f, "the sub module alone makes sound");
    setValue(processor, "subEnable", 0.0f);

    setValue(processor, "noiseEnable", 1.0f);
    require(peakForNote(processor) > 0.0f, "the noise module alone makes sound");
    setValue(processor, "noiseEnable", 0.0f);

    setValue(processor, "oscAEnable", 1.0f);
    const auto oscAOnly = peakForNote(processor);
    require(oscAOnly > 0.0f, "oscillator A alone makes sound");

    // Each oscillator owns its level outright. Nothing about B may reach A.
    setValue(processor, "oscBLevel", 0.9f);
    setValue(processor, "oscBDetune", 1.0f);
    setValue(processor, "oscBUnison", 8.0f);
    requireClose(peakForNote(processor), oscAOnly, 0.0001f,
                 "oscillator B's controls do not affect A while B is switched off");

    setValue(processor, "oscBEnable", 1.0f);
    require(peakForNote(processor) > 0.0f, "both oscillators together make sound");

    // Bypassing the filter must actually bypass it: a cutoff low enough to
    // remove nearly everything should stop mattering.
    setValue(processor, "cutoff", 60.0f);
    setValue(processor, "filterEnable", 1.0f);
    const auto filtered = peakForNote(processor);
    setValue(processor, "filterEnable", 0.0f);
    const auto bypassed = peakForNote(processor);
    require(bypassed > filtered * 2.0f, "switching the filter off bypasses it");

    filterRoutingSuite();
    envelopeSuite();
    lfoSuite();
    voicingSuite();
    modulationSuite();

    oscillatorSuite();

    // Output stays finite and bounded across an extreme patch.
    theta::forge::Processor extreme;
    for (const auto* id : {"oscAEnable", "oscBEnable", "subEnable", "noiseEnable", "filterEnable"})
        setValue(extreme, id, 1.0f);
    setValue(extreme, "oscAUnison", 8.0f);
    setValue(extreme, "oscBUnison", 8.0f);
    setValue(extreme, "oscADetune", 1.0f);
    setValue(extreme, "oscBDetune", 1.0f);
    setValue(extreme, "resonance", 1.0f);
    setValue(extreme, "drive", 1.0f);
    setValue(extreme, "output", 1.25f);
    setValue(extreme, "subLevel", 1.0f);
    setValue(extreme, "noiseLevel", 1.0f);
    setValue(extreme, "lfoRate", 20.0f);
    // Drive the matrix hard too: LFO 1 into the cutoff and into oscillator A's
    // pitch, both at full depth.
    setValue(extreme, "mod1Source", 2.0f);
    setValue(extreme, "mod1Dest", 13.0f);
    setValue(extreme, "mod1Depth", 1.0f);
    setValue(extreme, "mod2Source", 2.0f);
    setValue(extreme, "mod2Dest", 5.0f);
    setValue(extreme, "mod2Depth", 1.0f);

    constexpr int blockSize = 512;
    extreme.prepareToPlay(48000.0, blockSize);
    juce::AudioBuffer<float> buffer(2, blockSize);
    juce::MidiBuffer midi;
    for (int note = 48; note < 58; ++note)
        midi.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), 0);
    auto peak = 0.0f;
    for (int block = 0; block < 40; ++block)
    {
        extreme.processBlock(buffer, midi);
        midi.clear();
        require(allSamplesFinite(buffer), "every rendered sample is finite");
        peak = juce::jmax(peak, buffer.getMagnitude(0, blockSize));
    }
    require(peak > 0.0f, "an extreme patch still produces signal");
    require(peak <= 1.0f, "an extreme patch stays within full scale");
}
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI initialiseJuce;
    const juce::String suite = argc > 1 ? argv[1] : "";

    if (suite.isEmpty() || suite == "--layout") layoutSuite();
    if (suite.isEmpty() || suite == "--presets") presetSuite();
    if (suite.isEmpty() || suite == "--engine") engineSuite();

    if (failures > 0)
    {
        std::cerr << failures << " Forge check(s) failed\n";
        return 1;
    }
    std::cout << "Theta Forge checks passed" << (suite.isEmpty() ? "" : " (" + suite + ")") << '\n';
    return 0;
}
