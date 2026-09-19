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

juce::Result Session::addAudioTrack()
{
    const auto tracks = te::getAudioTracks(*edit);
    edit->getUndoManager().beginNewTransaction("Add audio track");
    auto newTrack = edit->insertNewAudioTrack(te::TrackInsertPoint::getEndOfTracks(*edit), nullptr, false);
    if (newTrack == nullptr)
        return juce::Result::fail("Could not create audio track.");
    newTrack->setName("Audio " + juce::String(tracks.size()));
    auto audioDevice = edit->getPluginCache().createNewPlugin(UtilityDevice::xmlTypeName, {});
    newTrack->pluginList.insertPlugin(audioDevice, 0, nullptr);
    refreshUtilityPointers();
    // The new track lands after the last one, so it joins a group only if that
    // group already ran to the bottom of the stack; reconciling says which.
    reconcileTrackGroups();
    ensureSceneSlots();
    ensureTrackMixers();
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

// Both utility pointers are positional - track 0's and track 1's - so any
// track that appears or disappears can leave them stale or dangling.
void Session::refreshUtilityPointers()
{
    const auto tracks = te::getAudioTracks(*edit);
    utility = nullptr;
    audioUtility = nullptr;
    for (int i = 0; i < tracks.size() && i < 2; ++i)
        for (auto plugin : tracks[i]->pluginList)
            if (auto* device = dynamic_cast<UtilityDevice*>(plugin))
            {
                (i == 0 ? utility : audioUtility) = device;
                break;
            }
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
    // The pattern editor follows whichever track is first, so removing the one
    // that currently holds its clip re-homes the clip rather than refusing.
    const auto removingEditedPatternTrack = patternClip != nullptr && patternClip->getClipTrack() == tracks[track];
    edit->getUndoManager().beginNewTransaction("Remove track");
    edit->deleteTrack(tracks[track]);
    if (removingEditedPatternTrack)
    {
        patternClip = nullptr;
        patternClipID = {};
        ensureEditablePatternClip();
    }
    refreshUtilityPointers();
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
    // Saturated enough to tell apart at a glance and to carry dark text, the
    // way a track colour does in the DAWs this borrows from. The muted set
    // these replace read as dirt on the panel rather than as a choice.
    static const std::vector<juce::Colour> palette {
        juce::Colour(0xff4aa3df), juce::Colour(0xff5ac8c8), juce::Colour(0xff58c07a), juce::Colour(0xff9bd14f),
        juce::Colour(0xffd8d24a), juce::Colour(0xffe8a33d), juce::Colour(0xffe8743d), juce::Colour(0xffe05a5a),
        juce::Colour(0xffe85f9b), juce::Colour(0xffb069d8), juce::Colour(0xff7d7ee0), juce::Colour(0xff8d9aa8),
        juce::Colour(0xff2f7fb8), juce::Colour(0xff3f9a68), juce::Colour(0xffb8862f), juce::Colour(0xffb04a6a)
    };
    return palette;
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
    refreshUtilityPointers();
    // Where a track lands decides which group it is in: carried into a group it
    // joins, carried out of one it leaves.
    reconcileTrackGroups();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

}
