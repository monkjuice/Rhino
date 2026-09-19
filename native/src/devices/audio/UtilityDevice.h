#pragma once
#include <tracktion_engine/tracktion_engine.h>

namespace rhino
{
namespace te = tracktion::engine;

// Stable device and parameter IDs are persisted by Tracktion's edit model.
class UtilityDevice final : public te::Plugin
{
public:
    inline static const char* xmlTypeName = "rhino.utility.v1";
    static const char* getPluginName() { return "Utility"; }
    explicit UtilityDevice(te::PluginCreationInfo);
    ~UtilityDevice() override;
    juce::String getName() const override { return getPluginName(); }
    juce::String getPluginType() override { return xmlTypeName; }
    juce::String getVendor() override { return "Rhino"; }
    juce::String getSelectableDescription() override { return getName(); }
    BusLayout getBusses() const override { return BusLayout::singlePassThrough(); }
    void initialise(const te::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void applyToBuffer(const te::PluginRenderContext&) override;
    void restorePluginStateFromValueTree(const juce::ValueTree&) override;
    te::AutomatableParameter& gain() { return *gainParameter; }

private:
    juce::CachedValue<float> gainDb;
    te::AutomatableParameter::Ptr gainParameter;
    juce::SmoothedValue<float> amplitude;
};
}
