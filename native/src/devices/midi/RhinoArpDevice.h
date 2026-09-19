#pragma once
#include <tracktion_engine/tracktion_engine.h>
#include <array>

namespace rhino
{
namespace te = tracktion::engine;

class RhinoArpDevice final : public te::Plugin
{
public:
    inline static const char* xmlTypeName = "rhino.arp.v1";
    static const char* getPluginName() { return "Rhino Arp"; }
    explicit RhinoArpDevice(te::PluginCreationInfo);
    ~RhinoArpDevice() override;
    juce::String getName() const override { return getPluginName(); }
    juce::String getPluginType() override { return xmlTypeName; }
    juce::String getVendor() override { return "Rhino"; }
    juce::String getSelectableDescription() override { return getName(); }
    BusLayout getBusses() const override { return {}; }
    void initialise(const te::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    double getLatencySeconds() override { return 0.0; }
    int getNumOutputChannelsGivenInputs(int) override { return 0; }
    void getChannelNames(juce::StringArray*, juce::StringArray*) override {}
    bool canBeAddedToClip() override { return false; }
    void reset() override;
    void midiPanic() override;
    void applyToBuffer(const te::PluginRenderContext&) override;
    void restorePluginStateFromValueTree(const juce::ValueTree&) override;

private:
    struct HeldNote
    {
        bool active = false;
        int channel = 1;
        float velocity = 0.8f;
        te::MPESourceID source = {};
    };

    struct PendingOff
    {
        bool active = false;
        double time = 0.0;
        int pitch = 60;
        int channel = 1;
        te::MPESourceID source = {};
    };

    double stepSeconds() const;
    int activeNoteCount() const;
    int noteAtOrdinal(int ordinal) const;
    void addPendingOff(double time, int pitch, int channel, te::MPESourceID source);

    juce::CachedValue<float> rateIndex, octaves, gatePercent;
    te::AutomatableParameter::Ptr rateParam, octavesParam, gateParam;
    std::array<HeldNote, 128> heldNotes;
    std::array<PendingOff, 128> pendingOffs;
    double nextTickSeconds = 0.0;
    int arpStep = 0;
};
}
