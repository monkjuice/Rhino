#pragma once
#include <tracktion_engine/tracktion_engine.h>
#include <vector>

namespace rhino
{
namespace te = tracktion::engine;

class RhinoSpaceDevice final : public te::Plugin
{
public:
    inline static const char* xmlTypeName = "rhino.space.v1";
    static const char* getPluginName() { return "Rhino Space"; }
    explicit RhinoSpaceDevice(te::PluginCreationInfo);
    ~RhinoSpaceDevice() override;
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
    juce::CachedValue<float> mix, size, smear, drive, width, outputDb;
    te::AutomatableParameter::Ptr mixParam, sizeParam, smearParam, driveParam, widthParam, outputParam;
    juce::Reverb reverb;
    std::vector<float> delayL, delayR, dryL, dryR;
    double sampleRate = 48000.0;
    int writeIndex = 0;
    float smoothedDelaySamples = 1.0f;
};
}
