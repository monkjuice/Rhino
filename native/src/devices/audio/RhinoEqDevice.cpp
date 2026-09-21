#include "RhinoEqDevice.h"
#include <algorithm>
#include <cmath>

namespace rhino
{
namespace
{
const juce::Identifier analyserId ("analyser"), selectedId ("sel");

// What a fresh EQ is: eight bands already spread across the spectrum, with
// one flat bell switched on in the middle so there is something to take hold
// of. Nothing here colours the sound until it is moved.
struct BandDefault
{
    EqFilterType type;
    float frequency;
    float q;
    bool enabled;
};

constexpr BandDefault bandDefaults[RhinoEqDevice::bandCount]
{
    {EqFilterType::LowCut48,   80.0f,  0.71f, false},
    {EqFilterType::LowShelf,  120.0f,  0.71f, false},
    {EqFilterType::Bell,      250.0f,  1.00f, false},
    {EqFilterType::Bell,      750.0f,  1.00f, true},
    {EqFilterType::Bell,     2200.0f,  1.00f, false},
    {EqFilterType::Bell,     6000.0f,  1.00f, false},
    {EqFilterType::HighShelf, 10000.0f, 0.71f, false},
    {EqFilterType::HighCut48, 15000.0f, 0.71f, false}
};

juce::Identifier bandId(const char* suffix, int band)
{
    return juce::Identifier("b" + juce::String(band + 1) + suffix);
}

juce::String frequencyText(float hertz)
{
    if (hertz >= 10000.0f) return juce::String(hertz / 1000.0f, 1) + " kHz";
    if (hertz >= 1000.0f)  return juce::String(hertz / 1000.0f, 2) + " kHz";
    if (hertz >= 100.0f)   return juce::String(juce::roundToInt(hertz)) + " Hz";
    return juce::String(hertz, 1) + " Hz";
}

// Frequency and Q are read logarithmically by ear, so the parameter is skewed
// to match: the middle of the control is the geometric middle of the range,
// not the arithmetic one. Automation resolution follows the same curve, which
// is where it is wanted -- a lane that spends most of its resolution above
// 10 kHz is a lane that cannot sweep a low cut.
juce::NormalisableRange<float> frequencyRange()
{
    juce::NormalisableRange<float> range {eqMinimumFrequency, eqMaximumFrequency};
    range.setSkewForCentre(std::sqrt(eqMinimumFrequency * eqMaximumFrequency));
    return range;
}

juce::NormalisableRange<float> qRange()
{
    juce::NormalisableRange<float> range {eqMinimumQ, eqMaximumQ};
    range.setSkewForCentre(1.0f);
    return range;
}
}

RhinoEqDevice::RhinoEqDevice(te::PluginCreationInfo info) : Plugin(info)
{
    auto* undo = getUndoManager();
    for (int band = 0; band < bandCount; ++band)
    {
        const auto& fallback = bandDefaults[band];
        frequency[band].referTo(state, bandId("f", band), undo, fallback.frequency);
        gain[band].referTo(state, bandId("g", band), undo, 0.0f);
        q[band].referTo(state, bandId("q", band), undo, fallback.q);
        enabled[band].referTo(state, bandId("on", band), undo, fallback.enabled);
        type[band].referTo(state, bandId("t", band), undo, static_cast<int>(fallback.type));
    }
    outputGainDb.referTo(state, "outGain", undo, 0.0f);
    scalePercent.referTo(state, "scale", undo, 100.0f);
    analyser.referTo(state, analyserId, undo, static_cast<int>(EqEngine::AnalyserMode::Post));
    selected.referTo(state, selectedId, undo, 3);

    for (int band = 0; band < bandCount; ++band)
    {
        const auto number = juce::String(band + 1);
        frequencyParam[band] = addParam("b" + number + "f", number + " Freq", frequencyRange());
        gainParam[band] = addParam("b" + number + "g", number + " Gain",
                                   {-eqMaximumGainDb, eqMaximumGainDb});
        qParam[band] = addParam("b" + number + "q", number + " Q", qRange());

        frequencyParam[band]->attachToCurrentValue(frequency[band]);
        gainParam[band]->attachToCurrentValue(gain[band]);
        qParam[band]->attachToCurrentValue(q[band]);

        frequencyParam[band]->valueToStringFunction = [] (float v) { return frequencyText(v); };
        gainParam[band]->valueToStringFunction = [] (float v) { return juce::String(v, 1) + " dB"; };
        qParam[band]->valueToStringFunction = [] (float v) { return juce::String(v, 2); };
    }

    outputGainParam = addParam("outGain", "Output", {-12.0f, 12.0f});
    scaleParam = addParam("scale", "Scale", {0.0f, 400.0f});
    outputGainParam->attachToCurrentValue(outputGainDb);
    scaleParam->attachToCurrentValue(scalePercent);
    outputGainParam->valueToStringFunction = [] (float v) { return juce::String(v, 1) + " dB"; };
    scaleParam->valueToStringFunction = [] (float v) { return juce::String(juce::roundToInt(v)) + "%"; };
}

RhinoEqDevice::~RhinoEqDevice()
{
    notifyListenersOfDeletion();
    for (int band = 0; band < bandCount; ++band)
    {
        frequencyParam[band]->detachFromCurrentValue();
        gainParam[band]->detachFromCurrentValue();
        qParam[band]->detachFromCurrentValue();
    }
    outputGainParam->detachFromCurrentValue();
    scaleParam->detachFromCurrentValue();
}

void RhinoEqDevice::initialise(const te::PluginInitialisationInfo& info)
{
    engine.prepare(info.sampleRate > 0.0 ? info.sampleRate : 48000.0, 2,
                   std::max(64, info.blockSizeSamples));
}

void RhinoEqDevice::reset()
{
    engine.reset();
}

bool RhinoEqDevice::bandEnabled(int band) const
{
    return juce::isPositiveAndBelow(band, bandCount) && enabled[band].get();
}

void RhinoEqDevice::setBandEnabled(int band, bool on)
{
    if (juce::isPositiveAndBelow(band, bandCount))
        writeProperty(bandId("on", band), on);
}

EqFilterType RhinoEqDevice::bandType(int band) const
{
    if (!juce::isPositiveAndBelow(band, bandCount))
        return EqFilterType::Bell;
    return static_cast<EqFilterType>(juce::jlimit(0, eqFilterTypeCount - 1, type[band].get()));
}

void RhinoEqDevice::setBandType(int band, EqFilterType next)
{
    if (juce::isPositiveAndBelow(band, bandCount))
        writeProperty(bandId("t", band), static_cast<int>(next));
}

EqEngine::AnalyserMode RhinoEqDevice::analyserMode() const
{
    return static_cast<EqEngine::AnalyserMode>(
        juce::jlimit(0, EqEngine::analyserModeCount - 1, analyser.get()));
}

void RhinoEqDevice::setAnalyserMode(EqEngine::AnalyserMode mode)
{
    writeProperty(analyserId, static_cast<int>(mode));
}

int RhinoEqDevice::selectedBand() const
{
    return juce::jlimit(0, bandCount - 1, selected.get());
}

void RhinoEqDevice::setSelectedBand(int band)
{
    writeProperty(selectedId, juce::jlimit(0, bandCount - 1, band));
}

void RhinoEqDevice::writeProperty(const juce::Identifier& id, const juce::var& value)
{
    state.setProperty(id, value, getUndoManager());
}

EqEngine::Settings RhinoEqDevice::currentSettings() const
{
    EqEngine::Settings settings;
    for (int band = 0; band < bandCount; ++band)
    {
        auto& target = settings.band[static_cast<size_t>(band)];
        target.enabled = enabled[band].get();
        target.type = bandType(band);
        target.frequency = frequencyParam[band]->getCurrentValue();
        target.gainDb = gainParam[band]->getCurrentValue();
        target.q = qParam[band]->getCurrentValue();
    }
    settings.outputGainDb = outputGainParam->getCurrentValue();
    settings.scalePercent = scaleParam->getCurrentValue();
    settings.analyser = analyserMode();
    return settings;
}

void RhinoEqDevice::applyToBuffer(const te::PluginRenderContext& context)
{
    if (context.destBuffer == nullptr || context.bufferNumSamples == 0)
        return;

    SCOPED_REALTIME_CHECK
    juce::ScopedNoDenormals noDenormals;
    auto& buffer = *context.destBuffer;
    const auto channels = buffer.getNumChannels();
    if (channels == 0) return;

    engine.setSettings(currentSettings());

    float* channelData[2] {};
    const auto used = std::min(channels, 2);
    for (int channel = 0; channel < used; ++channel)
        channelData[channel] = buffer.getWritePointer(channel, context.bufferStartSample);
    engine.process(channelData, used, context.bufferNumSamples);
}

void RhinoEqDevice::restorePluginStateFromValueTree(const juce::ValueTree& source)
{
    for (int band = 0; band < bandCount; ++band)
        te::copyPropertiesToCachedValues(source, frequency[band], gain[band], q[band],
                                         enabled[band], type[band]);
    te::copyPropertiesToCachedValues(source, outputGainDb, scalePercent, analyser, selected);

    for (int band = 0; band < bandCount; ++band)
    {
        frequencyParam[band]->updateFromAttachedValue();
        gainParam[band]->updateFromAttachedValue();
        qParam[band]->updateFromAttachedValue();
    }
    outputGainParam->updateFromAttachedValue();
    scaleParam->updateFromAttachedValue();
}
}
