#pragma once
#include "VocoderEngine.h"
#include <tracktion_engine/tracktion_engine.h>

namespace rhino
{
namespace te = tracktion::engine;

// Rhino Vocoder: the voice on the track, played by another track's synth.
//
// The device is a thin shell.  Everything that decides how it sounds lives in
// VocoderEngine, in RhinoCore, where a test can push two synthesised signals
// through it and measure what comes back without an Edit, a graph or a render.
//
// The carrier arrives as a *sidechain*, which is the one thing about this
// device that is not ordinary.  Declaring four input channels rather than two
// is the whole of what it takes: the engine's graph builder sees a plugin with
// more inputs than its main bus, and when the plugin names a source track it
// taps that track after its own devices and mixer and sums it into the extra
// channels.  The tap sits ahead of the source track's mute, so muting the
// synth -- which is the point of the exercise, or you hear it twice -- leaves
// the carrier running.
//
// Live calls this the Vocoder's External mode, and it is the only carrier
// Rhino offers: Noise, Modulator and Pitch Tracking are all a way of making a
// carrier when you have not got one, and Rhino has a track full of synths.
// With no source chosen the voice passes through dry rather than the device
// going silent for a reason nothing on the face explains.
class VocoderDevice final : public te::Plugin
{
public:
    inline static const char* xmlTypeName = "rhino.vocoder.v1";
    static const char* getPluginName() { return "Rhino Vocoder"; }

    explicit VocoderDevice(te::PluginCreationInfo);
    ~VocoderDevice() override;

    juce::String getName() const override { return getPluginName(); }
    juce::String getPluginType() override { return xmlTypeName; }
    juce::String getVendor() override { return "Rhino"; }
    juce::String getSelectableDescription() override { return getName(); }

    // Two stereo inputs -- the voice and the carrier -- and one stereo output.
    // The second input bus is what makes the engine offer this plugin a
    // sidechain at all.
    BusLayout getBusses() const override;
    void getChannelNames(juce::StringArray* ins, juce::StringArray* outs) override;
    // The main bus only: the two carrier channels are routed in, not out, and
    // the graph trims them off the result.
    int getNumOutputChannelsGivenInputs(int) override { return 2; }
    bool canSidechain() override { return true; }
    bool noTail() override { return false; }
    double getTailLength() const override { return 0.5; }

    void initialise(const te::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void reset() override;
    void applyToBuffer(const te::PluginRenderContext&) override;
    void restorePluginStateFromValueTree(const juce::ValueTree&) override;

    // ---- what the face reads ----------------------------------------------
    VocoderEngine::Readout readout() const { return engine.readout(); }
    int bandCount() const;
    float bandCentreHz(int index) const;

private:
    juce::CachedValue<float> bands, formant, bandwidth, rangeLow, rangeHigh;
    juce::CachedValue<float> attack, release, gate, unvoiced, enhance, depth, level, mix;

    te::AutomatableParameter::Ptr bandsParam, formantParam, bandwidthParam, rangeLowParam, rangeHighParam;
    te::AutomatableParameter::Ptr attackParam, releaseParam, gateParam, unvoicedParam;
    te::AutomatableParameter::Ptr enhanceParam, depthParam, levelParam, mixParam;

    VocoderEngine engine;
};
}
