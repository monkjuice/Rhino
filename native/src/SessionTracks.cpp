#include "SessionInternal.h"
#include <algorithm>
#include <set>

// Track creation and removal.

namespace theta
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
    refreshLoop();
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}


}
