#include "SessionInternal.h"
// The count-in and the DJ booth are held by unique_ptr, so the destructor
// here needs their definitions even though nothing in this file drives them.
#include "CountInClick.h"
#include "SessionDjInternal.h"
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
    // The MIDI device list is built on a timer and rebuilt whenever a device
    // is enabled or a keyboard is plugged in, so what a track listens to has
    // to be re-resolved when it changes rather than once at arming time.
    midiDeviceWatcher = std::make_unique<MidiDeviceWatcher>(*this);
    engine.getDeviceManager().addChangeListener(midiDeviceWatcher.get());
    initialiseExternalPlugins();
    buildStarterEdit();
    addChangeListener(&automationMirror);
}

// The preview and the count-in each hold an audio callback on the engine's
// device manager, so both have to come off before either of them goes.
Session::~Session()
{
    removeChangeListener(&automationMirror);
    if (midiDeviceWatcher != nullptr)
        engine.getDeviceManager().removeChangeListener(midiDeviceWatcher.get());
    cancelCountIn();
    releaseDj();
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
    // Four empty tracks, MIDI and audio alternating, which is the stack most
    // songs start from: somewhere to play and somewhere to drop a file, twice
    // over, without anyone having to build it first. Each one says what it is
    // at creation, so a MIDI lane takes a clip straight away and an audio lane
    // refuses one, exactly as a track added later does.
    static constexpr TrackType starterTypes[] {TrackType::midi, TrackType::audio,
                                               TrackType::midi, TrackType::audio};
    static constexpr int starterTrackCount = 4;
    edit->ensureNumberOfAudioTracks(starterTrackCount);
    const auto starterTracks = te::getAudioTracks(*edit);
    for (int i = 0; i < starterTrackCount; ++i)
    {
        auto* starter = starterTracks[i];
        const auto type = starterTypes[static_cast<size_t>(i)];
        starter->setName(trackTypeName(type));
        // Only MIDI is written down: audio is what a track with nothing to say
        // is, so an audio track needs no property.
        if (type == TrackType::midi)
            starter->state.setProperty(trackTypeID, "midi", nullptr);
        starter->setColour(pickTrackColour());
        starter->pluginList.insertPlugin(edit->getPluginCache().createNewPlugin(UtilityDevice::xmlTypeName, {}),
                                         starter->pluginList.size(), nullptr);
    }
    // The note editor opens on a hidden starter clip on the first MIDI track,
    // so it works from the first click.
    auto* track = starterTracks[0];
    const auto end = edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(beatsPerBar()));
    patternClip = track->insertMIDIClip("Pattern 1", {{}, end}, nullptr).get();
    if (patternClip != nullptr)
    {
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
    lastTouchedParameter = {};
    automationRuntime.clear();
    mirroredCurves.clear();
    mirroredPlugins.clear();
    automationMirrorStale = true;
    manualLoop = false;
    buildStarterEdit();
    // The booth belongs to the document, so a new one starts with no decks.
    djReset();
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
    repairPatternClip();
    edit->tempoSequence.updateTempoData();
    refreshLoop();
    if (changed && edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
}

juce::ValueTree Session::projectSnapshot()
{
    edit->flushState();
    // The booth is written into the state only when a snapshot is taken.
    writeDjState();
    auto snapshot = edit->state.createCopy();
    snapshot.setProperty("rhinoSnapshotRevision", changeRevision, nullptr);
    return snapshot;
}

void Session::markModified(int changedTrack)
{
    ++changeRevision;
    // Anything that changes the document may have moved a lane, the device a
    // lane drives, or the track it sits on.
    automationMirrorStale = true;
    edit->markAsChanged();
    // A bounced deck plays what the document said a moment ago; a change
    // known to be one track's leaves the other decks alone.
    djDocumentChanged(changedTrack);
}

juce::Result Session::restoreProject(const juce::ValueTree& state, const juce::File& file)
{
    if (!state.hasType(te::IDs::EDIT) || static_cast<int>(state.getProperty("rhinoFormatVersion")) != 1)
        return juce::Result::fail("This is not a supported Rhino native project.");
    auto candidate = te::loadEditFromState(engine, state.createCopy());
    if (!candidate) return juce::Result::fail("The project could not be loaded.");
    candidate->editFileRetriever = [file] { return file; };
    if (te::getAudioTracks(*candidate).isEmpty())
        return juce::Result::fail("This project has no tracks.");
    // Documents written when a track could stack instruments collapse here.
    // Nothing else is added or switched: reopening restores what the document
    // records, track by track, and never decides what a track ought to run or
    // which of them is first.
    collapseStackedInstruments(*candidate);
    listeners.call(&Listener::editWillChange);
    cancelCountIn();
    stop();
    // Held against the outgoing edit's clips, so they go with it.
    clipsBeforeRecording.clear();
    recordingStart = -1.0;
    recordingStarted = false;
    playbackStartSeconds = 0.0;
    // Before the outgoing edit goes: these hold its plugins alive, and a
    // plugin must not outlive the edit it belongs to.
    mirroredCurves.clear();
    edit = std::move(candidate);
    // The automation that was being played by hand belonged to the outgoing
    // document's tracks, and is keyed by their indices.
    lastTouchedParameter = {};
    automationRuntime.clear();
    mirroredPlugins.clear();
    automationMirrorStale = true;
    // A dragged loop span is not part of a document, so it does not survive
    // into the next one, exactly as File > New drops it.
    manualLoop = false;
    patternClip = nullptr;
    patternClipID = {};
    repairPatternClip();
    ensureSceneSlots();
    ensureTrackMixers();
    // Documents written before a group was a bus are rebuilt as real ones here,
    // which is also what puts every member output back onto its bus.
    migrateLegacyTrackGroups();
    reconcileTrackGroups();
    // The decks the document saved come back empty and stale; the poll
    // loads them again.
    readDjState();
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
