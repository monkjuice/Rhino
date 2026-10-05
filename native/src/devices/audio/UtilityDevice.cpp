#include "UtilityDevice.h"

namespace rhino
{
void UtilityDevice::prepare(double newRate, int)
{
    amplitude.reset(newRate, 0.005);
}

void UtilityDevice::clear()
{
    amplitude.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(gainDb.value()));
}

void UtilityDevice::process(RenderBlock& block)
{
    amplitude.setTargetValue(juce::Decibels::decibelsToGain(gainDb.value()));
    for (int i = 0; i < block.numSamples; ++i)
    {
        const auto gain = amplitude.getNextValue();
        for (int channel = 0; channel < block.numChannels; ++channel)
            block.channels[channel][i] *= gain;
    }
}
}
