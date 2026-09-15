#include "SessionInternal.h"
#include <algorithm>

// The mixer: per-track volume, pan, mute and solo, plus the master level.
//
// There is one mixer, not one per view. The session view draws it as a
// vertical strip under each track column and the arrangement draws it as a
// horizontal strip in each track header, but both read and write the same
// VolumeAndPanPlugin, so neither view can hold a stale copy of a level.
//
// Theta's UtilityDevice is a device in a chain, like Live's Utility. It is not
// the track fader, and the two are deliberately separate.

namespace theta
{

te::VolumeAndPanPlugin* Session::trackVolumePlugin(int track) const
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size())) return nullptr;
    return tracks[track]->getVolumePlugin();
}

// Every track needs a fader, including tracks in documents written before the
// mixer existed. The fader goes last so it sits after the track's devices.
void Session::ensureTrackMixers()
{
    for (auto* track : te::getAudioTracks(*edit))
    {
        if (track->getVolumePlugin() != nullptr)
            continue;
        auto plugin = edit->getPluginCache().createNewPlugin(te::VolumeAndPanPlugin::xmlTypeName, {});
        if (plugin == nullptr)
            continue;
        track->pluginList.insertPlugin(plugin, track->pluginList.size(), nullptr);
    }
}

Session::TrackMixer Session::trackMixer(int track) const
{
    TrackMixer mixer;
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size()))
        return mixer;
    mixer.muted = tracks[track]->isMuted(false);
    mixer.soloed = tracks[track]->isSolo(false);
    if (auto* volume = tracks[track]->getVolumePlugin())
    {
        mixer.volumeDb = volume->getVolumeDb();
        mixer.pan = volume->getPan();
    }
    return mixer;
}

juce::Result Session::setTrackVolumeDb(int track, float decibels)
{
    auto* volume = trackVolumePlugin(track);
    if (volume == nullptr)
        return juce::Result::fail("That track has no fader.");
    volume->setVolumeDb(juce::jlimit(minimumVolumeDb, maximumVolumeDb, decibels));
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::setTrackPan(int track, float pan)
{
    auto* volume = trackVolumePlugin(track);
    if (volume == nullptr)
        return juce::Result::fail("That track has no fader.");
    volume->setPan(juce::jlimit(-1.0f, 1.0f, pan));
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

// Gestures bracket a drag so the engine records one automation move and the
// undo history gets one entry rather than one per pixel.
void Session::beginTrackVolumeGesture(int track)
{
    if (auto* volume = trackVolumePlugin(track))
    {
        edit->getUndoManager().beginNewTransaction("Track volume");
        volume->volParam->parameterChangeGestureBegin();
    }
}

void Session::endTrackVolumeGesture(int track)
{
    if (auto* volume = trackVolumePlugin(track))
    {
        volume->volParam->parameterChangeGestureEnd();
        edit->getUndoManager().beginNewTransaction();
    }
}

void Session::beginTrackPanGesture(int track)
{
    if (auto* volume = trackVolumePlugin(track))
    {
        edit->getUndoManager().beginNewTransaction("Track pan");
        volume->panParam->parameterChangeGestureBegin();
    }
}

void Session::endTrackPanGesture(int track)
{
    if (auto* volume = trackVolumePlugin(track))
    {
        volume->panParam->parameterChangeGestureEnd();
        edit->getUndoManager().beginNewTransaction();
    }
}

void Session::setTrackMuted(int track, bool muted)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size())) return;
    if (tracks[track]->isMuted(false) == muted) return;
    toggleTrackMute(track);
}

void Session::setTrackSoloed(int track, bool soloed)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size())) return;
    if (tracks[track]->isSolo(false) == soloed) return;
    toggleTrackSolo(track);
}

float Session::masterVolumeDb() const
{
    if (auto master = edit->getMasterVolumePlugin())
        return master->getVolumeDb();
    return 0.0f;
}

void Session::setMasterVolumeDb(float decibels)
{
    if (auto master = edit->getMasterVolumePlugin())
    {
        master->setVolumeDb(juce::jlimit(minimumVolumeDb, maximumVolumeDb, decibels));
        markModified();
        sendSynchronousChangeMessage();
    }
}

float Session::masterPan() const
{
    if (auto master = edit->getMasterVolumePlugin())
        return master->getPan();
    return 0.0f;
}

void Session::setMasterPan(float pan)
{
    if (auto master = edit->getMasterVolumePlugin())
    {
        master->setPan(juce::jlimit(-1.0f, 1.0f, pan));
        markModified();
        sendSynchronousChangeMessage();
    }
}

void Session::beginMasterPanGesture()
{
    if (auto master = edit->getMasterVolumePlugin())
    {
        edit->getUndoManager().beginNewTransaction("Main pan");
        master->panParam->parameterChangeGestureBegin();
    }
}

void Session::endMasterPanGesture()
{
    if (auto master = edit->getMasterVolumePlugin())
    {
        master->panParam->parameterChangeGestureEnd();
        edit->getUndoManager().beginNewTransaction();
    }
}

void Session::beginMasterVolumeGesture()
{
    if (auto master = edit->getMasterVolumePlugin())
    {
        edit->getUndoManager().beginNewTransaction("Main volume");
        master->volParam->parameterChangeGestureBegin();
    }
}

void Session::endMasterVolumeGesture()
{
    if (auto master = edit->getMasterVolumePlugin())
    {
        master->volParam->parameterChangeGestureEnd();
        edit->getUndoManager().beginNewTransaction();
    }
}

}
