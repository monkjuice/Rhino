#pragma once
#include <tracktion_engine/tracktion_engine.h>
#include <vector>

namespace rhino
{
namespace te = tracktion::engine;

class RhinoBloomDevice final : public te::Plugin
{
public:
    inline static const char* xmlTypeName = "rhino.bloom.v1";
    static const char* getPluginName() { return "Rhino Bloom"; }
    explicit RhinoBloomDevice(te::PluginCreationInfo);
    ~RhinoBloomDevice() override;
    juce::String getName() const override { return getPluginName(); }
    juce::String getPluginType() override { return xmlTypeName; }
    juce::String getVendor() override { return "Rhino"; }
    juce::String getSelectableDescription() override { return getName(); }
    BusLayout getBusses() const override { return BusLayout::singleStereoInOut(); }
    void initialise(const te::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void reset() override;
    void applyToBuffer(const te::PluginRenderContext&) override;
    void restorePluginStateFromValueTree(const juce::ValueTree&) override;

private:
    float readDelay(const std::vector<float>& delay, float offsetSamples) const;

    juce::CachedValue<float> bloom, chorus, clouds, plate, colour, outputDb;
    te::AutomatableParameter::Ptr bloomParam, chorusParam, cloudsParam, plateParam, colourParam, outputParam;
    juce::Reverb reverb;
    std::vector<float> delayL, delayR, dryL, dryR;
    double sampleRate = 48000.0;
    int writeIndex = 0;
    float chorusPhase = 0.0f;
};
}
