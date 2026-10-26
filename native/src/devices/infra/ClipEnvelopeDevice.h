#pragma once
#include <tracktion_engine/tracktion_engine.h>

namespace rhino
{
namespace te = tracktion::engine;

// The hidden plugin that plays an audio clip's Gain and Pan envelopes
// (dsp/ClipEnvelope.h). It sits at the head of the clip's own plugin list, so
// it works on the clip's sound before any effect dropped on the clip, and the
// session drives its two parameters from engine curves written from the clip's
// envelopes. Infrastructure: never offered, never shown, and on a clip only
// while the clip has a Gain or Pan envelope to play.
//
// It plays the clip's own fades as well while the clip has a Pitch envelope.
// The engine fades a clip after the clip's plugins and allows for none of
// their delay, so behind Clip Pitch its fades would land a third of a second
// early and cut off the clip's end; ahead of the shifter, here, they meet the
// sound they were set on.
//
// Each block is ramped from where the last one ended to where the curves are
// now, so a step drawn in the envelope is a ramp of one engine sub-block
// rather than a click.
class ClipEnvelopeDevice final : public te::Plugin
{
public:
    inline static const char* xmlTypeName = "rhino.clip.envelope.v1";
    explicit ClipEnvelopeDevice(te::PluginCreationInfo);
    ~ClipEnvelopeDevice() override;

    juce::String getName() const override { return "Clip Envelope"; }
    juce::String getPluginType() override { return xmlTypeName; }
    juce::String getVendor() override { return "Rhino"; }
    juce::String getSelectableDescription() override { return getName(); }
    BusLayout getBusses() const override { return BusLayout::singlePassThrough(); }
    // As many channels as the clip has. A mono clip has nothing to balance,
    // just as the clip's own Pan knob leaves a mono clip alone.
    int getNumOutputChannelsGivenInputs(int numInputChannels) override { return numInputChannels; }
    void getChannelNames(juce::StringArray*, juce::StringArray*) override {}
    // A clip's plugin list is not a track's: the engine checks a plugin
    // bound for one as it checks one for the master list.
    bool canBeAddedToClip() override { return true; }
    bool canBeAddedToMaster() override { return true; }
    bool canBeMoved() override { return false; }
    void initialise(const te::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void reset() override;
    double getLatencySeconds() override { return 0.0; }
    void applyToBuffer(const te::PluginRenderContext&) override;
    void restorePluginStateFromValueTree(const juce::ValueTree&) override;

    juce::CachedValue<float> gainValue, panValue, fadeValue;
    // The Gain envelope's share of the clip's gain, 0 to 1, the Pan
    // envelope's balance, -1 to 1, and how far into its fades the clip is, 0
    // to 1, when the device plays them.
    te::AutomatableParameter::Ptr gain, pan, fade;

private:
    // Where the last block ended, so the next one ramps from there. Audio
    // thread only.
    float leftGain = 1.0f, rightGain = 1.0f, shareGain = 1.0f;
    bool primed = false;
};
}
