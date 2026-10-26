#pragma once
#include <tracktion_engine/tracktion_engine.h>
#include <memory>

namespace rhino
{
namespace te = tracktion::engine;

// The hidden plugin that plays an audio clip's Pitch envelope
// (dsp/ClipEnvelope.h): the clip's sound through a stretcher at speed one,
// shifted by the semitones its parameter says, which the session drives from
// an engine curve written from the envelope. It sits after the Clip Envelope
// on the clip's own plugin list, and is there only while the clip has a
// Pitch envelope.
//
// It is the engine's pitch shifter with two things put right. The engine's
// reports the frames it buffers and not the stretcher's own delay, so the
// graph compensated half of what it delays a clip by and every pitched hit
// came 150 ms late. This reports both, so delay compensation lands the clip
// where it was. And its delay is one fixed length at every sample rate,
// `latencySeconds`, because the engine reads a delayed plugin's automation
// that much early (PluginNode's automationAdjustmentTime), as if the
// parameter acted on what comes out rather than on what goes in: the session
// writes the Pitch curve that much early to match, which it can only do for a
// delay it knows before the device is ever prepared.
class ClipPitchDevice final : public te::Plugin
{
public:
    inline static const char* xmlTypeName = "rhino.clip.pitch.v1";
    explicit ClipPitchDevice(te::PluginCreationInfo);
    ~ClipPitchDevice() override;

    juce::String getName() const override { return "Clip Pitch"; }
    juce::String getPluginType() override { return xmlTypeName; }
    juce::String getVendor() override { return "Rhino"; }
    juce::String getSelectableDescription() override { return getName(); }
    BusLayout getBusses() const override { return BusLayout::singlePassThrough(); }
    // A mono clip stays mono; the stretcher works in stereo either way.
    int getNumOutputChannelsGivenInputs(int numInputChannels) override { return numInputChannels; }
    void getChannelNames(juce::StringArray*, juce::StringArray*) override {}
    // A clip's plugin list is checked as the master's is.
    bool canBeAddedToClip() override { return true; }
    bool canBeAddedToMaster() override { return true; }
    bool canBeMoved() override { return false; }
    void initialise(const te::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void reset() override;
    double getLatencySeconds() override;
    double getTailLength() const override;
    void applyToBuffer(const te::PluginRenderContext&) override;
    void restorePluginStateFromValueTree(const juce::ValueTree&) override;

    juce::CachedValue<float> semitonesValue;
    te::AutomatableParameter::Ptr semitones;

    // The stretcher it runs, the same one an unwarped clip's own Pitch knob
    // goes through.
    static constexpr auto stretchMode = te::TimeStretcher::signalsmithDefault;
    // How far behind its input its output is, at any rate: the stretcher's
    // own 150 ms and the 150 ms of frames it may ask for at once, with room
    // to spare, padded out to exactly this.
    static constexpr double latencySeconds = 0.32;

private:
    // Runs up to one prepared block of the buffer through the stretcher.
    void processChunk(juce::AudioBuffer<float>&, int start, int count, float shift);
    // Empties the stretcher and its queues and lays down the frames it
    // buffers, so its output is a fixed delay behind its input from the first.
    void restart(float shift);

    std::unique_ptr<te::TimeStretcher> stretcher;
    te::AudioFifo input {2, 16}, output {2, 16};
    juce::AudioBuffer<float> scratch;
    double sampleRate = 44100.0;
    int blockSize = 512, buffered = 0, latencySamples = 0;
};
}
