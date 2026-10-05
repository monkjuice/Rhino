#pragma once
#include "sdk/NativeDevice.h"

namespace rhino
{
// The gain trim every track carries as its channel strip. Stable device and
// parameter ids are persisted by Tracktion's edit model.
class UtilityDevice final : public NativeAudioEffect
{
public:
    inline static const char* xmlTypeName = "rhino.utility.v1";
    explicit UtilityDevice(te::PluginCreationInfo info) : NativeAudioEffect(std::move(info), xmlTypeName) {}

    BusLayout getBusses() const override { return BusLayout::singlePassThrough(); }
    // A gain stage passes out exactly what it was handed. Saying so matters:
    // the engine's default answer is "however many channels I have names for",
    // which is two, so a mono clip or a mono input would arrive here as one
    // channel and leave declared as two with the second never written. The
    // track's Volume & Pan takes a stereo input and is what duplicates a mono
    // signal into both, and it only does that for a node that still admits to
    // being mono - so an honest answer here is what keeps a mono recording
    // from playing out of the left side alone.
    int getNumOutputChannelsGivenInputs(int numInputChannels) override { return numInputChannels; }
    te::AutomatableParameter& gain() { return gainDb.automatable(); }

private:
    void prepare(double newRate, int maximumBlockSize) override;
    void clear() override;
    void process(RenderBlock&) override;

    Param gainDb = param("gainDb", "Gain").range(-60.0f, 6.0f).defaultValue(0.0f).unit(ParamUnit::decibels);
    // Smoothed as a gain rather than in decibels, so a block costs one
    // conversion instead of one per sample.
    juce::SmoothedValue<float> amplitude;
};
}
