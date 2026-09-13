#include "ForgeProcessor.h"
#include "ForgeEditor.h"

namespace theta::forge
{
namespace
{
std::unique_ptr<juce::RangedAudioParameter> parameter(const char* id, const char* name, juce::NormalisableRange<float> range, float initial)
{
    // Explicit version hints make VST3 parameter IDs stable from the first
    // release, rather than relying on JUCE's legacy VST2-compatible mapping.
    return std::make_unique<juce::AudioParameterFloat>(juce::ParameterID {id, 1}, name, range, initial);
}
}

juce::AudioProcessorValueTreeState::ParameterLayout Processor::parameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> result;
    result.push_back(parameter("oscAPosition", "Osc A Position", {0.0f, 1.0f}, 0.55f));
    result.push_back(parameter("oscBPosition", "Osc B Position", {0.0f, 1.0f}, 0.18f));
    result.push_back(parameter("oscBLevel", "Osc B Level", {0.0f, 1.0f}, 0.25f));
    result.push_back(parameter("oscBTune", "Osc B Tune", {-24.0f, 24.0f, 1.0f}, 7.0f));
    result.push_back(parameter("subLevel", "Sub Level", {0.0f, 1.0f}, 0.12f));
    result.push_back(parameter("noiseLevel", "Noise Level", {0.0f, 1.0f}, 0.0f));
    result.push_back(parameter("unison", "Unison", {1.0f, 8.0f, 1.0f}, 2.0f));
    result.push_back(parameter("detune", "Detune", {0.0f, 1.0f}, 0.18f));
    result.push_back(parameter("cutoff", "Cutoff", {30.0f, 18000.0f, 0.0f, 0.25f}, 7800.0f));
    result.push_back(parameter("resonance", "Resonance", {0.0f, 1.0f}, 0.12f));
    result.push_back(parameter("attack", "Attack", {0.001f, 4.0f, 0.0f, 0.35f}, 0.01f));
    result.push_back(parameter("decay", "Decay", {0.001f, 4.0f, 0.0f, 0.35f}, 0.24f));
    result.push_back(parameter("sustain", "Sustain", {0.0f, 1.0f}, 0.75f));
    result.push_back(parameter("release", "Release", {0.001f, 8.0f, 0.0f, 0.35f}, 0.35f));
    result.push_back(parameter("filterEnvAmount", "Filter Env Amount", {-1.0f, 1.0f}, 0.25f));
    result.push_back(parameter("filterAttack", "Filter Attack", {0.001f, 4.0f, 0.0f, 0.35f}, 0.005f));
    result.push_back(parameter("filterDecay", "Filter Decay", {0.001f, 4.0f, 0.0f, 0.35f}, 0.3f));
    result.push_back(parameter("filterSustain", "Filter Sustain", {0.0f, 1.0f}, 0.35f));
    result.push_back(parameter("filterRelease", "Filter Release", {0.001f, 8.0f, 0.0f, 0.35f}, 0.3f));
    result.push_back(parameter("lfoRate", "LFO Rate", {0.05f, 20.0f, 0.0f, 0.35f}, 0.5f));
    result.push_back(parameter("lfoCutoff", "LFO to Cutoff", {-1.0f, 1.0f}, 0.0f));
    result.push_back(parameter("drive", "Drive", {0.0f, 1.0f}, 0.08f));
    result.push_back(parameter("output", "Output", {0.0f, 1.25f}, 0.75f));
    result.push_back(parameter("lfoPosition", "LFO to Position", {-1.0f, 1.0f}, 0.0f));
    result.push_back(parameter("lfoPitch", "LFO to Pitch", {-12.0f, 12.0f}, 0.0f));
    result.push_back(parameter("chorusMix", "Chorus Mix", {0.0f, 1.0f}, 0.0f));
    result.push_back(parameter("chorusRate", "Chorus Rate", {0.02f, 8.0f, 0.0f, 0.35f}, 0.35f));
    result.push_back(parameter("chorusDepth", "Chorus Depth", {0.0f, 1.0f}, 0.4f));
    result.push_back(parameter("delayMix", "Delay Mix", {0.0f, 1.0f}, 0.0f));
    result.push_back(parameter("delayTime", "Delay Time", {0.02f, 2.0f, 0.0f, 0.35f}, 0.375f));
    result.push_back(parameter("delayFeedback", "Delay Feedback", {0.0f, 0.92f}, 0.3f));
    result.push_back(parameter("polyphony", "Polyphony", {1.0f, 16.0f, 1.0f}, 8.0f));
    result.push_back(parameter("mono", "Mono", {0.0f, 1.0f, 1.0f}, 0.0f));
    result.push_back(parameter("legato", "Legato", {0.0f, 1.0f, 1.0f}, 1.0f));
    result.push_back(parameter("glide", "Glide", {0.0f, 2.0f, 0.0f, 0.35f}, 0.08f));
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
    const auto value = [this] (const char* id) { return state.getRawParameterValue(id)->load(); };
    return {value("oscAPosition"), value("oscBPosition"), value("oscBLevel"), value("oscBTune"),
            value("subLevel"), value("noiseLevel"), value("unison"), value("detune"), value("cutoff"),
            value("resonance"), value("attack"), value("decay"), value("sustain"), value("release"),
            value("filterEnvAmount"), value("filterAttack"), value("filterDecay"), value("filterSustain"),
            value("filterRelease"), value("lfoRate"), value("lfoCutoff"), value("drive"), value("output"),
            value("lfoPosition"), value("lfoPitch"), value("chorusMix"), value("chorusRate"),
            value("chorusDepth"), value("delayMix"), value("delayTime"), value("delayFeedback"),
            value("polyphony"), value("mono"), value("legato"), value("glide")};
}

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor(*this); }

void Processor::getStateInformation(juce::MemoryBlock& destination)
{
    if (const auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, destination);
}

void Processor::setStateInformation(const void* data, int size)
{
    if (const auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName(state.state.getType()))
            state.replaceState(juce::ValueTree::fromXml(*xml));
}
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new theta::forge::Processor();
}
