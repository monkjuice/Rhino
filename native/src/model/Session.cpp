#include "SessionInternal.h"
// The count-in and the DJ booth are held by unique_ptr, so the destructor
// here needs their definitions even though nothing in this file drives them.
#include "CountInClick.h"
#include "SessionDjInternal.h"
#include <algorithm>
#include <atomic>
#include <set>

namespace rhino
{
namespace
{
bool commandLineTestMode = false;

// How many folders deep a path is: a drive's root is 0, C:\Users 1.
int folderDepth(const juce::File& file)
{
    int depth = 0;
    if (file != juce::File())
        for (auto folder = file; folder != folder.getParentDirectory(); folder = folder.getParentDirectory())
            ++depth;
    return depth;
}

// The system's own folders and the temp folder. A file in one is named
// absolute from a document outside one, and the other way round.
bool inSystemOrTempFolder(const juce::File& file)
{
    const auto path = file.getFullPathName();
   #if JUCE_MAC
    for (const auto* folder : {"/System", "/Library", "/usr", "/var", "/private/var"})
        if (path.startsWith(folder)) return true;
   #elif JUCE_WINDOWS
    for (const auto& folder : {juce::File::getSpecialLocation(juce::File::windowsSystemDirectory).getParentDirectory(),
                               juce::File::getSpecialLocation(juce::File::globalApplicationsDirectory)})
        if (path.startsWithIgnoreCase(folder.getFullPathName())) return true;
   #elif JUCE_LINUX
    for (const auto* folder : {"/usr", "/var", "/etc", "/sys", "/proc"})
        if (path.startsWith(folder)) return true;
   #endif
    return file.isAChildOf(juce::File::getSpecialLocation(juce::File::tempDirectory));
}

// How a document at `document` names `file`: relative to the document's
// folder where the engine would store it so - the two share a folder deep
// enough to be one workspace, or a shallow one both sit close to - and
// absolute otherwise. The rule is the engine's (shouldUseRelativePath,
// tracktion_SourceFileReference.cpp at 4536d8a, less the project folder Rhino
// never has), restated because it is private to that file. The engine also
// measures from the document itself, with getRelativePathFrom, which takes a
// document that is not on disk yet - a Save As to a new name - for a folder
// and climbs one level too many; this measures from the document's folder.
juce::String sourceNameFor(const juce::File& file, const juce::File& document, bool alwaysRelative)
{
    const auto relative = [&]
    {
        if (document == juce::File())
            return false;
       #if JUCE_WINDOWS
        if (document.getFullPathName()[0] != file.getFullPathName()[0])
            return false;
       #endif
        if (alwaysRelative)
            return true;
        if (inSystemOrTempFolder(file) != inSystemOrTempFolder(document))
            return false;
        const auto folder = document.getParentDirectory();
        auto shared = folder;
        while (shared != shared.getParentDirectory() && !file.isAChildOf(shared))
            shared = shared.getParentDirectory();
        const auto depth = folderDepth(shared);
        return depth > 2 || (depth > 0 && folderDepth(file) - depth <= 3 && folderDepth(folder) - depth <= 3);
    }();
    return relative ? file.getRelativePathFrom(document.getParentDirectory()) : file.getFullPathName();
}

// Unique across every Session in the process, so two documents never share one.
juce::uint64 nextDocumentSerial()
{
    static std::atomic<juce::uint64> last {0};
    return ++last;
}
}

void nameSourcesFor(juce::ValueTree node, te::Edit& live, const juce::File& document)
{
    if (node.hasType(te::IDs::AUDIOCLIP) || node.hasType(te::IDs::TAKE))
        if (const auto named = node[te::IDs::source].toString(); named.isNotEmpty())
            if (const auto file = te::SourceFileReference::findFileFromString(live, named); file != juce::File())
                node.setProperty(te::IDs::source, sourceNameFor(file, document, live.alwaysUseRelativePaths.get()), nullptr);
    for (auto child : node)
        nameSourcesFor(child, live, document);
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
    initialiseKeyboards();
    initialiseExternalPlugins();
    buildStarterEdit();
    addChangeListener(&automationMirror);
}

// The preview and the count-in each hold an audio callback on the engine's
// device manager, so both have to come off before either of them goes.
Session::~Session()
{
    // While the edit is whole: the bounce puts back what it changed for its
    // render, and either render hands the edit back to the device.
    cancelBounce();
    cancelExport();
    releaseMeterTaps();
    removeChangeListener(&automationMirror);
    if (midiDeviceWatcher != nullptr)
        engine.getDeviceManager().removeChangeListener(midiDeviceWatcher.get());
    // A keyboard's port and its listeners on the engine's devices deliver from
    // the MIDI thread into devices the engine is about to take down.
    closeKeyboardPorts();
    keyboards.reset();
    // The controller has closed its port by now (the window goes first), so
    // nothing is delivering through these.
    {
        const juce::SpinLock::ScopedLockType lock(controllerTargetLock);
        controllerAllIns.reset();
        controllerPush.reset();
    }
    // Off the plugin's processor while the edit still holds it: the mapper is
    // destroyed before the edit is, and the audio thread may still be running.
    detachParameterMapping();
    cancelCountIn();
    // The tracks' hidden DJ ends point into the booth, which goes before the
    // document does: they let go of it first.
    djUnbindAll();
    releaseDj();
    releasePreview();
    // Last: anything above may still ask for a track.
    unwatchEdit();
}

// The starter document, built here rather than in the constructor so that File >
// New project can ask for the very same thing the app opens with.
void Session::buildStarterEdit()
{
    edit = te::createEmptyEdit(engine, {});
    documentSerial = nextDocumentSerial();
    watchEditTree();
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
    const auto starterTracks = audioTracks();
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
    // A delay and a reverb on the returns, every track sending nothing to
    // either until it is turned up.
    addStarterReturns();
    returnSoloHeard = false;
    // The main row's card, coloured as a new track is.
    edit->state.setProperty(mainColourID, pickTrackColour().toString(), nullptr);
    refreshLoop();
    edit->getUndoManager().clearUndoHistory();
    edit->resetChangedStatus();
}

void Session::newProject()
{
    // A bounce or an export renders the outgoing edit.
    cancelBounce();
    cancelExport();
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
    detachParameterMapping();
    releaseMeterTaps();
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

    for (auto* track : audioTracks())
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

// A bounce or an export in flight renders tracks and clips that an undo could
// take away under it; the shell blocks input meanwhile, and this holds the
// line for anything that reaches here anyway.
void Session::undo()
{
    if (!isBouncing() && !isExporting())
        refreshAfterUndoRedo(edit->getUndoManager().undo());
}

void Session::redo()
{
    if (!isBouncing() && !isExporting())
        refreshAfterUndoRedo(edit->getUndoManager().redo());
}

void Session::refreshAfterUndoRedo(bool changed)
{
    // An undo can take back any edit at all, so it cannot say what it changed.
    if (changed) markModified(-1, ModelChange::unknown);
    // A solo or a mute may have come back, on a return or under one soloed.
    if (changed) followReturnSolo(true);
    // A track's output names a bus as "track N", resolved by the engine the
    // moment the name is put back - partway through the replay, maybe before
    // the tracks are back in their places. Resolved again now they are, which
    // writes nothing (reconcileTrackGroups). Down the stack, since resolving
    // asks whether the bus already feeds the track, and only for a track that
    // names one: for any other output the engine restarts playback instead.
    if (changed)
        for (auto* track : audioTracks())
            if (auto& output = track->getOutput(); !output.usesDefaultAudioOut())
                output.updateOutput();
    repairPatternClip();
    edit->tempoSequence.updateTempoData();
    refreshLoop();
    if (changed && edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
}

juce::ValueTree Session::projectSnapshot(const juce::File& destination)
{
    edit->flushState();
    // The booth is written into the state only when a snapshot is taken.
    writeDjState();
    auto snapshot = edit->state.createCopy();
    snapshot.setProperty("rhinoSnapshotRevision", changeRevision, nullptr);
    // A file beside the document is stored relative to it, so a project
    // folder can move whole; written to another file as the edit names it,
    // every such name pointed into a folder beside that file that was never
    // there, and the reopened clips played nothing. Only the copy is named
    // again: a new name on a live clip restarts its playback, and the clips
    // the undo history can bring back are named as the edit names them.
    const auto opened = edit->editFileRetriever ? edit->editFileRetriever() : juce::File();
    if (destination != juce::File() && destination != opened)
        nameSourcesFor(snapshot, *edit, destination);
    return snapshot;
}

void Session::markModified(int changedTrack, ModelChange changes)
{
    ++changeRevision;
    edit->markAsChanged();
    // A change naming a track counts against that track whatever its kind: a
    // clip's colour is `none` and is still something a view of the track
    // shows. One naming no track counts against every track unless it is
    // nothing any view of a track shows.
    if (auto* track = audioTrack(changedTrack))
        ++trackRevisions[track->itemID.getRawID()];
    else if (changes != ModelChange::none)
        ++allTracksRevision;
    // A name, a colour or a view setting moves no lane and changes nothing a
    // deck plays.
    if (changes == ModelChange::none)
        return;
    // Anything else may have moved a lane, the device a lane drives, or the
    // track it sits on.
    automationMirrorStale = true;
    // A chain that changed may have lost the device being mapped from.
    if ((static_cast<unsigned>(changes) & static_cast<unsigned>(ModelChange::devices)) != 0)
        dropParameterMappingIfDeviceGone();
    // A deck playing a clip of the track makes it again when what the clip
    // plays changed; a change known to be one track's leaves the others alone.
    djDocumentChanged(changedTrack, changes);
}

juce::uint64 Session::trackRevision(int track) const
{
    auto* audio = audioTrack(track);
    if (audio == nullptr)
        return 0;
    const auto own = trackRevisions.find(audio->itemID.getRawID());
    return allTracksRevision + (own != trackRevisions.end() ? own->second : 0);
}

// After the edit is made or replaced. The watcher is on the edit's own tree,
// so a track the engine adds on its own - none today - is seen too.
void Session::watchEditTree()
{
    audioTracksStale = true;
    cachedAudioTracks.clear();
    trackRevisions.clear();
    edit->state.addListener(&trackCacheWatcher);
    edit->getTransport().addChangeListener(&engineStateRelay);
    edit->getUndoManager().addChangeListener(&engineStateRelay);
}

void Session::unwatchEdit()
{
    if (edit == nullptr)
        return;
    edit->state.removeListener(&trackCacheWatcher);
    edit->getTransport().removeChangeListener(&engineStateRelay);
    edit->getUndoManager().removeChangeListener(&engineStateRelay);
}

void Session::EngineStateRelay::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (session.edit != nullptr && source == &session.edit->getTransport())
        session.transportState.sendChangeMessage();
    else
        session.undoHistory.sendChangeMessage();
}

bool Session::canUndo() const
{
    return edit->getUndoManager().canUndo();
}

bool Session::canRedo() const
{
    return edit->getUndoManager().canRedo();
}

bool Session::ownsDevice(const te::Plugin& plugin) const
{
    return plugin.state.isAChildOf(edit->state);
}

const juce::Array<te::AudioTrack*>& Session::cachedTracks() const
{
    if (audioTracksStale)
    {
        cachedAudioTracks = te::getAudioTracks(*edit);
        audioTracksStale = false;
    }
    return cachedAudioTracks;
}

juce::Array<te::AudioTrack*> Session::audioTracks() const
{
    return cachedTracks();
}

te::AudioTrack* Session::audioTrack(int index) const
{
    const auto& tracks = cachedTracks();
    return juce::isPositiveAndBelow(index, tracks.size()) ? tracks.getUnchecked(index) : nullptr;
}

void Session::markClipsModified(std::vector<int> tracks, ModelChange also)
{
    const auto count = trackCount();
    std::erase_if(tracks, [count](int track) { return !juce::isPositiveAndBelow(track, count); });
    std::sort(tracks.begin(), tracks.end());
    tracks.erase(std::unique(tracks.begin(), tracks.end()), tracks.end());
    if (tracks.empty())
    {
        markModified(-1, ModelChange::notes | ModelChange::audioClips | also);
        return;
    }
    for (const auto track : tracks)
        markModified(track, (trackType(track) == TrackType::midi ? ModelChange::notes : ModelChange::audioClips) | also);
}

juce::Result Session::restoreProject(const juce::ValueTree& state, const juce::File& file)
{
    if (!state.hasType(te::IDs::EDIT) || static_cast<int>(state.getProperty("rhinoFormatVersion")) != 1)
        return juce::Result::fail("This is not a supported Rhino native project.");
    // The document's file goes in with its state rather than after it: a
    // clip resolves its source file while the edit is being built, so every
    // clip stored relative to the document - anything imported after it was
    // first opened - came back silent when the edit was told afterwards.
    auto candidate = loadDocumentEdit(engine, state.createCopy(), file, te::Edit::forEditing);
    if (!candidate) return juce::Result::fail("The project could not be loaded.");
    if (te::getAudioTracks(*candidate).isEmpty())
        return juce::Result::fail("This project has no tracks.");
    // Documents written when a track could stack instruments collapse here.
    // Nothing else is added or switched: reopening restores what the document
    // records, track by track, and never decides what a track ought to run or
    // which of them is first.
    collapseStackedInstruments(*candidate);
    // A bounce or an export renders the outgoing edit.
    cancelBounce();
    cancelExport();
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
    detachParameterMapping();
    releaseMeterTaps();
    unwatchEdit();
    edit = std::move(candidate);
    documentSerial = nextDocumentSerial();
    watchEditTree();
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
    // The returns' isolation follows a solo saved on one of them.
    returnSoloHeard = false;
    followReturnSolo(false);
    // A document from before the main row had a card is given a colour for
    // it, as a new one is; written outside the history it is opened with.
    if (!edit->state.hasProperty(mainColourID))
        edit->state.setProperty(mainColourID, pickTrackColour().toString(), nullptr);
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
    // Takes and merged audio go beside the file the document was saved as
    // (recordingDirectory, mergedAudioDirectory). The edit itself goes on
    // resolving its clips' files against the file it was opened from: every
    // name the live clips and the undo history hold was made against that
    // file, and pointed at this one the edit would look for each of them in
    // the wrong folder. Each snapshot names them for its own file instead.
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
