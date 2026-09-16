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
        for (const auto& knob : module.knobs)
            require(processor.state.getParameter(knob.id) != nullptr,
                    "a module's knob id names a real parameter");
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
            for (const auto& knob : module.knobs)
                if (withId->paramID == knob.id) found = true;
        }
        require(found, "every parameter appears somewhere on the panel");
        if (!found) std::cerr << "       orphan parameter: " << withId->paramID << '\n';
    }

    for (size_t i = 0; i < modules.size(); ++i)
    {
        const auto area = theta::forge::ui::moduleBounds(bounds, modules[i]);
        require(!area.isEmpty(), "a module occupies a non-empty rectangle");
        require(content.contains(area), "a module stays inside the content area");

        const auto knobRow = theta::forge::ui::knobRowBounds(area, modules[i]);
        require(knobRow.getHeight() > 30, "a module leaves usable height for its knobs");
        require(area.withTrimmedTop(theta::forge::ui::headerHeight).contains(knobRow),
                "knobs stay clear of the module header, so labels cannot collide with the title");

        for (size_t j = i + 1; j < modules.size(); ++j)
            require(!area.intersects(theta::forge::ui::moduleBounds(bounds, modules[j])),
                    "no two modules overlap");
    }

    // The proportions have to survive the whole resize range, not just the
    // default size.
    for (const auto size : {juce::Point<int>(1060, 760), juce::Point<int>(1800, 1200)})
    {
        const auto resized = juce::Rectangle<int>(0, 0, size.x, size.y);
        for (const auto& module : modules)
        {
            const auto area = theta::forge::ui::moduleBounds(resized, module);
            require(theta::forge::ui::contentBounds(resized).contains(area),
                    "a module stays inside the content area at every allowed size");
            require(theta::forge::ui::knobRowBounds(area, module).getHeight() > 24,
                    "knobs stay usable at every allowed size");
        }
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
    requireText(textFor(processor, "oscBTune", 7.0f), "+7 st", "tune reads as signed semitones");
    requireText(textFor(processor, "unison", 4.0f), "4", "unison reads as a plain count");
    requireText(textFor(processor, "lfoCutoff", -0.5f), "-50 %", "a bipolar depth keeps its sign");
    require(!textFor(processor, "cutoff", 7800.0f).contains("7800.0004"),
            "no knob falls back to a raw float readout");

    directory.deleteRecursively();
}

// ---------------------------------------------------------------- engine ---

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

    // With B switched off, A takes the whole voice instead of being scaled down
    // by B's level, so muting B must not make A quieter.
    setValue(processor, "oscBLevel", 0.9f);
    requireClose(peakForNote(processor), oscAOnly, 0.0001f,
                 "oscillator B's level does not affect A while B is switched off");

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

    // Output stays finite and bounded across an extreme patch.
    theta::forge::Processor extreme;
    for (const auto* id : {"oscAEnable", "oscBEnable", "subEnable", "noiseEnable", "filterEnable"})
        setValue(extreme, id, 1.0f);
    setValue(extreme, "unison", 8.0f);
    setValue(extreme, "detune", 1.0f);
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
