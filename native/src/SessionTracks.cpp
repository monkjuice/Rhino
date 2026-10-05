#include "SessionInternal.h"
#include <algorithm>
#include <set>

// Track creation and removal.

namespace rhino
{

int Session::trackCount() const
{
    return te::getAudioTracks(*edit).size();
}

juce::String Session::trackName(int track) const
{
    if (isMasterTrack(track)) return "Main";
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size())) return {};
    return tracks[track]->getName();
}

juce::String Session::trackTypeName(TrackType type)
{
    return type == TrackType::midi ? "MIDI" : "Audio";
}

// What the track said it was when it was made, and nothing else. Reading the
// chain instead - a track that runs an instrument is MIDI - meant an audio
// lane turned into a MIDI one the moment a synth landed on it, so the lane's
// kind was a consequence of the last drop rather than a promise to the person
// who created it.
Session::TrackType Session::trackType(int track) const
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size()))
        return TrackType::audio;
    return tracks[track]->state.getProperty(trackTypeID).toString() == "midi" ? TrackType::midi
                                                                              : TrackType::audio;
}

// MIDI until the person picks otherwise, because a track they will play is the
// one they are most often after; the add-track menu is what remembers.
Session::TrackType Session::lastAddedTrackType()
{
    if (isCommandLineTestMode())
        return TrackType::midi;
    juce::PropertiesFile properties(rhinoSettingsOptions());
    return properties.getValue("lastAddedTrackType", "midi") == "audio" ? TrackType::audio
                                                                        : TrackType::midi;
}

void Session::setLastAddedTrackType(TrackType type)
{
    if (isCommandLineTestMode())
        return;
    juce::PropertiesFile properties(rhinoSettingsOptions());
    properties.setValue("lastAddedTrackType", type == TrackType::audio ? "audio" : "midi");
    properties.saveIfNeeded();
}

te::AudioTrack* Session::appendTrack(TrackType type)
{
    auto newTrack = edit->insertNewAudioTrack(te::TrackInsertPoint::getEndOfTracks(*edit), nullptr, false);
    if (newTrack == nullptr)
        return nullptr;
    newTrack->setName(trackTypeName(type));
    newTrack->setColour(pickTrackColour());
    // Only MIDI is written down: audio is what a track with nothing to say is,
    // so an audio track needs no property and no document needs migrating.
    if (type == TrackType::midi)
        newTrack->state.setProperty(trackTypeID, "midi", &edit->getUndoManager());
    auto audioDevice = edit->getPluginCache().createNewPlugin(UtilityDevice::xmlTypeName, {});
    newTrack->pluginList.insertPlugin(audioDevice, 0, nullptr);
    return newTrack.get();
}

juce::Result Session::addTrack(TrackType type)
{
    const auto kind = trackTypeName(type);
    edit->getUndoManager().beginNewTransaction("Add " + kind.toLowerCase() + " track");
    if (appendTrack(type) == nullptr)
        return juce::Result::fail("Could not create " + kind.toLowerCase() + " track.");
    // The new track lands after the last one, below every group, so it joins
    // none of them; reconciling routes it to the main output with the rest.
    reconcileTrackGroups();
    ensureSceneSlots();
    ensureTrackMixers();
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::removeAudioTrack(int track)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (isMasterTrack(track))
        return juce::Result::fail("The main row cannot be removed.");
    if (!juce::isPositiveAndBelow(track, tracks.size()))
        return juce::Result::fail("Select a track to remove.");
    if (tracks.size() <= 1)
        return juce::Result::fail("Keep at least one track.");
    edit->getUndoManager().beginNewTransaction("Remove track");
    // Anything taking this track's audio as a sidechain loses its source. The
    // id is cleared inside the same transaction as the deletion, so undo puts
    // both back; left behind, it would route a device to a bus nothing sends
    // to, which is a device that has gone silent for no visible reason.
    clearSidechainSourcesNaming(tracks[track]->itemID);
    edit->deleteTrack(tracks[track]);
    // The note editor's clip may have gone with the track.
    repairPatternClip();
    reconcileTrackGroups();
    refreshLoop();
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

namespace
{
const juce::Identifier laneHeightID {"rhinoLaneHeight"};
}

// Zero is "not chosen yet" rather than a height of nothing, which is what lets
// an untouched project keep fitting its rows to the panel.
float Session::trackLaneHeight(int track) const
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size()))
        return 0.0f;
    return static_cast<float>(static_cast<double>(tracks[track]->state.getProperty(laneHeightID, 0.0)));
}

juce::Result Session::setTrackLaneHeight(int track, float height)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size()))
        return juce::Result::fail("Select a track to resize.");
    // Row heights are a view setting, so they are written straight to the track
    // state without an undo transaction: undo belongs to what the track plays.
    tracks[track]->state.setProperty(laneHeightID, static_cast<double>(std::max(0.0f, height)), nullptr);
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::setTrackName(int track, const juce::String& name)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (isMasterTrack(track))
        return juce::Result::fail("The main row keeps its name.");
    if (!juce::isPositiveAndBelow(track, tracks.size()))
        return juce::Result::fail("Select a track to rename.");
    const auto trimmed = name.trim();
    if (trimmed.isEmpty())
        return juce::Result::fail("A track needs a name.");
    edit->getUndoManager().beginNewTransaction("Rename track");
    tracks[track]->setName(trimmed);
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Colour Session::trackColour(int track) const
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size()))
        return {};
    return tracks[track]->getColour();
}

juce::Result Session::setTrackColour(int track, juce::Colour colour)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (isMasterTrack(track))
        return juce::Result::fail("The main row takes no colour.");
    if (!juce::isPositiveAndBelow(track, tracks.size()))
        return juce::Result::fail("Select a track to colour.");
    edit->getUndoManager().beginNewTransaction("Colour track");
    tracks[track]->setColour(colour);
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

const std::vector<juce::Colour>& Session::trackColourPalette()
{
    static const std::vector<juce::Colour> palette {
        juce::Colour(0xffff0000), juce::Colour(0xffd00000), juce::Colour(0xffdc2f02), juce::Colour(0xffff5400),
        juce::Colour(0xffe85d04), juce::Colour(0xff6f4e37), juce::Colour(0xff8b5e34), juce::Colour(0xffff8700),
        juce::Colour(0xffc19a6b), juce::Colour(0xfff48c06), juce::Colour(0xfffaa307), juce::Colour(0xffffba08),
        juce::Colour(0xffffbd00), juce::Colour(0xffffd300), juce::Colour(0xffdeff0a), juce::Colour(0xffa1ff0a),
        juce::Colour(0xffa7e85e), juce::Colour(0xff0b8f3a), juce::Colour(0xff2fbf71), juce::Colour(0xff0aff99),
        juce::Colour(0xff90fcf9), juce::Colour(0xff0aefff), juce::Colour(0xff63b4d1), juce::Colour(0xff147df5),
        juce::Colour(0xff7699d4), juce::Colour(0xff580aff), juce::Colour(0xff390099), juce::Colour(0xff9448bc),
        juce::Colour(0xffbe0aff), juce::Colour(0xff480355), juce::Colour(0xff4a1942), juce::Colour(0xff2e1c2b),
        juce::Colour(0xff893168), juce::Colour(0xff9e0059), juce::Colour(0xff370617), juce::Colour(0xffff0054),
        juce::Colour(0xffff5d8f), juce::Colour(0xffff8fab), juce::Colour(0xffffc2d1), juce::Colour(0xff6a040f),
        juce::Colour(0xff9d0208), juce::Colour(0xffeaeaea), juce::Colour(0xff050404)
    };
    return palette;
}

juce::Colour Session::pickTrackColour() const
{
    const auto& palette = trackColourPalette();
    std::vector<juce::Colour> unused = palette;
    for (auto* track : te::getAudioTracks(*edit))
    {
        const auto worn = track->getColour();
        unused.erase(std::remove(unused.begin(), unused.end(), worn), unused.end());
    }
    // Once every entry is on screen the palette starts again, which is what
    // keeps a stack of twenty tracks coloured rather than half coloured.
    const auto& choices = unused.empty() ? palette : unused;
    // A run of scenarios that renders and compares pixels cannot be asked to
    // agree with a dice roll, so under --self-test the choice is the first
    // unused entry. Tracks still come out distinct, and in the same order
    // every time.
    const auto index = isCommandLineTestMode()
                           ? 0
                           : juce::Random::getSystemRandom().nextInt(static_cast<int>(choices.size()));
    return choices[static_cast<size_t>(index)];
}

// The reorder on its own. The group calls move several tracks inside one
// transaction of their own, so opening one here would split theirs in half.
void Session::moveTrackInEdit(int track, int destination)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size()))
        return;
    destination = juce::jlimit(0, tracks.size() - 1, destination);
    if (destination == track)
        return;
    // An insert point names the track the moved one lands behind, so it is read
    // from the order with the moved track already lifted out of it.
    std::vector<te::Track*> remaining;
    for (int i = 0; i < tracks.size(); ++i)
        if (i != track)
            remaining.push_back(tracks[i]);
    auto* preceding = destination > 0 ? remaining[static_cast<size_t>(destination) - 1] : nullptr;
    edit->moveTrack(tracks[track], te::TrackInsertPoint(nullptr, preceding));
}

juce::Result Session::moveTrack(int track, int destination)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (isMasterTrack(track) || isMasterTrack(destination))
        return juce::Result::fail("The main row keeps its place.");
    if (!juce::isPositiveAndBelow(track, tracks.size()))
        return juce::Result::fail("Select a track to move.");
    destination = juce::jlimit(0, tracks.size() - 1, destination);
    if (destination == track)
        return juce::Result::ok();
    edit->getUndoManager().beginNewTransaction("Move track");
    moveTrackInEdit(track, destination);
    edit->getUndoManager().beginNewTransaction();
    // Where a track lands decides which group it is in: carried into a group it
    // joins, carried out of one it leaves.
    reconcileTrackGroups();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

}
