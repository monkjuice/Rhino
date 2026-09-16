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
    const auto bounds = juce::Rectangle<int>(0, 0, 1180, 780);
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

        for (size_t j = i + 1; j < modules.size(); ++j)
            require(!area.intersects(theta::forge::ui::moduleBounds(bounds, modules[j])),
                    "no two modules overlap");
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
                if (row.controls[static_cast<size_t>(i)].style == theta::forge::ui::Style::knob)
                    require(block.getWidth() == diameter, "every knob is drawn at the shared diameter");
            }
        }
    }

    // The proportions have to survive the whole resize range, not just the
    // default size.
    for (const auto size : {juce::Point<int>(1100, 840), juce::Point<int>(1800, 1200)})
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
    requireText(textFor(processor, "lfoCutoff", -0.5f), "-50 %", "a bipolar depth keeps its sign");
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
    setValue(extreme, "lfoCutoff", 1.0f);
    setValue(extreme, "lfoPitch", 12.0f);

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
