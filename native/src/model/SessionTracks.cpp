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

te::AudioTrack* Session::appendTrack(TrackType type, int after)
{
    const auto tracks = te::getAudioTracks(*edit);
    const auto at = juce::isPositiveAndBelow(after, tracks.size()) ? te::TrackInsertPoint(nullptr, tracks[after])
                                                                  : te::TrackInsertPoint::getEndOfTracks(*edit);
    auto newTrack = edit->insertNewAudioTrack(at, nullptr, false);
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

juce::Result Session::addTrack(TrackType type, int below, int* added)
{
    const auto kind = trackTypeName(type);
    auto after = juce::isPositiveAndBelow(below, trackCount()) ? below : -1;
    auto joining = 0;
    const auto groupId = trackGroupBusId(after) != 0 ? trackGroupBusId(after) : trackGroupId(after);
    if (const auto group = trackGroup(groupId))
    {
        // Under a folded group means under all of it: inside, the track would
        // be made and never seen. Under an open one it joins, explicitly,
        // because under the last member is not between two of them and
        // reconciling alone would leave it outside.
        if (group->collapsed)
            after = std::max(group->busTrack, group->lastTrack());
        else
            joining = group->id;
    }
    edit->getUndoManager().beginNewTransaction("Add " + kind.toLowerCase() + " track");
    auto* track = appendTrack(type, after);
    if (track == nullptr)
        return juce::Result::fail("Could not create " + kind.toLowerCase() + " track.");
    if (joining != 0)
        track->state.setProperty(trackGroupMemberID, joining, &edit->getUndoManager());
    // Reconciling routes the new track into the bus it joined, or to the main
    // output with the rest.
    reconcileTrackGroups();
    ensureSceneSlots();
    ensureTrackMixers();
    edit->getUndoManager().beginNewTransaction();
    const auto index = after < 0 ? trackCount() - 1 : after + 1;
    if (added != nullptr)
        *added = index;
    // An empty track below every other changes nothing any deck plays, and
    // moves no lane: the main row's index moves on one, but the mirror finds
    // a lane's curve by its device and parameter, not by the index. One
    // landing higher up moves every lane under it and may have joined a
    // group, which is what moving a track counts as.
    markModified(-1, index == trackCount() - 1 && joining == 0 ? ModelChange::none : ModelChange::unknown);
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
    // A bus lets its members go first, while it is still there to be routed
    // away from - see dissolveGroupInEdit for why the order matters to undo.
    if (const auto groupId = trackGroupBusId(track); groupId != 0)
        dissolveGroupInEdit(groupId);
    edit->deleteTrack(tracks[track]);
    // The note editor's clip may have gone with the track.
    repairPatternClip();
    reconcileTrackGroups();
    refreshLoop();
    edit->getUndoManager().beginNewTransaction();
    // A deck's own track, a group's member or bus, or a sidechain's source
    // may have gone, and the lanes below move up: every deck is behind.
    markModified(-1, ModelChange::unknown);
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

namespace
{
const juce::Identifier laneHeightID {"rhinoLaneHeight"};
const juce::Identifier trackFoldedID {"rhinoTrackFolded"};
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
    // A height chosen for a folded row is a height to see it at.
    tracks[track]->state.removeProperty(trackFoldedID, nullptr);
    markModified(track, ModelChange::none);
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

bool Session::isTrackFolded(int track) const
{
    const auto tracks = te::getAudioTracks(*edit);
    return juce::isPositiveAndBelow(track, tracks.size()) && static_cast<bool>(tracks[track]->state.getProperty(trackFoldedID, false));
}

juce::Result Session::setTrackFolded(int track, bool folded)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size()))
        return juce::Result::fail("Select a track to fold.");
    // Written without undo, as the height is; an open row carries nothing.
    if (folded)
        tracks[track]->state.setProperty(trackFoldedID, true, nullptr);
    else
        tracks[track]->state.removeProperty(trackFoldedID, nullptr);
    markModified(track, ModelChange::none);
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
    markModified(track, ModelChange::none);
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
    markModified(track, ModelChange::none);
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
    // Where a track lands decides which group it is in: carried into a group it
    // joins, carried out of one it leaves. Inside the move's own transaction,
    // so undoing the move undoes that too.
    reconcileTrackGroups();
    edit->getUndoManager().beginNewTransaction();
    // A group may have gained or lost a member, which its deck renders; no
    // kind says that, so every deck is counted behind.
    markModified(-1, ModelChange::unknown);
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

}
