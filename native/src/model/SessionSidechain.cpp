#include "SessionInternal.h"

// Routing one track's audio into another track's device.
//
// This is what Live calls a sidechain: the Vocoder's carrier, Rhino Duck's
// Audio trigger, a compressor ducking to the kick. Nothing here knows which
// device asks. The API is a device slot and a source track, and any device the
// engine says can take a sidechain - one that declares more inputs than its
// main bus - is offered one, Rhino's own and an external plugin's alike. The
// rack draws the chooser for every such device (DeviceEditorPanelSidechain.cpp).
//
// The engine owns the routing. A plugin records the source track's id on its
// own state, and the graph builder puts a send on that track -- after its own
// devices and mixer, and *before* its mute -- and a matching return in front
// of the plugin. Muting the source therefore silences it without taking the
// signal away, which is exactly the step the workflow asks for: you do not
// want to hear the synth and the vocoder at once.
//
// Three things are Rhino's rather than the engine's. The source is named by
// track index, because that is what every other call here takes and the UI has
// no notion of an EditItemID. Changing it rebuilds the playback graph at once
// rather than at the next transport start, because the whole point is to sing
// into an armed track and hear the result with the transport standing still.
// And a source that already carries the device's own track is refused, since
// the engine builds a feedback loop without a word.

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

// Tracktion's compressor reads its sidechain input only while told to, and
// otherwise detects on its own track whatever arrives. Choosing a source tells
// it; None, or the source going, tells it to listen to its own track again.
void followSidechain(te::Plugin& plugin, bool routed)
{
    if (auto* compressor = dynamic_cast<te::CompressorPlugin*>(&plugin))
        if (compressor->useSidechainTrigger.get() != routed)
            compressor->useSidechainTrigger = routed;
}

// Which tracks already carry `track`'s sound: every track it is routed into
// (a group's bus), every return it sends to - a send is in the graph at -inf
// too - and every track with a device it is the sidechain of, then the same of
// each of those. Taking a sidechain from any of them would feed the track's
// own output back into it.
std::vector<bool> tracksCarrying(const juce::Array<te::AudioTrack*>& tracks, int track)
{
    const auto count = tracks.size();
    std::vector<bool> carrying(static_cast<size_t>(count), false);
    if (!juce::isPositiveAndBelow(track, count))
        return carrying;

    // Who each track feeds, built once rather than asked pair by pair.
    std::vector<std::vector<int>> feeds(static_cast<size_t>(count));
    std::vector<std::pair<int, int>> waysIn;   // a return's bus, and the track
    for (int to = 0; to < count; ++to)
        for (auto* plugin : tracks[to]->pluginList)
        {
            if (auto* wayIn = dynamic_cast<te::AuxReturnPlugin*>(plugin))
                waysIn.emplace_back(wayIn->busNumber.get(), to);
            const auto source = plugin->getSidechainSourceID();
            if (!source.isValid())
                continue;
            for (int from = 0; from < count; ++from)
                if (from != to && tracks[from]->itemID == source)
                    feeds[static_cast<size_t>(from)].push_back(to);
        }
    for (int from = 0; from < count; ++from)
    {
        auto& out = feeds[static_cast<size_t>(from)];
        if (auto* destination = tracks[from]->getOutput().getDestinationTrack())
            if (const auto to = tracks.indexOf(destination); to >= 0)
                out.push_back(to);
        for (auto* plugin : tracks[from]->pluginList)
            if (auto* send = dynamic_cast<te::AuxSendPlugin*>(plugin))
                for (const auto& [bus, to] : waysIn)
                    if (bus == send->busNumber.get() && to != from)
                        out.push_back(to);
    }

    std::vector<int> pending {track};
    carrying[static_cast<size_t>(track)] = true;
    while (!pending.empty())
    {
        const auto from = pending.back();
        pending.pop_back();
        for (const auto to : feeds[static_cast<size_t>(from)])
            if (!carrying[static_cast<size_t>(to)])
            {
                carrying[static_cast<size_t>(to)] = true;
                pending.push_back(to);
            }
    }
    return carrying;
}
}

bool Session::deviceTakesSidechain(int track, int slot) const
{
    auto* plugin = devicePlugin(track, slot);
    return plugin != nullptr && plugin->canSidechain();
}

std::vector<Session::SidechainSource> Session::deviceSidechainSources(int track, int slot) const
{
    std::vector<SidechainSource> sources;
    if (!deviceTakesSidechain(track, slot))
        return sources;
    const auto tracks = audioTracks();
    // The track itself is among them: a track cannot be its own source, since
    // the graph would have to produce the plugin's input from its output.
    const auto carrying = tracksCarrying(tracks, track);
    for (int candidate = 0; candidate < tracks.size(); ++candidate)
        if (candidate != track && !carrying[static_cast<size_t>(candidate)])
            sources.push_back({candidate, trackName(candidate)});
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
    const auto tracks = audioTracks();
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

    const auto tracks = audioTracks();
    const auto routed = juce::isPositiveAndBelow(sourceTrack, tracks.size());
    if (routed && tracksCarrying(tracks, track)[static_cast<size_t>(sourceTrack)])
        return juce::Result::fail(trackName(sourceTrack) + " already carries this track's sound, "
                                  "so it cannot feed back into it. Choose another track.");

    edit->getUndoManager().beginNewTransaction("Set audio source");
    if (!routed)
    {
        plugin->setSidechainSourceID({});
    }
    else
    {
        plugin->setSidechainSourceID(tracks[sourceTrack]->itemID);
        rewire(*plugin);
    }
    followSidechain(*plugin, routed);
    closeUndoStep();
    // The routing belongs to the device, on its own track: the source track
    // plays as it did.
    markModified(track, ModelChange::devices);
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
        {
            plugin->setSidechainSourceID({});
            followSidechain(*plugin, false);
        }
}

// A device on the main output is left alone: nothing a track carries is the
// main output's own sound.
void Session::clearSidechainLoops()
{
    const auto tracks = audioTracks();
    for (int track = 0; track < tracks.size(); ++track)
        for (auto* plugin : tracks[track]->pluginList)
        {
            const auto source = plugin->getSidechainSourceID();
            if (!source.isValid())
                continue;
            auto sourceTrack = -1;
            for (int candidate = 0; candidate < tracks.size(); ++candidate)
                if (tracks[candidate]->itemID == source)
                    sourceTrack = candidate;
            if (sourceTrack < 0 || !tracksCarrying(tracks, track)[static_cast<size_t>(sourceTrack)])
                continue;
            plugin->setSidechainSourceID({});
            followSidechain(*plugin, false);
            markModified(track, ModelChange::devices);
        }
}

}
