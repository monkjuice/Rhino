#include "SessionInternal.h"

// Routing one track's audio into another track's device.
//
// This is what Live calls a sidechain, and what the Vocoder's External mode
// uses to reach the synth that plays the voice. Only the vocoder asks for it
// today, but nothing here knows that: the API is a device slot and a source
// track, so a compressor that ducks to the kick needs no new model code.
//
// The engine owns the routing. A plugin records the source track's id on its
// own state, and the graph builder puts a send on that track -- after its own
// devices and mixer, and *before* its mute -- and a matching return in front
// of the plugin. Muting the source therefore silences it without taking the
// carrier away, which is exactly the step the workflow asks for: you do not
// want to hear the synth and the vocoder at once.
//
// Two things are Rhino's rather than the engine's. The source is named by
// track index, because that is what every other call here takes and the UI has
// no notion of an EditItemID. And changing it rebuilds the playback graph at
// once rather than at the next transport start, because the whole point is to
// sing into an armed track and hear the result with the transport standing
// still.

namespace rhino
{
namespace
{
// The engine's wires are channel-to-channel connections, and it guesses a
// sensible set from the plugin's input names. They are only rebuilt when the
// source changes, so a plugin that has never had one gets one here.
void rewire(te::Plugin& plugin)
{
    while (plugin.getNumWires() > 0)
        if (auto* wire = plugin.getWire(0))
            plugin.breakConnection(wire->sourceChannelIndex, wire->destChannelIndex);
        else
            break;
    plugin.guessSidechainRouting();
}
}

std::vector<Session::SidechainSource> Session::deviceSidechainSources(int track, int slot) const
{
    std::vector<SidechainSource> sources;
    auto* plugin = devicePlugin(track, slot);
    if (plugin == nullptr || !plugin->canSidechain())
        return sources;
    const auto tracks = te::getAudioTracks(*edit);
    for (int candidate = 0; candidate < tracks.size(); ++candidate)
    {
        // A track cannot be its own carrier: the graph would have to produce
        // the plugin's input from the plugin's output.
        if (candidate == track)
            continue;
        sources.push_back({candidate, trackName(candidate)});
    }
    return sources;
}

int Session::deviceSidechainSource(int track, int slot) const
{
    auto* plugin = devicePlugin(track, slot);
    if (plugin == nullptr)
        return -1;
    const auto sourceID = plugin->getSidechainSourceID();
    if (!sourceID.isValid())
        return -1;
    const auto tracks = te::getAudioTracks(*edit);
    for (int candidate = 0; candidate < tracks.size(); ++candidate)
        if (tracks[candidate]->itemID == sourceID)
            return candidate;
    // Removing a track clears the id off anything naming it, so this is only
    // reached for a track that went by some other route - and the answer is
    // the same either way: nothing is routed.
    return -1;
}

juce::Result Session::setDeviceSidechainSource(int track, int slot, int sourceTrack)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    auto* plugin = devicePlugin(track, slot);
    if (plugin == nullptr)
        return juce::Result::fail("Select a device first.");
    if (!plugin->canSidechain())
        return juce::Result::fail(plugin->getName() + " takes no audio from another track.");
    if (sourceTrack == track)
        return juce::Result::fail("A track cannot play itself. Choose another track.");

    const auto tracks = te::getAudioTracks(*edit);
    edit->getUndoManager().beginNewTransaction("Set audio source");
    if (!juce::isPositiveAndBelow(sourceTrack, tracks.size()))
    {
        plugin->setSidechainSourceID({});
    }
    else
    {
        plugin->setSidechainSourceID(tracks[sourceTrack]->itemID);
        rewire(*plugin);
    }
    edit->getUndoManager().beginNewTransaction();
    markModified();
    // Changing the id only marks the plugin as changed, which does not rebuild
    // the graph. Without this the routing takes hold at the next transport
    // start, and monitoring a live voice through the device would go on
    // hearing the old source - or nothing - until then.
    edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::clearDeviceSidechainSource(int track, int slot)
{
    return setDeviceSidechainSource(track, slot, -1);
}

// Every plugin in the edit, not only the ones on tracks: a device on the main
// output can name a source track too.
void Session::clearSidechainSourcesNaming(te::EditItemID sourceTrack)
{
    if (!sourceTrack.isValid())
        return;
    for (auto plugin : edit->getPluginCache().getPlugins())
        if (plugin != nullptr && plugin->getSidechainSourceID() == sourceTrack)
            plugin->setSidechainSourceID({});
}

}
