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
    ensureSceneSlots();
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::removeAudioTrack(int track)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (track <= 0 || !juce::isPositiveAndBelow(track, tracks.size()))
        return juce::Result::fail("Select an audio track to remove.");
    if (tracks.size() <= 2)
        return juce::Result::fail("Keep at least one audio track.");
    const auto removingEditedPatternTrack = patternClip != nullptr && patternClip->getClipTrack() == tracks[track];
    edit->getUndoManager().beginNewTransaction("Remove audio track");
    edit->deleteTrack(tracks[track]);
    if (removingEditedPatternTrack)
    {
        patternClip = nullptr;
        patternClipID = {};
        ensureEditablePatternClip();
    }
    if (track == 1)
        audioUtility = nullptr;
    const auto refreshed = te::getAudioTracks(*edit);
    if (audioUtility == nullptr && refreshed.size() > 1)
        for (auto plugin : refreshed[1]->pluginList)
            if (auto* device = dynamic_cast<UtilityDevice*>(plugin))
                audioUtility = device;
    refreshLoop();
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

}
