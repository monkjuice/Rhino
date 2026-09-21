#include "VocoderDevice.h"
#include <algorithm>
#include <cmath>

namespace rhino
{
namespace
{
juce::String percent(float value)
{
    return juce::String(juce::roundToInt(value * 100.0f)) + "%";
}

juce::String hertz(float value)
{
    return value >= 1000.0f ? juce::String(value / 1000.0f, value >= 10000.0f ? 1 : 2) + " kHz"
                            : juce::String(juce::roundToInt(value)) + " Hz";
}
}

VocoderDevice::VocoderDevice(te::PluginCreationInfo info) : Plugin(info)
{
    auto* undo = getUndoManager();
    bands.referTo(state, "bands", undo, 20.0f);
    formant.referTo(state, "formant", undo, 0.0f);
    bandwidth.referTo(state, "bw", undo, 1.0f);
    rangeLow.referTo(state, "low", undo, 100.0f);
    rangeHigh.referTo(state, "high", undo, 12000.0f);
    attack.referTo(state, "attack", undo, 2.0f);
    release.referTo(state, "release", undo, 30.0f);
    gate.referTo(state, "gate", undo, -60.0f);
    unvoiced.referTo(state, "unvoiced", undo, 0.25f);
    enhance.referTo(state, "enhance", undo, 0.5f);
    depth.referTo(state, "depth", undo, 1.0f);
    level.referTo(state, "level", undo, 0.0f);
    mix.referTo(state, "mix", undo, 1.0f);

    bandsParam     = addParam("bands", "Bands", {static_cast<float>(VocoderEngine::minBands),
                                                 static_cast<float>(VocoderEngine::maxBands)});
    formantParam   = addParam("formant", "Formant", {-24.0f, 24.0f});
    bandwidthParam = addParam("bw", "BW", {0.25f, 4.0f});
    rangeLowParam  = addParam("low", "Low", {20.0f, 2000.0f});
    rangeHighParam = addParam("high", "High", {1000.0f, 20000.0f});
    attackParam    = addParam("attack", "Attack", {0.1f, 100.0f});
    releaseParam   = addParam("release", "Release", {1.0f, 1000.0f});
    gateParam      = addParam("gate", "Gate", {-80.0f, 0.0f});
    unvoicedParam  = addParam("unvoiced", "Unvoiced", {0.0f, 1.0f});
    enhanceParam   = addParam("enhance", "Enhance", {0.0f, 1.0f});
    depthParam     = addParam("depth", "Depth", {0.0f, 1.0f});
    levelParam     = addParam("level", "Level", {-24.0f, 24.0f});
    mixParam       = addParam("mix", "Dry/Wet", {0.0f, 1.0f});

    bandsParam->attachToCurrentValue(bands);
    formantParam->attachToCurrentValue(formant);
    bandwidthParam->attachToCurrentValue(bandwidth);
    rangeLowParam->attachToCurrentValue(rangeLow);
    rangeHighParam->attachToCurrentValue(rangeHigh);
    attackParam->attachToCurrentValue(attack);
    releaseParam->attachToCurrentValue(release);
    gateParam->attachToCurrentValue(gate);
    unvoicedParam->attachToCurrentValue(unvoiced);
    enhanceParam->attachToCurrentValue(enhance);
    depthParam->attachToCurrentValue(depth);
    levelParam->attachToCurrentValue(level);
    mixParam->attachToCurrentValue(mix);

    bandsParam->valueToStringFunction = [] (float v) { return juce::String(juce::roundToInt(v)); };
    formantParam->valueToStringFunction = [] (float v)
    {
        return (v > 0.0f ? "+" : "") + juce::String(v, 1) + " st";
    };
    bandwidthParam->valueToStringFunction = [] (float v) { return juce::String(v, 2) + "x"; };
    rangeLowParam->valueToStringFunction = [] (float v) { return hertz(v); };
    rangeHighParam->valueToStringFunction = [] (float v) { return hertz(v); };
    attackParam->valueToStringFunction = [] (float v) { return juce::String(v, v < 10.0f ? 1 : 0) + " ms"; };
    releaseParam->valueToStringFunction = [] (float v) { return juce::String(juce::roundToInt(v)) + " ms"; };
    gateParam->valueToStringFunction = [] (float v)
    {
        return v <= -80.0f ? juce::String("Off") : juce::String(juce::roundToInt(v)) + " dB";
    };
    unvoicedParam->valueToStringFunction = [] (float v) { return percent(v); };
    enhanceParam->valueToStringFunction = [] (float v) { return percent(v); };
    depthParam->valueToStringFunction = [] (float v) { return percent(v); };
    levelParam->valueToStringFunction = [] (float v) { return juce::String(v, 1) + " dB"; };
    mixParam->valueToStringFunction = [] (float v) { return percent(v); };
}

VocoderDevice::~VocoderDevice()
{
    notifyListenersOfDeletion();
    for (auto* parameter : {&bandsParam, &formantParam, &bandwidthParam, &rangeLowParam, &rangeHighParam,
                            &attackParam, &releaseParam, &gateParam, &unvoicedParam,
                            &enhanceParam, &depthParam, &levelParam, &mixParam})
        (*parameter)->detachFromCurrentValue();
}

te::Plugin::BusLayout VocoderDevice::getBusses() const
{
    BusLayout layout;
    layout.inputs.push_back(te::ChannelConfiguration::stereo());
    layout.inputs.push_back(te::ChannelConfiguration::stereo());
    layout.outputs.push_back(te::ChannelConfiguration::stereo());
    return layout;
}

// The engine reads this rather than the bus layout when it decides whether a
// plugin can be sidechained at all, and the names are what its wire map is
// built from: four in, two out, so channels two and three are the carrier.
void VocoderDevice::getChannelNames(juce::StringArray* ins, juce::StringArray* outs)
{
    if (ins != nullptr)
    {
        ins->clear();
        ins->add(TRANS("Left"));
        ins->add(TRANS("Right"));
        ins->add(TRANS("Carrier L"));
        ins->add(TRANS("Carrier R"));
    }
    if (outs != nullptr)
    {
        outs->clear();
        outs->add(TRANS("Left"));
        outs->add(TRANS("Right"));
    }
}

void VocoderDevice::initialise(const te::PluginInitialisationInfo& info)
{
    engine.prepare(info.sampleRate > 0.0 ? info.sampleRate : 48000.0, 2,
                   std::max(64, info.blockSizeSamples));
}

void VocoderDevice::reset()
{
    engine.reset();
}

int VocoderDevice::bandCount() const
{
    return std::clamp(juce::roundToInt(bands.get()), VocoderEngine::minBands, VocoderEngine::maxBands);
}

float VocoderDevice::bandCentreHz(int index) const
{
    return VocoderEngine::bandCentreHz(index, bandCount(), rangeLow.get(), rangeHigh.get());
}

void VocoderDevice::applyToBuffer(const te::PluginRenderContext& context)
{
    if (context.destBuffer == nullptr || context.bufferNumSamples == 0)
        return;

    SCOPED_REALTIME_CHECK
    auto& buffer = *context.destBuffer;
    const auto channels = buffer.getNumChannels();
    if (channels == 0)
        return;

    VocoderEngine::Settings settings;
    settings.bands = juce::roundToInt(bandsParam->getCurrentValue());
    settings.formantSemitones = formantParam->getCurrentValue();
    settings.bandwidth = bandwidthParam->getCurrentValue();
    settings.lowHz = rangeLowParam->getCurrentValue();
    settings.highHz = rangeHighParam->getCurrentValue();
    settings.attackMs = attackParam->getCurrentValue();
    settings.releaseMs = releaseParam->getCurrentValue();
    // The bottom of the range means off rather than a gate at -80 dB, which
    // would still be a gate, and audibly so on a quiet take.
    settings.gateDb = gateParam->getCurrentValue() <= -80.0f ? -140.0f : gateParam->getCurrentValue();
    settings.unvoiced = unvoicedParam->getCurrentValue();
    settings.enhance = enhanceParam->getCurrentValue();
    settings.depth = depthParam->getCurrentValue();
    settings.outputGainDb = levelParam->getCurrentValue();
    settings.dryWet = mixParam->getCurrentValue();
    engine.setSettings(settings);

    // Channels nought and one are this track -- the voice. Two and three are
    // the carrier, and are only there when a source track has been chosen: the
    // graph hands the plugin its main bus alone otherwise.
    const auto main = std::min(channels, 2);
    float* modulator[2] {};
    for (int channel = 0; channel < main; ++channel)
        modulator[channel] = buffer.getWritePointer(channel, context.bufferStartSample);

    const float* carrier[2] {};
    const auto haveCarrier = channels >= 4;
    if (haveCarrier)
        for (int channel = 0; channel < 2; ++channel)
            carrier[channel] = buffer.getReadPointer(2 + channel, context.bufferStartSample);

    // Only the main bus is handed over, so the carrier channels come back
    // untouched -- which is what the graph expects, because it trims them off
    // the output rather than passing them downstream.
    engine.process(modulator, haveCarrier ? carrier : nullptr, main, context.bufferNumSamples);
}

void VocoderDevice::restorePluginStateFromValueTree(const juce::ValueTree& source)
{
    te::copyPropertiesToCachedValues(source, bands, formant, bandwidth, rangeLow, rangeHigh,
                                     attack, release, gate, unvoiced, enhance, depth, level, mix);
    for (auto* parameter : {&bandsParam, &formantParam, &bandwidthParam, &rangeLowParam, &rangeHighParam,
                            &attackParam, &releaseParam, &gateParam, &unvoicedParam,
                            &enhanceParam, &depthParam, &levelParam, &mixParam})
        (*parameter)->updateFromAttachedValue();
}
}
