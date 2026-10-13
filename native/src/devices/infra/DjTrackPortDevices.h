#pragma once
#include "dj/DjTrackPort.h"
#include <tracktion_engine/tracktion_engine.h>
#include <array>
#include <atomic>
#include <vector>

namespace rhino
{
namespace te = tracktion::engine;

// The two hidden plugins that let a DJ deck be heard through a track of the
// song, so the track's own instrument and effects are what the deck sounds
// like and every edit to them is heard at once - no render in between. They
// are infrastructure, like the track's Utility channel strip: never offered
// in the browser, never shown in the rack. Every track carries one of each
// from the moment it is made, inside the step that makes it, so they never
// move another device's place in the chain and an undo never meets a chain
// it did not record. They do nothing - the block passes straight through -
// unless the booth has bound a deck's port to them, and even then only while
// the song's transport is stopped and nothing is being rendered: the song
// owns its tracks while it plays, and a merge or an export never hears a
// deck.
//
// The input sits at the very head of the chain, before the MIDI effects and
// the instrument; the output where the track's own devices end and its
// channel strip - the Utility, Volume & Pan, the meter - begins. Together they
// are the port's two ends on the song's side (DjTrackPort).

// Head of the chain. Bound, it adds the deck's audio to the block and writes
// the deck's notes into the chain's MIDI, so a MIDI track's effects and
// instrument play the deck's notes live and an audio track's effects work the
// deck's audio live. It remembers every note it played, and lets them all go
// the moment it stops playing the deck - unbound, the song started, a render,
// the port resynced after a stall - so no note it started is left sounding.
class DjTrackInjectorDevice final : public te::Plugin
{
public:
    inline static const char* xmlTypeName = "rhino.dj.inject.v1";
    explicit DjTrackInjectorDevice(te::PluginCreationInfo);

    // The booth points it at the slot's port, or null to unbind. Any thread;
    // the audio thread reads it once a block.
    void setPort(DjTrackPort* p) noexcept { port.store(p, std::memory_order_release); }
    DjTrackPort* currentPort() const noexcept { return port.load(std::memory_order_acquire); }

    juce::String getName() const override { return "DJ Input"; }
    juce::String getPluginType() override { return xmlTypeName; }
    juce::String getVendor() override { return "Rhino"; }
    juce::String getSelectableDescription() override { return getName(); }
    BusLayout getBusses() const override { return BusLayout::singlePassThrough(); }
    // As many channels as it is given, so a mono track stays mono and its
    // song mixer still spreads it across both sides; two at a chain's head
    // that has none, so a deck has somewhere to put its sound.
    int getNumOutputChannelsGivenInputs(int numInputChannels) override { return numInputChannels == 0 ? 2 : numInputChannels; }
    void getChannelNames(juce::StringArray*, juce::StringArray*) override {}
    bool canBeAddedToClip() override { return false; }
    bool canBeAddedToMaster() override { return false; }
    bool canBeMoved() override { return false; }
    void initialise(const te::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    double getLatencySeconds() override { return 0.0; }
    void applyToBuffer(const te::PluginRenderContext&) override;

private:
    // Note-offs for every note it played, at a time within the block, into a
    // chain's MIDI being built.
    void releaseDelivered(te::MidiMessageArray& output, double atSeconds);

    std::atomic<DjTrackPort*> port {nullptr};
    std::vector<float> left, right;
    std::vector<DjPortEvent> events;
    // The chain's MIDI is built into one of these and swapped in, as Rhino
    // Arp's is: the first swap hands one reserved array to the engine and the
    // steady one is used after, so both sides of every swap own room.
    te::MidiMessageArray firstOutput, steadyOutput;
    bool firstRender = true;
    // The notes it has started and not yet ended.
    std::array<bool, 128> delivered {};
    bool anyDelivered = false;
    double sampleRate = 48000.0;
};

// After the track's own devices, ahead of its channel strip. Bound, it hands
// the track's output to the deck's channel strip through the port and then
// clears the block, so the song's own output carries nothing of a track a
// deck is playing. Unbound, it leaves the block alone.
class DjTrackTapDevice final : public te::Plugin
{
public:
    inline static const char* xmlTypeName = "rhino.dj.tap.v1";
    explicit DjTrackTapDevice(te::PluginCreationInfo);

    void setPort(DjTrackPort* p) noexcept { port.store(p, std::memory_order_release); }
    DjTrackPort* currentPort() const noexcept { return port.load(std::memory_order_acquire); }

    juce::String getName() const override { return "DJ Output"; }
    juce::String getPluginType() override { return xmlTypeName; }
    juce::String getVendor() override { return "Rhino"; }
    juce::String getSelectableDescription() override { return getName(); }
    BusLayout getBusses() const override { return BusLayout::singlePassThrough(); }
    int getNumOutputChannelsGivenInputs(int numInputChannels) override { return numInputChannels; }
    void getChannelNames(juce::StringArray*, juce::StringArray*) override {}
    bool canBeAddedToClip() override { return false; }
    bool canBeAddedToMaster() override { return false; }
    bool canBeMoved() override { return false; }
    void initialise(const te::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    double getLatencySeconds() override { return 0.0; }
    void applyToBuffer(const te::PluginRenderContext&) override;

private:
    std::atomic<DjTrackPort*> port {nullptr};
    std::vector<float> left, right;
};
}
