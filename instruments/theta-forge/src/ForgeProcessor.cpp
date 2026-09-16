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

juce::String asToggle(float value) { return value >= 0.5f ? "ON" : "OFF"; }

juce::String asGain(float value) { return juce::String(value, 2); }

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

std::unique_ptr<juce::RangedAudioParameter> parameter(const char* id, const char* name,
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
std::unique_ptr<juce::RangedAudioParameter> toggle(const char* id, const char* name, bool initial)
{
    return std::make_unique<juce::AudioParameterBool>(juce::ParameterID {id, 1}, name, initial);
}
}

juce::AudioProcessorValueTreeState::ParameterLayout Processor::parameterLayout()
{
    // Declaration order is Patch's field order. Processor::patch() initialises
    // that aggregate positionally, so the two lists move together.
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> result;
    result.push_back(toggle("oscAEnable", "Osc A Enable", true));
    result.push_back(parameter("oscAPosition", "Osc A Position", {0.0f, 1.0f}, 0.55f, asPercent));
    result.push_back(parameter("unison", "Unison", {1.0f, 8.0f, 1.0f}, 2.0f, asCount));
    result.push_back(parameter("detune", "Detune", {0.0f, 1.0f}, 0.18f, asPercent));
    result.push_back(toggle("oscBEnable", "Osc B Enable", true));
    result.push_back(parameter("oscBPosition", "Osc B Position", {0.0f, 1.0f}, 0.18f, asPercent));
    result.push_back(parameter("oscBLevel", "Osc B Level", {0.0f, 1.0f}, 0.25f, asPercent));
    result.push_back(parameter("oscBTune", "Osc B Tune", {-24.0f, 24.0f, 1.0f}, 7.0f, asSemitones));
    result.push_back(toggle("subEnable", "Sub Enable", true));
    result.push_back(parameter("subLevel", "Sub Level", {0.0f, 1.0f}, 0.12f, asPercent));
    result.push_back(toggle("noiseEnable", "Noise Enable", false));
    result.push_back(parameter("noiseLevel", "Noise Level", {0.0f, 1.0f}, 0.25f, asPercent));
    result.push_back(toggle("filterEnable", "Filter Enable", true));
    result.push_back(parameter("cutoff", "Cutoff", {30.0f, 18000.0f, 0.0f, 0.25f}, 7800.0f, asHertz));
    result.push_back(parameter("resonance", "Resonance", {0.0f, 1.0f}, 0.12f, asPercent));
    result.push_back(parameter("drive", "Drive", {0.0f, 1.0f}, 0.08f, asPercent));
    result.push_back(parameter("attack", "Attack", {0.001f, 4.0f, 0.0f, 0.35f}, 0.01f, asSeconds));
    result.push_back(parameter("decay", "Decay", {0.001f, 4.0f, 0.0f, 0.35f}, 0.24f, asSeconds));
    result.push_back(parameter("sustain", "Sustain", {0.0f, 1.0f}, 0.75f, asPercent));
    result.push_back(parameter("release", "Release", {0.001f, 8.0f, 0.0f, 0.35f}, 0.35f, asSeconds));
    result.push_back(parameter("lfoRate", "LFO Rate", {0.05f, 20.0f, 0.0f, 0.35f}, 0.5f, asRate));
    result.push_back(parameter("lfoCutoff", "LFO to Cutoff", {-1.0f, 1.0f}, 0.0f, asSignedPercent));
    result.push_back(parameter("lfoPosition", "LFO to Position", {-1.0f, 1.0f}, 0.0f, asSignedPercent));
    result.push_back(parameter("lfoPitch", "LFO to Pitch", {-12.0f, 12.0f}, 0.0f, asSemitones));
    result.push_back(parameter("polyphony", "Polyphony", {1.0f, 16.0f, 1.0f}, 8.0f, asCount));
    result.push_back(parameter("mono", "Mono", {0.0f, 1.0f, 1.0f}, 0.0f, asToggle));
    result.push_back(parameter("legato", "Legato", {0.0f, 1.0f, 1.0f}, 1.0f, asToggle));
    result.push_back(parameter("glide", "Glide", {0.0f, 2.0f, 0.0f, 0.35f}, 0.08f, asSeconds));
    result.push_back(parameter("output", "Output", {0.0f, 1.25f}, 0.75f, asGain));
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
}

bool Processor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const auto values = patch();
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
        float left, right; core.renderSample(values, left, right);
        buffer.addSample(0, i, left);
        if (buffer.getNumChannels() > 1) buffer.addSample(1, i, right);
    }
}

Patch Processor::patch() const
{
    // Positional, matching Patch's field order. Nothing is remapped on the way
    // through: the macros that used to bend these values were hardwired offsets
    // and come back as real assignable sources with the modulation matrix.
    const auto value = [this] (const char* id) { return state.getRawParameterValue(id)->load(); };
    return {value("oscAEnable"), value("oscAPosition"), value("unison"), value("detune"),
            value("oscBEnable"), value("oscBPosition"), value("oscBLevel"), value("oscBTune"),
            value("subEnable"), value("subLevel"),
            value("noiseEnable"), value("noiseLevel"),
            value("filterEnable"), value("cutoff"), value("resonance"), value("drive"),
            value("attack"), value("decay"), value("sustain"), value("release"),
            value("lfoRate"), value("lfoCutoff"), value("lfoPosition"), value("lfoPitch"),
            value("polyphony"), value("mono"), value("legato"), value("glide"),
            value("output")};
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
