#include "SessionInternal.h"
// The count-in is held by unique_ptr, so the destructor here needs its
// definition even though nothing in this file drives it.
#include "CountInClick.h"
#include <algorithm>
#include <set>

namespace rhino
{
namespace
{
bool commandLineTestMode = false;
}

void Session::setCommandLineTestMode(bool enabled)
{
    commandLineTestMode = enabled;
}

bool isCommandLineTestMode()
{
    return commandLineTestMode;
}

Session::Session() : engine(commandLineTestMode ? "Rhino Native Tests" : "Theda Native",
                            nullptr, std::make_unique<RhinoEngineBehaviour>())
{
    // The behaviour is built with the engine, before there is a Session to ask
    // where a recording goes, so it is handed the question rather than the
    // answer. This is the only thing Rhino tells the engine about itself.
    if (auto* behaviour = dynamic_cast<RhinoEngineBehaviour*>(&engine.getEngineBehaviour()))
        behaviour->recordingDirectory = [this] { return recordingDirectory(); };
    DeviceCatalog::registerBuiltInTypes(engine);
    initialiseExternalPlugins();
    buildStarterEdit();
}

// The preview and the count-in each hold an audio callback on the engine's
// device manager, so both have to come off before either of them goes.
Session::~Session()
{
    cancelCountIn();
    releasePreview();
}

// The starter document, built here rather than in the constructor so that File >
// New project can ask for the very same thing the app opens with.
void Session::buildStarterEdit()
{
    edit = te::createEmptyEdit(engine, {});
    edit->state.setProperty("rhinoFormatVersion", 1, nullptr);
    edit->clickTrackEnabled = false;
    edit->clickTrackEmphasiseBars = true;
    edit->clickTrackGain = juce::Decibels::decibelsToGain(-6.0f);
    edit->tempoSequence.getTempo(0)->setBpm(120.0);
    // One empty track, as a new document should be. It runs no instrument, so
    // it is neither a MIDI nor an audio track until something is dropped on it:
    // an instrument makes it one and renames it, a sample makes it the other.
    edit->ensureNumberOfAudioTracks(1);
    auto* track = te::getAudioTracks(*edit)[0];
    track->setName("Track 1");
    auto device = edit->getPluginCache().createNewPlugin(UtilityDevice::xmlTypeName, {});
    utility = dynamic_cast<UtilityDevice*>(device.get());
    track->pluginList.insertPlugin(device, track->pluginList.size(), nullptr);
    const auto end = edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(beatsPerBar()));
    patternClip = track->insertMIDIClip("Pattern 1", {{}, end}, nullptr).get();
    if (patternClip != nullptr)
    {
        patternClip->setColour(presetColour(PatternPreset::WarmPulse));
        patternClip->state.setProperty(starterPlaceholderID, true, nullptr);
        patternClip->state.setProperty(editorStepsID, defaultSteps, nullptr);
    }
    patternClipID = patternClip->itemID;
    ensureSceneSlots();
    ensureTrackMixers();
    refreshLoop();
    edit->getUndoManager().clearUndoHistory();
    edit->resetChangedStatus();
}

void Session::newProject()
{
    listeners.call(&Listener::editWillChange);
    cancelCountIn();
    stop();
    // Everything cached from the outgoing edit goes with it: these point at
    // plugins and parameters the starter edit is about to replace.
    clipsBeforeRecording.clear();
    recordingStart = -1.0;
    recordingStarted = false;
    playbackStartSeconds = 0.0;
    audioUtility = nullptr;
    lastTouchedParameter = {};
    automationRuntime.clear();
    offlineAutomation.clear();
    manualLoop = false;
    buildStarterEdit();
    projectFile = juce::File{};
    // A save still running against the old document must not report this one
    // as saved, so the revision moves on rather than resetting to zero.
    savedRevision = ++changeRevision;
    refreshLoop();
    listeners.call(&Listener::editDidChange);
    sendSynchronousChangeMessage();
}

void Session::panicReset(bool restartAudioDevice)
{
    cancelCountIn();
    clipsBeforeRecording.clear();
    recordingStart = -1.0;
    recordingStarted = false;
    te::TransportControl::stopAllTransports(engine, false, true);
    auto& transport = edit->getTransport();
    transport.stop(false, true);
    transport.setPosition({});

    for (auto* track : te::getAudioTracks(*edit))
    {
        resetPluginList(&track->pluginList);
        for (auto* clip : track->getClips())
            resetPluginList(clip->getPluginList());
    }

    transport.freePlaybackContext();
    engine.getDeviceManager().deviceManager.closeAudioDevice();
    if (restartAudioDevice)
    {
        engine.getDeviceManager().deviceManager.restartLastAudioDevice();
        transport.ensureContextAllocated(true);
    }
    sendSynchronousChangeMessage();
}

void Session::undo()
{
    refreshAfterUndoRedo(edit->getUndoManager().undo());
}

void Session::redo()
{
    refreshAfterUndoRedo(edit->getUndoManager().redo());
}

void Session::refreshAfterUndoRedo(bool changed)
{
    if (changed) markModified();
    ensureEditablePatternClip();
    edit->tempoSequence.updateTempoData();
    refreshLoop();
    if (changed && edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
}

void Session::ensureEditablePatternClip()
{
    if (auto* midi = dynamic_cast<te::MidiClip*>(findClip(patternClipID)))
    {
        patternClip = midi;
        return;
    }

    auto* track = patternTrackOf(*edit);
    if (track != nullptr)
        for (auto* clip : track->getClips())
            if (auto* midi = dynamic_cast<te::MidiClip*>(clip))
            {
                patternClip = midi;
                patternClipID = midi->itemID;
                return;
            }

    if (track != nullptr)
    {
        const auto end = edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(beatsPerBar()));
        patternClip = track->insertMIDIClip("Pattern 1", {{}, end}, nullptr).get();
        if (patternClip != nullptr)
        {
            patternClip->setColour(presetColour(PatternPreset::WarmPulse));
            patternClip->state.setProperty(starterPlaceholderID, true, nullptr);
            patternClipID = patternClip->itemID;
        }
    }
}

juce::ValueTree Session::projectSnapshot()
{
    edit->flushState();
    auto snapshot = edit->state.createCopy();
    snapshot.setProperty("rhinoSnapshotRevision", changeRevision, nullptr);
    return snapshot;
}

void Session::markModified()
{
    ++changeRevision;
    edit->markAsChanged();
}

juce::Result Session::restoreProject(const juce::ValueTree& state, const juce::File& file)
{
    if (!state.hasType(te::IDs::EDIT) || static_cast<int>(state.getProperty("rhinoFormatVersion")) != 1)
        return juce::Result::fail("This is not a supported Rhino native project.");
    auto candidate = te::loadEditFromState(engine, state.createCopy());
    if (!candidate) return juce::Result::fail("The project could not be loaded.");
    candidate->editFileRetriever = [file] { return file; };
    const auto tracks = te::getAudioTracks(*candidate);
    if (tracks.isEmpty())
        return juce::Result::fail("This project has no tracks.");
    // A group bus is a track in this list, and it holds neither clips nor a
    // utility. The pattern track and the audio track are therefore the first
    // two tracks that are not buses, rather than indices 0 and 1.
    auto* patternTrack = patternTrackOf(*candidate);
    if (patternTrack == nullptr)
        return juce::Result::fail("This project has no playable tracks.");
    te::AudioTrack* audioTrack = nullptr;
    for (auto* track : tracks)
        if (track != patternTrack && static_cast<int>(track->state.getProperty(trackGroupBusID, 0)) == 0)
        {
            audioTrack = track;
            break;
        }
    te::MidiClip* nextPattern = nullptr;
    UtilityDevice* nextUtility = nullptr;
    UtilityDevice* nextAudioUtility = nullptr;
    for (auto* clip : patternTrack->getClips())
        if (auto* midi = dynamic_cast<te::MidiClip*>(clip))
            nextPattern = midi;
    for (auto plugin : patternTrack->pluginList)
        if (auto* device = dynamic_cast<UtilityDevice*>(plugin)) nextUtility = device;
    if (audioTrack != nullptr)
        for (auto plugin : audioTrack->pluginList)
            if (auto* device = dynamic_cast<UtilityDevice*>(plugin)) nextAudioUtility = device;
    if (!nextPattern || !nextUtility)
        return juce::Result::fail("The project is missing its pattern track devices.");
    // Documents written when a track could stack instruments collapse here.
    collapseStackedInstruments(*candidate);
    // Nothing is added here. A track that ran no instrument is a track that
    // plays audio, and an instrument put in front of its clips replaces them
    // with a synth that has no notes to play.
    // Positional, exactly like the pointer it fills: the audio utility belongs to
    // track 1, so a project saved with nothing but the pattern track has nowhere
    // to put one. The read above already asks whether that track exists; without
    // the same question here, a single-track project asks a one-element array for
    // tracks[1], gets the null juce::Array hands back for an index it does not
    // have, and dereferences it. refreshUtilityPointers() leaves this pointer
    // null for precisely the same reason, so null is the supported state for a
    // project this shape rather than a device that failed to be created.
    if (!nextAudioUtility && audioTrack != nullptr)
    {
        auto device = candidate->getPluginCache().createNewPlugin(UtilityDevice::xmlTypeName, {});
        nextAudioUtility = dynamic_cast<UtilityDevice*>(device.get());
        audioTrack->pluginList.insertPlugin(device, 0, nullptr);
    }
    listeners.call(&Listener::editWillChange);
    cancelCountIn();
    stop();
    // Held against the outgoing edit's clips, so they go with it.
    clipsBeforeRecording.clear();
    recordingStart = -1.0;
    recordingStarted = false;
    playbackStartSeconds = 0.0;
    edit = std::move(candidate);
    patternClip = nextPattern;
    patternClipID = patternClip->itemID;
    utility = nextUtility;
    audioUtility = nextAudioUtility;
    // Reopening restores what the document records; it never decides what a
    // track ought to run. The property names the instrument the pattern track
    // was last given, so switching to it is a no-op for a track that already
    // carries it and a rebuild for one whose plugin had to be recreated. A
    // document that never named one - every track of it holding audio rather
    // than notes - is left exactly as it was saved.
    const auto patternInstrument = edit->state.getProperty("rhinoPatternInstrument").toString();
    if (patternInstrument.isNotEmpty())
    {
        bool instrumentChanged = false;
        if (patternInstrument == "wave")
            juce::ignoreUnused(switchTrackInstrument(*edit, *patternTrack, Instrument::RhinoWave, instrumentChanged));
        else if (patternInstrument == "forge")
            juce::ignoreUnused(switchTrackInstrument(*edit, *patternTrack, Instrument::RhinoForge, instrumentChanged,
                                                     forgeDescription ? &*forgeDescription : nullptr));
        else
            setPatternInstrument(patternInstrument == "drums");
    }
    ensureSceneSlots();
    ensureTrackMixers();
    // Documents written before a group was a bus are rebuilt as real ones here,
    // which is also what puts every member output back onto its bus.
    migrateLegacyTrackGroups();
    reconcileTrackGroups();
    projectFile = file;
    savedRevision = ++changeRevision;
    edit->getUndoManager().clearUndoHistory();
    edit->resetChangedStatus();
    refreshLoop();
    listeners.call(&Listener::editDidChange);
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

void Session::projectSaved(const juce::ValueTree& snapshot, const juce::File& file)
{
    projectFile = file;
    // Edits made while the worker wrote the snapshot must remain unsaved.
    if (static_cast<juce::int64>(snapshot.getProperty("rhinoSnapshotRevision")) == changeRevision)
    {
        savedRevision = changeRevision;
        edit->resetChangedStatus();
    }
    sendSynchronousChangeMessage();
}

}
