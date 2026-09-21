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
    // A gain stage passes out exactly what it was handed. Saying so matters:
    // the engine's default answer is "however many channels I have names for",
    // which is two, so a mono clip or a mono input would arrive here as one
    // channel and leave declared as two with the second never written. The
    // track's Volume & Pan takes a stereo input and is what duplicates a mono
    // signal into both, and it only does that for a node that still admits to
    // being mono - so an honest answer here is what keeps a mono recording
    // from playing out of the left side alone.
    int getNumOutputChannelsGivenInputs(int numInputChannels) override { return numInputChannels; }
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
