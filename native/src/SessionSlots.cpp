#include "SessionInternal.h"
#include <algorithm>

// Session view model: scenes, clip slots and clip launching.
//
// Tracktion owns launch timing. Everything here runs on the message thread and
// only queues launch-handle state or edits slot contents; the audio thread
// reads those handles and decides when a slot actually starts or stops.
//
// One rule is enforced here rather than by the engine: a track plays at most
// one slot clip at a time. The engine mixes every playing slot on a track, so
// launching a clip explicitly stops that track's other clips at the same beat.

namespace theta
{
namespace
{
// Slot clips loop until stopped, so a preset slot clip is one bar long.
constexpr int presetSlotBars = 1;
}

te::ClipSlot* Session::clipSlotAt(int track, int scene) const
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size())) return nullptr;
    const auto slots = tracks[track]->getClipSlotList().getClipSlots();
    if (!juce::isPositiveAndBelow(scene, slots.size())) return nullptr;
    return slots[scene];
}

void Session::ensureSceneSlots(int minimumScenes)
{
    auto& scenes = edit->getSceneList();
    const auto wanted = std::max(minimumScenes, scenes.getNumScenes());
    scenes.ensureNumberOfScenes(wanted);
    // A track restored from an older document, or added before the scene list
    // existed, can still be short of slots.
    for (auto* track : te::getAudioTracks(*edit))
        track->getClipSlotList().ensureNumberOfSlots(wanted);
}

int Session::sceneCount() const
{
    return edit->getSceneList().getNumScenes();
}

juce::String Session::sceneName(int scene) const
{
    const auto scenes = edit->getSceneList().getScenes();
    if (!juce::isPositiveAndBelow(scene, scenes.size()))
        return {};
    const auto name = scenes[scene]->name.get();
    return name.isNotEmpty() ? name : "Scene " + juce::String(scene + 1);
}

te::SceneWatcher* Session::sceneWatcher() const
{
    return &edit->getSceneList().sceneWatcher;
}

Session::SlotClip Session::slotClip(int track, int scene) const
{
    SlotClip info;
    auto* slot = clipSlotAt(track, scene);
    if (slot == nullptr) return info;
    auto* clip = slot->getClip();
    if (clip == nullptr) return info;
    info.hasClip = true;
    info.name = clip->getName();
    info.colour = clip->getColour();
    info.clipID = clip->itemID;
    info.isMidi = dynamic_cast<te::MidiClip*>(clip) != nullptr;
    if (auto handle = clip->getLaunchHandle())
    {
        info.playing = handle->getPlayingStatus() == te::LaunchHandle::PlayState::playing;
        if (const auto queued = handle->getQueuedStatus())
        {
            info.playQueued = *queued == te::LaunchHandle::QueueState::playQueued;
            info.stopQueued = *queued == te::LaunchHandle::QueueState::stopQueued;
        }
    }
    return info;
}

bool Session::trackHasActiveSlot(int track) const
{
    for (int scene = 0; scene < sceneCount(); ++scene)
    {
        const auto info = slotClip(track, scene);
        if (info.playing || info.playQueued)
            return true;
    }
    return false;
}

te::LaunchQType Session::launchQuantisation() const
{
    return edit->getLaunchQuantisation().type.get();
}

void Session::setLaunchQuantisation(te::LaunchQType type)
{
    edit->getLaunchQuantisation().type = type;
    markModified();
    sendSynchronousChangeMessage();
}

// The beat a launch should land on, or nullopt to take effect immediately.
// Immediate is also the right answer before the playback context exists, which
// is the case for the very first clip launched from a stopped transport.
std::optional<te::MonotonicBeat> Session::nextLaunchBeat() const
{
    const auto type = edit->getLaunchQuantisation().type.get();
    if (type == te::LaunchQType::none)
        return {};
    if (auto* context = edit->getTransport().getCurrentPlaybackContext())
        if (const auto sync = context->getSyncPoint())
        {
            const auto quantised = te::getNext(type, edit->tempoSequence, sync->beat);
            return te::MonotonicBeat {sync->monotonicBeat.v + (quantised - sync->beat)};
        }
    return {};
}

void Session::startTransportForLaunch()
{
    auto& transport = edit->getTransport();
    if (transport.isPlaying())
        return;
    transport.ensureContextAllocated(true);
    transport.play(false);
}

juce::Result Session::launchSlot(int track, int scene)
{
    auto* slot = clipSlotAt(track, scene);
    if (slot == nullptr)
        return juce::Result::fail("That clip slot does not exist.");
    auto* clip = slot->getClip();
    if (clip == nullptr)
    {
        // An empty slot is the track's stop button, as it is in Live.
        stopTrackSlots(track);
        return juce::Result::ok();
    }
    auto handle = clip->getLaunchHandle();
    if (handle == nullptr)
        return juce::Result::fail("That clip cannot be launched.");
    startTransportForLaunch();
    const auto at = nextLaunchBeat();
    for (int other = 0; other < sceneCount(); ++other)
    {
        if (other == scene) continue;
        if (auto* otherSlot = clipSlotAt(track, other))
            if (auto* otherClip = otherSlot->getClip())
                if (auto otherHandle = otherClip->getLaunchHandle())
                    otherHandle->stop(at);
    }
    handle->play(at);
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::launchScene(int scene)
{
    if (!juce::isPositiveAndBelow(scene, sceneCount()))
        return juce::Result::fail("That scene does not exist.");
    startTransportForLaunch();
    const auto at = nextLaunchBeat();
    const auto tracks = te::getAudioTracks(*edit);
    for (int track = 0; track < tracks.size(); ++track)
    {
        // A track with nothing in this scene stops, matching Live's behaviour.
        for (int other = 0; other < sceneCount(); ++other)
        {
            if (other == scene) continue;
            if (auto* otherSlot = clipSlotAt(track, other))
                if (auto* otherClip = otherSlot->getClip())
                    if (auto handle = otherClip->getLaunchHandle())
                        handle->stop(at);
        }
        if (auto* slot = clipSlotAt(track, scene))
            if (auto* clip = slot->getClip())
                if (auto handle = clip->getLaunchHandle())
                    handle->play(at);
    }
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

void Session::stopTrackSlots(int track)
{
    const auto at = nextLaunchBeat();
    for (int scene = 0; scene < sceneCount(); ++scene)
        if (auto* slot = clipSlotAt(track, scene))
            if (auto* clip = slot->getClip())
                if (auto handle = clip->getLaunchHandle())
                    handle->stop(at);
    sendSynchronousChangeMessage();
}

void Session::stopAllSlots()
{
    const auto at = nextLaunchBeat();
    const auto tracks = te::getAudioTracks(*edit);
    for (int track = 0; track < tracks.size(); ++track)
        for (int scene = 0; scene < sceneCount(); ++scene)
            if (auto* slot = clipSlotAt(track, scene))
                if (auto* clip = slot->getClip())
                    if (auto handle = clip->getLaunchHandle())
                        handle->stop(at);
    sendSynchronousChangeMessage();
}

// Launching a slot clip makes that track ignore its timeline clips, which is
// what Live does. Without a way back, the arrangement would stay silent on
// those tracks for the rest of the session -- this is Live's Back to
// Arrangement. Clearing the flag also stops any slot clip still playing.
bool Session::anyTrackPlayingSlots() const
{
    for (auto* track : te::getAudioTracks(*edit))
        if (track->playSlotClips.get())
            return true;
    return false;
}

void Session::returnToArrangement()
{
    for (auto* track : te::getAudioTracks(*edit))
        track->playSlotClips = false;
    sendSynchronousChangeMessage();
}

juce::Result Session::addScene()
{
    edit->getUndoManager().beginNewTransaction("Add scene");
    ensureSceneSlots(sceneCount() + 1);
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::deleteScene(int scene)
{
    const auto scenes = edit->getSceneList().getScenes();
    if (!juce::isPositiveAndBelow(scene, scenes.size()))
        return juce::Result::fail("That scene does not exist.");
    if (scenes.size() <= 1)
        return juce::Result::fail("Keep at least one scene.");
    edit->getUndoManager().beginNewTransaction("Delete scene");
    edit->getSceneList().deleteScene(*scenes[scene]);
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::deleteSlotClip(int track, int scene)
{
    auto* slot = clipSlotAt(track, scene);
    if (slot == nullptr)
        return juce::Result::fail("That clip slot does not exist.");
    auto* clip = slot->getClip();
    if (clip == nullptr)
        return juce::Result::fail("That slot is already empty.");
    if (auto handle = clip->getLaunchHandle())
        handle->stop({});
    edit->getUndoManager().beginNewTransaction("Delete slot clip");
    clip->removeFromParent();
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::insertPatternPresetInSlot(PatternPreset preset, int track, int scene)
{
    auto* slot = clipSlotAt(track, scene);
    if (slot == nullptr)
        return juce::Result::fail("Drop clips on a session slot.");
    const auto data = presetPattern(preset);
    edit->getUndoManager().beginNewTransaction("Add " + data.name + " to slot");
    if (const auto prepared = preparePresetTrack(track, data, preset); prepared.failed())
        return prepared;
    const auto beats = static_cast<double>(presetSlotBars) * beatsPerBar();
    const auto end = edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(beats));
    auto clip = te::insertMIDIClip(*slot, data.name, {tracktion::core::TimePosition(), end});
    if (clip == nullptr)
        return juce::Result::fail("The pattern clip could not be added to that slot.");
    clip->setColour(presetColour(preset));
    fillMidiClip(*clip, data, edit->getUndoManager());
    clip->setLoopRangeBeats({tracktion::core::BeatPosition(), tracktion::core::BeatPosition::fromBeats(beats)});
    edit->getUndoManager().beginNewTransaction();
    markModified();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::insertInstrumentClipInSlot(Instrument instrument, int track, int scene)
{
    if (instrument == Instrument::Utility)
        return addInstrument(instrument, track);
    auto* slot = clipSlotAt(track, scene);
    if (slot == nullptr)
        return juce::Result::fail("Drop instruments on a session slot.");
    const auto useDrums = instrument == Instrument::Drums;
    const auto name = useDrums ? juce::String("Theta Drums")
        : instrument == Instrument::ThetaWave ? juce::String("Theta Wave")
        : instrument == Instrument::ThetaForge ? juce::String("Theta Forge")
        : juce::String("4OSC synth");
    edit->getUndoManager().beginNewTransaction("Add " + name + " to slot");
    bool instrumentChanged = false;
    const auto result = switchTrackInstrument(*edit, *te::getAudioTracks(*edit)[track], instrument, instrumentChanged,
                                              forgeDescription ? &*forgeDescription : nullptr);
    if (result.failed())
        return result;
    const auto beats = static_cast<double>(presetSlotBars) * beatsPerBar();
    const auto end = edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(beats));
    auto clip = te::insertMIDIClip(*slot, name, {tracktion::core::TimePosition(), end});
    if (clip == nullptr)
        return juce::Result::fail("The instrument clip could not be added to that slot.");
    clip->setColour(instrumentColour(instrument));
    clip->setLoopRangeBeats({tracktion::core::BeatPosition(), tracktion::core::BeatPosition::fromBeats(beats)});
    edit->getUndoManager().beginNewTransaction();
    markModified();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::insertAudioFileInSlot(const juce::File& file, int track, int scene)
{
    auto* slot = clipSlotAt(track, scene);
    if (slot == nullptr)
        return juce::Result::fail("Drop audio on a session slot.");
    if (!file.existsAsFile())
        return juce::Result::fail("That audio file could not be found.");
    const te::AudioFile audioFile(engine, file);
    if (!audioFile.isValid())
        return juce::Result::fail("Theta could not read " + file.getFileName() + ".");
    edit->getUndoManager().beginNewTransaction("Add audio to slot");
    auto clip = te::insertWaveClip(*slot, file.getFileNameWithoutExtension(), file,
                                   {{tracktion::core::TimePosition(),
                                     tracktion::core::TimePosition::fromSeconds(audioFile.getLength())}},
                                   te::DeleteExistingClips::yes);
    if (clip == nullptr)
        return juce::Result::fail("The audio clip could not be added to that slot.");
    edit->getUndoManager().beginNewTransaction();
    markModified();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

}
