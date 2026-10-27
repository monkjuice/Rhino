#include "SessionInternal.h"
#include <algorithm>

// The mixer: per-track volume, pan, mute and solo, plus the master level. A
// track's volume and pan can be automated, as a device knob can.
//
// There is one mixer: each track's own VolumeAndPanPlugin. The arrangement
// draws it in each track header and reads and writes that plugin, so it cannot
// hold a stale copy of a level. A DJ deck takes its track from where the
// track's own devices end, ahead of the channel strip, so the song's trim,
// fader and pan are the song's and the deck's are the booth's.
//
// Rhino's UtilityDevice is a device in a chain, like Live's Utility. It is not
// the track fader, and the two are deliberately separate.

namespace rhino
{

te::VolumeAndPanPlugin* Session::trackVolumePlugin(int track) const
{
    const auto tracks = audioTracks();
    if (!juce::isPositiveAndBelow(track, tracks.size())) return nullptr;
    return tracks[track]->getVolumePlugin();
}

// Every track needs a fader, including tracks in documents written before the
// mixer existed. The fader goes last so it sits after the track's devices.
// The meter goes straight behind the fader, so the card shows what the track
// sends to the mix, its level and pan included (takeOutputLevels). A starter
// track already has the engine's own meter there.
void Session::ensureTrackMixers()
{
    for (auto* track : audioTracks())
    {
        // Every track carries the DJ booth's two hidden ends from the start,
        // made with it here as the mixer is.
        ensureDjPortDevices(*edit, *track);
        if (track->getVolumePlugin() == nullptr)
            if (auto plugin = edit->getPluginCache().createNewPlugin(te::VolumeAndPanPlugin::xmlTypeName, {}))
                track->pluginList.insertPlugin(plugin, track->pluginList.size(), nullptr);
        if (track->getLevelMeterPlugin() != nullptr)
            continue;
        auto meter = edit->getPluginCache().createNewPlugin(te::LevelMeterPlugin::xmlTypeName, {});
        if (meter == nullptr)
            continue;
        auto* fader = track->getVolumePlugin();
        auto behindFader = track->pluginList.size();
        for (int i = 0; i < track->pluginList.size(); ++i)
            if (track->pluginList[i] == fader)
                behindFader = i + 1;
        track->pluginList.insertPlugin(meter, behindFader, nullptr);
    }
    // The sends after the meter, and each return's way in at its head.
    ensureReturnRouting();
}

Session::TrackMixer Session::trackMixer(int track) const
{
    TrackMixer mixer;
    auto* audio = audioTrack(track);
    if (audio == nullptr)
        return mixer;
    mixer.muted = audio->isMuted(false);
    mixer.soloed = audio->isSolo(false);
    if (auto* volume = audio->getVolumePlugin())
    {
        mixer.volumeDb = volume->getVolumeDb();
        mixer.pan = volume->getPan();
    }
    return mixer;
}

// Inside a fader or pan drag the views are not told of every pixel: the control
// being dragged draws its own value, and rebuilding every panel per pixel is
// what made a drag stutter. The gesture's end tells them once.
void Session::announceMixerChange(int track, ModelChange changes)
{
    markModified(track, changes);
    if (mixerGestureDepth == 0)
        sendSynchronousChangeMessage();
}

void Session::endMixerGesture()
{
    mixerGestureDepth = std::max(0, mixerGestureDepth - 1);
    edit->getUndoManager().beginNewTransaction();
    if (mixerGestureDepth == 0)
        sendSynchronousChangeMessage();
}

// A lane names the fader as it names any device: by its place in the chain and
// the parameter's place among those the device shows, which for the engine's
// fader are its two, Volume and Pan. Worked out by the parameter itself rather
// than assumed to be 0 and 1.
Session::DeviceTarget Session::trackMixerTarget(int track, MixerControl control) const
{
    auto* audio = audioTrack(track);
    auto* fader = audio != nullptr ? audio->getVolumePlugin() : nullptr;
    if (fader == nullptr)
        return {};
    const auto* wanted = control == MixerControl::volume ? fader->volParam.get() : fader->panParam.get();
    for (int slot = 0; slot < audio->pluginList.size(); ++slot)
    {
        if (audio->pluginList[slot] != fader)
            continue;
        int index = 0;
        for (auto* parameter : fader->getAutomatableParameters())
        {
            if (parameter == nullptr || !parameter->isParameterActive())
                continue;
            if (parameter == wanted)
                return {track, slot, index};
            ++index;
        }
    }
    return {};
}

// Without this the engine would go on playing the curve under the hand, and a
// drag would snap back to the lane at the next block.
void Session::takeMixerFromLane(int track, MixerControl control)
{
    if (auto* runtime = findAutomationRuntime(trackMixerTarget(track, control));
        runtime != nullptr && runtime->active && !runtime->overridden)
    {
        runtime->overridden = true;
        mirrorAutomationToEngine();
    }
}

juce::Result Session::setTrackVolumeDb(int track, float decibels)
{
    auto* volume = trackVolumePlugin(track);
    if (volume == nullptr)
        return juce::Result::fail("That track has no fader.");
    // A double-click or an encoder sets it with no drag around it.
    takeMixerFromLane(track, MixerControl::volume);
    volume->setVolumeDb(juce::jlimit(minimumVolumeDb, maximumVolumeDb, decibels));
    // The fader is a parameter of the track's own chain.
    announceMixerChange(track, ModelChange::parameters);
    return juce::Result::ok();
}

juce::Result Session::setTrackPan(int track, float pan)
{
    auto* volume = trackVolumePlugin(track);
    if (volume == nullptr)
        return juce::Result::fail("That track has no fader.");
    takeMixerFromLane(track, MixerControl::pan);
    volume->setPan(juce::jlimit(-1.0f, 1.0f, pan));
    announceMixerChange(track, ModelChange::parameters);
    return juce::Result::ok();
}

// Gestures bracket a drag so the engine records one automation move and the
// undo history gets one entry rather than one per pixel.
void Session::beginTrackVolumeGesture(int track)
{
    if (auto* volume = trackVolumePlugin(track))
    {
        // Taken at the grab, before the first value arrives.
        takeMixerFromLane(track, MixerControl::volume);
        edit->getUndoManager().beginNewTransaction("Track volume");
        volume->volParam->parameterChangeGestureBegin();
        ++mixerGestureDepth;
    }
}

void Session::endTrackVolumeGesture(int track)
{
    if (auto* volume = trackVolumePlugin(track))
        volume->volParam->parameterChangeGestureEnd();
    // Closed even when the track went under a held fader, as a device
    // parameter's gesture is: a depth left counted silences every later move.
    endMixerGesture();
}

void Session::beginTrackPanGesture(int track)
{
    if (auto* volume = trackVolumePlugin(track))
    {
        takeMixerFromLane(track, MixerControl::pan);
        edit->getUndoManager().beginNewTransaction("Track pan");
        volume->panParam->parameterChangeGestureBegin();
        ++mixerGestureDepth;
    }
}

void Session::endTrackPanGesture(int track)
{
    if (auto* volume = trackVolumePlugin(track))
        volume->panParam->parameterChangeGestureEnd();
    // Closed even when the track went under a held fader, as a device
    // parameter's gesture is: a depth left counted silences every later move.
    endMixerGesture();
}

void Session::setTrackMuted(int track, bool muted)
{
    const auto tracks = audioTracks();
    if (!juce::isPositiveAndBelow(track, tracks.size())) return;
    if (tracks[track]->isMuted(false) == muted) return;
    toggleTrackMute(track);
}

void Session::setTrackSoloed(int track, bool soloed)
{
    const auto tracks = audioTracks();
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
        // The main output is the song's, which no deck plays through.
        announceMixerChange(masterTrackIndex(), ModelChange::none);
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
        announceMixerChange(masterTrackIndex(), ModelChange::none);
    }
}

void Session::beginMasterPanGesture()
{
    if (auto master = edit->getMasterVolumePlugin())
    {
        edit->getUndoManager().beginNewTransaction("Main pan");
        master->panParam->parameterChangeGestureBegin();
        ++mixerGestureDepth;
    }
}

void Session::endMasterPanGesture()
{
    if (auto master = edit->getMasterVolumePlugin())
    {
        master->panParam->parameterChangeGestureEnd();
        endMixerGesture();
    }
}

void Session::beginMasterVolumeGesture()
{
    if (auto master = edit->getMasterVolumePlugin())
    {
        edit->getUndoManager().beginNewTransaction("Main volume");
        master->volParam->parameterChangeGestureBegin();
        ++mixerGestureDepth;
    }
}

void Session::endMasterVolumeGesture()
{
    if (auto master = edit->getMasterVolumePlugin())
    {
        master->volParam->parameterChangeGestureEnd();
        endMixerGesture();
    }
}

}
