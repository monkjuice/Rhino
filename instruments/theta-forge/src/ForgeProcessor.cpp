#include "ForgeProcessor.h"
#include "ForgeEditor.h"

namespace theta::forge
{
namespace
{
// Value formatting belongs to the parameter, not the editor. A
// SliderAttachment overwrites any textFromValueFunction the editor sets, so a
// formatter installed there never runs and every knob reads "0.5500000".
// Declaring it here means the host's automation lane and Forge's own knobs
// show the same text.
using Format = juce::String (*)(float);

juce::String asPercent(float value) { return juce::String(juce::roundToInt(value * 100.0f)) + " %"; }

juce::String asSignedPercent(float value)
{
    return (value > 0.0f ? "+" : "") + juce::String(juce::roundToInt(value * 100.0f)) + " %";
}

juce::String asSemitones(float value)
{
    return (value > 0.0f ? "+" : "") + juce::String(juce::roundToInt(value)) + " st";
}

juce::String asHertz(float value)
{
    return value >= 1000.0f ? juce::String(value / 1000.0f, 2) + " kHz"
                            : juce::String(juce::roundToInt(value)) + " Hz";
}

juce::String asRate(float value) { return juce::String(value, 2) + " Hz"; }

juce::String asSeconds(float value)
{
    return value < 1.0f ? juce::String(juce::roundToInt(value * 1000.0f)) + " ms"
                        : juce::String(value, 2) + " s";
}

juce::String asCount(float value) { return juce::String(juce::roundToInt(value)); }

juce::String asGain(float value) { return juce::String(value, 2); }

juce::String asOctaves(float value)
{
    const auto octaves = juce::roundToInt(value);
    return octaves == 0 ? juce::String("0") : (octaves > 0 ? "+" : "") + juce::String(octaves) + " oct";
}

juce::String asCents(float value)
{
    const auto cents = juce::roundToInt(value);
    return cents == 0 ? juce::String("0") : (cents > 0 ? "+" : "") + juce::String(cents) + " ct";
}

juce::String asPan(float value)
{
    const auto amount = juce::roundToInt(std::abs(value) * 100.0f);
    if (amount == 0) return "C";
    return (value < 0.0f ? "L" : "R") + juce::String(amount);
}

// Format 2 dropped the effects, the macros and the filter envelope. Format 1
// files are not accepted; they described a synth that no longer exists.
//
// Within a version, presets are still reconciled against the parameters that
// exist when they are opened — see Processor::migrated. That is what keeps a
// preset saved at one milestone working at the next, while Forge's controls
// are still moving.
constexpr int presetFormatVersion = 2;

juce::ValueTree parameterEntry(const juce::ValueTree& tree, const juce::String& id)
{
    for (const auto child : tree)
        if (child.getProperty("id").toString() == id)
            return child;
    return {};
}

std::unique_ptr<juce::RangedAudioParameter> parameter(const juce::String& id, const juce::String& name,
                                                      juce::NormalisableRange<float> range,
                                                      float initial, Format format)
{
    // Explicit version hints make VST3 parameter IDs stable from the first
    // release, rather than relying on JUCE's legacy VST2-compatible mapping.
    return std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID {id, 1}, name, range, initial,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [format] (float value, int) { return format(value); }));
}

// Module enables are genuine switches, so they are declared as bools and show
// up in a host's automation lane as on/off rather than as a float that happens
// to be stepped.
std::unique_ptr<juce::RangedAudioParameter> toggle(const juce::String& id, const juce::String& name, bool initial)
{
    return std::make_unique<juce::AudioParameterBool>(juce::ParameterID {id, 1}, name, initial);
}
}

juce::AudioProcessorValueTreeState::ParameterLayout Processor::parameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> result;

    // The two oscillators are declared identically. Neither is expressed in
    // terms of the other, so each owns its tuning, its stack, its pan and its
    // level outright.
    const auto oscillator = [&result] (const char* prefix, const char* label, bool enabled,
                                       float position, float semitone, float level)
    {
        const auto id = [prefix] (const char* suffix) { return juce::String(prefix) + suffix; };
        const auto name = [label] (const char* suffix) { return juce::String(label) + " " + suffix; };
        result.push_back(toggle(id("Enable"), name("Enable"), enabled));
        result.push_back(parameter(id("Position"), name("Position"), {0.0f, 1.0f}, position, asPercent));
        result.push_back(parameter(id("Octave"), name("Octave"), {-4.0f, 4.0f, 1.0f}, 0.0f, asOctaves));
        result.push_back(parameter(id("Semitone"), name("Semitone"), {-12.0f, 12.0f}, semitone, asSemitones));
        result.push_back(parameter(id("Fine"), name("Fine"), {-100.0f, 100.0f, 1.0f}, 0.0f, asCents));
        result.push_back(parameter(id("Unison"), name("Unison"), {1.0f, 8.0f, 1.0f}, 2.0f, asCount));
        result.push_back(parameter(id("Detune"), name("Detune"), {0.0f, 1.0f}, 0.18f, asPercent));
        result.push_back(parameter(id("Blend"), name("Blend"), {0.0f, 1.0f}, 0.5f, asPercent));
        result.push_back(parameter(id("Pan"), name("Pan"), {-1.0f, 1.0f}, 0.0f, asPan));
        result.push_back(parameter(id("Level"), name("Level"), {0.0f, 1.0f}, level, asPercent));
    };
    oscillator("oscA", "Osc A", true, 0.55f, 0.0f, 0.75f);
    oscillator("oscB", "Osc B", true, 0.18f, 7.0f, 0.25f);

    result.push_back(toggle("subEnable", "Sub Enable", true));
    result.push_back(parameter("subLevel", "Sub Level", {0.0f, 1.0f}, 0.12f, asPercent));
    result.push_back(toggle("noiseEnable", "Noise Enable", false));
    result.push_back(parameter("noiseLevel", "Noise Level", {0.0f, 1.0f}, 0.25f, asPercent));
    result.push_back(toggle("filterEnable", "Filter Enable", true));
    result.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID {"filterType", 1}, "Filter Type",
        juce::StringArray {"LP", "HP", "BP"}, 0));
    result.push_back(toggle("routeA", "Filter Route Osc A", true));
    result.push_back(toggle("routeB", "Filter Route Osc B", true));
    result.push_back(toggle("routeSub", "Filter Route Sub", true));
    result.push_back(toggle("routeNoise", "Filter Route Noise", true));
    result.push_back(parameter("cutoff", "Cutoff", {30.0f, 18000.0f, 0.0f, 0.25f}, 7800.0f, asHertz));
    result.push_back(parameter("resonance", "Resonance", {0.0f, 1.0f}, 0.12f, asPercent));
    result.push_back(parameter("drive", "Drive", {0.0f, 1.0f}, 0.08f, asPercent));
    result.push_back(parameter("attack", "Attack", {0.001f, 4.0f, 0.0f, 0.35f}, 0.01f, asSeconds));
    result.push_back(parameter("decay", "Decay", {0.001f, 4.0f, 0.0f, 0.35f}, 0.24f, asSeconds));
    result.push_back(parameter("sustain", "Sustain", {0.0f, 1.0f}, 0.75f, asPercent));
    result.push_back(parameter("release", "Release", {0.001f, 8.0f, 0.0f, 0.35f}, 0.35f, asSeconds));
    result.push_back(parameter("lfoRate", "LFO Rate", {0.05f, 20.0f, 0.0f, 0.35f}, 0.5f, asRate));
    result.push_back(parameter("polyphony", "Polyphony", {1.0f, 16.0f, 1.0f}, 8.0f, asCount));
    result.push_back(toggle("mono", "Mono", false));
    result.push_back(toggle("legato", "Legato", true));
    result.push_back(parameter("glide", "Glide", {0.0f, 2.0f, 0.0f, 0.35f}, 0.08f, asSeconds));
    result.push_back(parameter("output", "Output", {0.0f, 1.25f}, 0.75f, asGain));

    for (int macro = 1; macro <= macroCount; ++macro)
        result.push_back(parameter("macro" + juce::String(macro), "Macro " + juce::String(macro),
                                   {0.0f, 1.0f}, 0.0f, asPercent));

    // Eight modulation slots. Each is three parameters so a host can automate a
    // routing as readily as a knob, and so the whole matrix saves with a preset
    // without a separate serialisation path.
    juce::StringArray sourceNames;
    for (int i = 0; i < modSourceCount; ++i) sourceNames.add(modSourceName(i));
    juce::StringArray destinationNames;
    for (const auto& destination : destinations()) destinationNames.add(destination.label);

    for (int slot = 1; slot <= modSlotCount; ++slot)
    {
        const auto id = [slot] (const char* suffix) { return "mod" + juce::String(slot) + suffix; };
        const auto name = [slot] (const char* suffix) { return "Mod " + juce::String(slot) + " " + suffix; };
        // Slot 1 is pre-wired to LFO 1 into the cutoff at zero depth, so the
        // most common first move is one knob rather than three.
        result.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID {id("Source"), 1}, name("Source"), sourceNames,
            slot == 1 ? static_cast<int>(ModSource::lfo1) : 0));
        result.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID {id("Dest"), 1}, name("Destination"), destinationNames,
            slot == 1 ? 13 : 0));
        result.push_back(parameter(id("Depth"), name("Depth"), {-1.0f, 1.0f}, 0.0f, asSignedPercent));
    }
    return {result.begin(), result.end()};
}

Processor::Processor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "ThetaForgeState", parameterLayout())
{
}

void Processor::prepareToPlay(double sampleRate, int)
{
    core.initialise(sampleRate);
    // Hand the engine each destination's range so modulation happens in the
    // same normalised space the knob moves in. Taken from the parameters
    // themselves, so there is only ever one definition of a range.
    for (int i = 1; i < destinationCount; ++i)
        if (const auto* parameter = state.getParameter(destinations()[static_cast<size_t>(i)].id))
            core.setDestinationRange(i, parameter->getNormalisableRange());
}

bool Processor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    // Anything played on the editor's keyboard joins the host's own notes
    // before a single sample is rendered.
    keyboardState.processNextMidiBuffer(midi, 0, buffer.getNumSamples(), true);
    const auto values = patch();
    const auto mods = modulation();
    auto event = midi.cbegin();
    const auto end = midi.cend();
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        while (event != end)
        {
            const auto metadata = *event;
            if (metadata.samplePosition > i) break;
            const auto message = metadata.getMessage();
            if (message.isNoteOn()) core.noteOn(message.getNoteNumber(), message.getFloatVelocity(), values);
            else if (message.isNoteOff()) core.noteOff(message.getNoteNumber());
            else if (message.isAllNotesOff()) core.allNotesOff();
            ++event;
        }
        float left, right; core.renderSample(values, mods, left, right);
        buffer.addSample(0, i, left);
        if (buffer.getNumChannels() > 1) buffer.addSample(1, i, right);
    }

    // Published once per block rather than per sample: the display redraws at
    // 24 Hz, so a per-sample store would be pure contention for no extra detail.
    meterLevel.store(core.envelopeLevel(), std::memory_order_relaxed);
    meterStage.store(core.envelopeStage(), std::memory_order_relaxed);
    for (int destination = 1; destination < destinationCount; ++destination)
        meterOffsets[static_cast<size_t>(destination)]
            .store(core.modulationOffset(destination), std::memory_order_relaxed);
}

Patch Processor::patch() const
{
    // Assigned by name, not positionally: the parameter list and the Patch
    // layout no longer have to be kept in the same order to stay correct.
    // Nothing is remapped on the way through — the macros that used to bend
    // these values were hardwired offsets, and return as real assignable
    // sources with the modulation matrix.
    const auto value = [this] (const juce::String& id) { return state.getRawParameterValue(id)->load(); };
    const auto readOscillator = [&value] (const char* prefix)
    {
        const auto id = [prefix] (const char* suffix) { return juce::String(prefix) + suffix; };
        Oscillator osc;
        osc.enable = value(id("Enable"));
        osc.position = value(id("Position"));
        osc.octave = value(id("Octave"));
        osc.semitone = value(id("Semitone"));
        osc.fine = value(id("Fine"));
        osc.unison = value(id("Unison"));
        osc.detune = value(id("Detune"));
        osc.blend = value(id("Blend"));
        osc.pan = value(id("Pan"));
        osc.level = value(id("Level"));
        return osc;
    };

    Patch result;
    result.a = readOscillator("oscA");
    result.b = readOscillator("oscB");
    result.subEnable = value("subEnable");
    result.subLevel = value("subLevel");
    result.noiseEnable = value("noiseEnable");
    result.noiseLevel = value("noiseLevel");
    result.filterEnable = value("filterEnable");
    result.filterType = value("filterType");
    result.routeA = value("routeA");
    result.routeB = value("routeB");
    result.routeSub = value("routeSub");
    result.routeNoise = value("routeNoise");
    result.cutoff = value("cutoff");
    result.resonance = value("resonance");
    result.drive = value("drive");
    result.attack = value("attack");
    result.decay = value("decay");
    result.sustain = value("sustain");
    result.release = value("release");
    result.lfoRate = value("lfoRate");
    result.polyphony = value("polyphony");
    result.mono = value("mono");
    result.legato = value("legato");
    result.glide = value("glide");
    result.output = value("output");
    for (int macro = 0; macro < macroCount; ++macro)
        result.macros[static_cast<size_t>(macro)] = value("macro" + juce::String(macro + 1));
    return result;
}

Modulation Processor::modulation() const
{
    const auto value = [this] (const juce::String& id) { return state.getRawParameterValue(id)->load(); };
    Modulation result;
    for (int slot = 0; slot < modSlotCount; ++slot)
    {
        const auto id = [slot] (const char* suffix) { return "mod" + juce::String(slot + 1) + suffix; };
        result.slots[static_cast<size_t>(slot)] = {value(id("Source")), value(id("Dest")), value(id("Depth"))};
    }
    return result;
}

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor(*this); }

void Processor::getStateInformation(juce::MemoryBlock& destination)
{
    if (const auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, destination);
}

void Processor::setStateInformation(const void* data, int size)
{
    // Host state gets the same reconciliation as a preset file, so a project
    // saved against an older Forge opens without carrying retired parameters
    // or leaving newer ones unset.
    if (const auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName(state.state.getType()))
            state.replaceState(migrated(juce::ValueTree::fromXml(*xml)));
}

juce::Result Processor::savePreset(const juce::File& destination, const juce::String& name)
{
    if (destination == juce::File {}) return juce::Result::fail("Choose a preset file first.");
    juce::ValueTree preset("ThetaForgePreset");
    preset.setProperty("formatVersion", presetFormatVersion, nullptr);
    preset.setProperty("name", name.trim().isNotEmpty() ? name.trim()
                                                        : destination.getFileNameWithoutExtension(), nullptr);
    preset.addChild(state.copyState(), -1, nullptr);
    const auto xml = preset.createXml();
    if (xml == nullptr) return juce::Result::fail("Forge could not create the preset data.");

    juce::TemporaryFile temporary(destination);
    if (!xml->writeTo(temporary.getFile(), {}))
        return juce::Result::fail("Forge could not write the preset file.");
    if (!temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail("Forge could not replace the preset file.");
    return juce::Result::ok();
}

juce::Result Processor::loadPreset(const juce::File& source)
{
    const auto xml = juce::XmlDocument::parse(source);
    if (xml == nullptr) return juce::Result::fail("That file is not readable XML.");
    const auto preset = juce::ValueTree::fromXml(*xml);
    if (!preset.hasType("ThetaForgePreset"))
        return juce::Result::fail("That file is not a Theta Forge preset.");
    const auto version = static_cast<int>(preset.getProperty("formatVersion", 0));
    if (version != presetFormatVersion)
        return juce::Result::fail("This preset was saved by a different version of Forge.");
    const auto savedState = preset.getChildWithName(state.state.getType());
    if (!savedState.isValid())
        return juce::Result::fail("The preset does not contain Forge parameter state.");
    state.replaceState(migrated(savedState));
    return juce::Result::ok();
}

// Reconcile a preset of any supported version with the parameters that exist
// now: drop entries Forge has retired, and fill in entries it has since gained
// with their defaults. replaceState only touches parameters the tree mentions,
// so without the second half an old preset would leave newer controls holding
// the previous patch's values.
juce::ValueTree Processor::migrated(const juce::ValueTree& savedState) const
{
    auto result = savedState.createCopy();
    for (int i = result.getNumChildren(); --i >= 0;)
    {
        const auto id = result.getChild(i).getProperty("id").toString();
        if (id.isNotEmpty() && state.getParameter(id) == nullptr)
            result.removeChild(i, nullptr);
    }
    for (auto* raw : getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(raw);
        if (ranged == nullptr || parameterEntry(result, ranged->paramID).isValid()) continue;
        juce::ValueTree entry("PARAM");
        entry.setProperty("id", ranged->paramID, nullptr);
        entry.setProperty("value", ranged->convertFrom0to1(ranged->getDefaultValue()), nullptr);
        result.addChild(entry, -1, nullptr);
    }
    return result;
}
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new theta::forge::Processor();
}
