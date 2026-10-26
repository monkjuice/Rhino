#include "DuckProcessor.h"
#include "DuckEditor.h"

namespace rhino::duck
{
namespace
{
constexpr const char* curvesNode = "CURVES";
constexpr const char* curveNode = "CURVE";
constexpr const char* uiScaleProperty = "uiScale";
}

Processor::Processor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "RHINO_DUCK", parameterLayout())
{
    for (auto& slope : slopes) slope.store(0.0f, std::memory_order_relaxed);
    for (auto& shift : shifts) shift.store(0.0f, std::memory_order_relaxed);
    mixValue = state.getRawParameterValue(id::mix);
    curveValue = state.getRawParameterValue(id::curve);
    lengthValue = state.getRawParameterValue(id::length);
    triggerValue = state.getRawParameterValue(id::trigger);
    loopValue = state.getRawParameterValue(id::loop);
    endValue = state.getRawParameterValue(id::end);
    bandValue = state.getRawParameterValue(id::band);
    cutoffValue = state.getRawParameterValue(id::cutoff);
    bandSoloValue = state.getRawParameterValue(id::bandSolo);
    antiClickValue = state.getRawParameterValue(id::antiClick);
    midiChannelValue = state.getRawParameterValue(id::midiChannel);
    midiNoteValue = state.getRawParameterValue(id::midiNote);
    midiFrames.reserve(512);
}

Processor::~Processor()
{
    cancelPendingUpdate();
}

Settings Processor::settingsNow() const noexcept
{
    const auto read = [] (const std::atomic<float>* value) { return value != nullptr ? value->load(std::memory_order_relaxed) : 0.0f; };
    Settings settings;
    settings.mix = read(mixValue);
    settings.curve = juce::roundToInt(read(curveValue));
    settings.adjust = curveAdjust(settings.curve);
    settings.length = juce::roundToInt(read(lengthValue));
    settings.trigger = juce::roundToInt(read(triggerValue));
    settings.loop = read(loopValue) > 0.5f;
    settings.endMarker = read(endValue);
    settings.band = read(bandValue) > 0.5f;
    settings.cutoffHz = read(cutoffValue);
    settings.bandSolo = read(bandSoloValue) > 0.5f;
    settings.antiClick = read(antiClickValue) > 0.5f;
    return settings;
}

void Processor::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock)
{
    preparedRate = sampleRate > 1.0 ? sampleRate : 48000.0;
    duck.prepare(preparedRate, maximumExpectedSamplesPerBlock);
    const auto settings = settingsNow();
    setLatencySamples(rhino::duck::latencySamples(static_cast<Trigger>(std::clamp(settings.trigger, 0, 2)), settings.antiClick, preparedRate));
}

// A mono or stereo pair in and the same out; the sidechain mono, stereo or
// absent.
bool Processor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto mainIn = layouts.getMainInputChannelSet();
    const auto mainOut = layouts.getMainOutputChannelSet();
    if (mainIn != mainOut) return false;
    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo()) return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto side = layouts.inputBuses[1];
        if (!side.isDisabled() && side != juce::AudioChannelSet::mono() && side != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const auto numSamples = buffer.getNumSamples();
    if (numSamples <= 0 || buffer.getNumChannels() == 0) return;
    const auto settings = settingsNow();

    HostClock host;
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
        {
            host.playing = position->getIsPlaying();
            if (const auto bpm = position->getBpm()) host.bpm = *bpm;
            if (const auto ppq = position->getPpqPosition()) host.ppq = *ppq;
        }

    // The notes that trigger, by frame. A note is kept only when it is on
    // the channel and the key the menu asks for, or the menu asks for all.
    midiFrames.clear();
    const auto channelWanted = juce::roundToInt(midiChannelValue->load(std::memory_order_relaxed));
    const auto noteWanted = juce::roundToInt(midiNoteValue->load(std::memory_order_relaxed)) - 1;
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (!message.isNoteOn()) continue;
        if (channelWanted != 0 && message.getChannel() != channelWanted) continue;
        if (noteWanted >= 0 && message.getNoteNumber() != noteWanted) continue;
        if (midiFrames.size() < midiFrames.capacity()) midiFrames.push_back(metadata.samplePosition);
    }

    auto main = getBusBuffer(buffer, false, 0);
    float* mainChannels[Engine::maxChannels] {};
    const auto mainCount = std::min(Engine::maxChannels, main.getNumChannels());
    for (int channel = 0; channel < mainCount; ++channel) mainChannels[channel] = main.getWritePointer(channel);

    const float* sideChannels[Engine::maxChannels] {};
    auto sideCount = 0;
    if (getBusCount(true) > 1 && getBus(true, 1)->isEnabled())
    {
        auto side = getBusBuffer(buffer, true, 1);
        sideCount = std::min(Engine::maxChannels, side.getNumChannels());
        for (int channel = 0; channel < sideCount; ++channel) sideChannels[channel] = side.getReadPointer(channel);
    }

    duck.process(mainChannels, mainCount, sideChannels, sideCount, numSamples, settings, host,
                 midiFrames.data(), static_cast<int>(midiFrames.size()));

    // The host learns a new latency on the message thread, not here.
    const auto wanted = rhino::duck::latencySamples(static_cast<Trigger>(std::clamp(settings.trigger, 0, 2)), settings.antiClick, preparedRate);
    if (wanted != getLatencySamples())
    {
        wantedLatency.store(wanted, std::memory_order_relaxed);
        triggerAsyncUpdate();
    }
}

void Processor::handleAsyncUpdate()
{
    setLatencySamples(wantedLatency.load(std::memory_order_relaxed));
}

juce::AudioProcessorEditor* Processor::createEditor()
{
    return new Editor(*this);
}

// ---- the curves' own memory ---------------------------------------------------

CurveAdjust Processor::curveAdjust(int curve) const noexcept
{
    const auto index = static_cast<size_t>(clampCurve(curve));
    return { slopes[index].load(std::memory_order_relaxed), shifts[index].load(std::memory_order_relaxed) };
}

void Processor::setCurveSlope(int curve, float slope)
{
    slopes[static_cast<size_t>(clampCurve(curve))].store(std::clamp(slope, -1.0f, 1.0f), std::memory_order_relaxed);
    revision.fetch_add(1, std::memory_order_relaxed);
}

void Processor::setCurveShift(int curve, float shift)
{
    shifts[static_cast<size_t>(clampCurve(curve))].store(std::clamp(shift, -0.5f, 0.5f), std::memory_order_relaxed);
    revision.fetch_add(1, std::memory_order_relaxed);
}

void Processor::resetCurve(int curve)
{
    const auto index = static_cast<size_t>(clampCurve(curve));
    slopes[index].store(0.0f, std::memory_order_relaxed);
    shifts[index].store(0.0f, std::memory_order_relaxed);
    revision.fetch_add(1, std::memory_order_relaxed);
}

void Processor::resetAllCurves()
{
    for (int curve = 0; curve < curveCount; ++curve)
    {
        slopes[static_cast<size_t>(curve)].store(0.0f, std::memory_order_relaxed);
        shifts[static_cast<size_t>(curve)].store(0.0f, std::memory_order_relaxed);
    }
    revision.fetch_add(1, std::memory_order_relaxed);
}

void Processor::writeCurves(juce::ValueTree& tree) const
{
    tree.removeChild(tree.getChildWithName(curvesNode), nullptr);
    juce::ValueTree all(curvesNode);
    for (int curve = 0; curve < curveCount; ++curve)
    {
        const auto adjust = curveAdjust(curve);
        if (adjust.slope == 0.0f && adjust.shift == 0.0f) continue;
        juce::ValueTree one(curveNode);
        one.setProperty("index", curve, nullptr);
        one.setProperty("slope", adjust.slope, nullptr);
        one.setProperty("shift", adjust.shift, nullptr);
        all.appendChild(one, nullptr);
    }
    tree.appendChild(all, nullptr);
}

void Processor::readCurves(const juce::ValueTree& tree)
{
    for (auto& slope : slopes) slope.store(0.0f, std::memory_order_relaxed);
    for (auto& shift : shifts) shift.store(0.0f, std::memory_order_relaxed);
    const auto all = tree.getChildWithName(curvesNode);
    for (const auto& one : all)
    {
        if (!one.hasType(curveNode)) continue;
        const auto curve = static_cast<int>(one.getProperty("index", -1));
        if (curve < 0 || curve >= curveCount) continue;
        slopes[static_cast<size_t>(curve)].store(std::clamp(static_cast<float>(one.getProperty("slope", 0.0f)), -1.0f, 1.0f),
                                                 std::memory_order_relaxed);
        shifts[static_cast<size_t>(curve)].store(std::clamp(static_cast<float>(one.getProperty("shift", 0.0f)), -0.5f, 0.5f),
                                                 std::memory_order_relaxed);
    }
    revision.fetch_add(1, std::memory_order_relaxed);
}

// ---- state -------------------------------------------------------------------

void Processor::getStateInformation(juce::MemoryBlock& destination)
{
    auto tree = state.copyState();
    writeCurves(tree);
    if (const auto xml = tree.createXml()) copyXmlToBinary(*xml, destination);
}

void Processor::setStateInformation(const void* data, int size)
{
    const auto xml = getXmlFromBinary(data, size);
    if (xml == nullptr || !xml->hasTagName(state.state.getType())) return;
    const auto tree = juce::ValueTree::fromXml(*xml);
    readCurves(tree);
    state.replaceState(tree);
}

float Processor::uiScale() const
{
    return std::clamp(static_cast<float>(state.state.getProperty(uiScaleProperty, 1.0f)), 0.5f, 3.0f);
}

void Processor::setUiScale(float scale)
{
    state.state.setProperty(uiScaleProperty, std::clamp(scale, 0.5f, 3.0f), nullptr);
}
}

// The plugin's entry point.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new rhino::duck::Processor();
}
