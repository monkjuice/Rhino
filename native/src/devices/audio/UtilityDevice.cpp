#include "UtilityDevice.h"

namespace rhino
{
UtilityDevice::UtilityDevice(te::PluginCreationInfo info) : Plugin(info)
{
    gainDb.referTo(state, "gainDb", getUndoManager(), 0.0f);
    gainParameter = addParam("gainDb", "Gain", {-60.0f, 6.0f});
    gainParameter->attachToCurrentValue(gainDb);
}

UtilityDevice::~UtilityDevice()
{
    notifyListenersOfDeletion();
    gainParameter->detachFromCurrentValue();
}

void UtilityDevice::initialise(const te::PluginInitialisationInfo& info)
{
    amplitude.reset(info.sampleRate, 0.005);
    amplitude.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(gainParameter->getCurrentValue()));
}

void UtilityDevice::applyToBuffer(const te::PluginRenderContext& context)
{
    if (context.destBuffer == nullptr || context.bufferNumSamples == 0)
        return;

    // Parameter reads are atomic. DSP owns the smoother; no UI, file access,
    // locks, allocations or ValueTree reads occur in this processing path.
    amplitude.setTargetValue(juce::Decibels::decibelsToGain(gainParameter->getCurrentValue()));
    auto* const* channels = context.destBuffer->getArrayOfWritePointers();
    for (int frame = context.bufferStartSample;
         frame < context.bufferStartSample + context.bufferNumSamples; ++frame)
    {
        const auto gain = amplitude.getNextValue();
        for (int channel = 0; channel < context.destBuffer->getNumChannels(); ++channel)
            channels[channel][frame] *= gain;
    }
}

void UtilityDevice::restorePluginStateFromValueTree(const juce::ValueTree& source)
{
    te::copyPropertiesToCachedValues(source, gainDb);
    gainParameter->updateFromAttachedValue();
}
}
