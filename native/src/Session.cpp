#include "SessionInternal.h"
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

Session::Session() : engine(commandLineTestMode ? "Rhino Native Tests" : "Theda Native")
{
    engine.getPluginManager().createBuiltInType<UtilityDevice>();
    engine.getPluginManager().createBuiltInType<DrumDevice>();
    engine.getPluginManager().createBuiltInType<RhinoSpaceDevice>();
    engine.getPluginManager().createBuiltInType<RhinoBloomDevice>();
    engine.getPluginManager().createBuiltInType<RhinoArpDevice>();
    engine.getPluginManager().createBuiltInType<RhinoWaveDevice>();
    initialiseExternalPlugins();
    buildStarterEdit();
}

// The starter document, built here rather than in the constructor so that File >
// New project can ask for the very same thing the app opens with.
void Session::buildStarterEdit()
{
    edit = te::createEmptyEdit(engine, {});
    edit->state.setProperty("rhinoFormatVersion", 1, nullptr);
    edit->clickTrackEnabled = false;
    edit->clickTrackEmphasiseBars = true;
    edit->clickTrackGain = -6.0f;
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
    stop();
    // Everything cached from the outgoing edit goes with it: these point at
    // plugins and parameters the starter edit is about to replace.
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

    const auto tracks = te::getAudioTracks(*edit);
    if (!tracks.isEmpty())
        for (auto* clip : tracks[0]->getClips())
            if (auto* midi = dynamic_cast<te::MidiClip*>(clip))
            {
                patternClip = midi;
                patternClipID = midi->itemID;
                return;
            }

    if (!tracks.isEmpty())
    {
        const auto end = edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(beatsPerBar()));
        patternClip = tracks[0]->insertMIDIClip("Pattern 1", {{}, end}, nullptr).get();
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
    te::MidiClip* nextPattern = nullptr;
    UtilityDevice* nextUtility = nullptr;
    UtilityDevice* nextAudioUtility = nullptr;
    for (auto* track : tracks)
        for (auto* clip : track->getClips())
            if (auto* midi = dynamic_cast<te::MidiClip*>(clip))
            {
                if (track == tracks[0]) nextPattern = midi;
            }
    for (auto plugin : tracks[0]->pluginList)
        if (auto* device = dynamic_cast<UtilityDevice*>(plugin)) nextUtility = device;
    if (tracks.size() > 1)
        for (auto plugin : tracks[1]->pluginList)
            if (auto* device = dynamic_cast<UtilityDevice*>(plugin)) nextAudioUtility = device;
    if (!nextPattern || !nextUtility)
        return juce::Result::fail("The project is missing its pattern track devices.");
    // Documents written when a track could stack instruments collapse here.
    collapseStackedInstruments(*candidate);
    if (trackInstrument(*tracks[0]) == nullptr)
    {
        auto device = candidate->getPluginCache().createNewPlugin(te::FourOscPlugin::xmlTypeName, {});
        if (device == nullptr)
            return juce::Result::fail("The pattern track instrument could not be created.");
        tracks[0]->pluginList.insertPlugin(device, 0, nullptr);
    }
    // Positional, exactly like the pointer it fills: the audio utility belongs to
    // track 1, so a project saved with nothing but the pattern track has nowhere
    // to put one. The read above already asks whether that track exists; without
    // the same question here, a single-track project asks a one-element array for
    // tracks[1], gets the null juce::Array hands back for an index it does not
    // have, and dereferences it. refreshUtilityPointers() leaves this pointer
    // null for precisely the same reason, so null is the supported state for a
    // project this shape rather than a device that failed to be created.
    if (!nextAudioUtility && tracks.size() > 1)
    {
        auto device = candidate->getPluginCache().createNewPlugin(UtilityDevice::xmlTypeName, {});
        nextAudioUtility = dynamic_cast<UtilityDevice*>(device.get());
        tracks[1]->pluginList.insertPlugin(device, 0, nullptr);
    }
    listeners.call(&Listener::editWillChange);
    stop();
    edit = std::move(candidate);
    patternClip = nextPattern;
    patternClipID = patternClip->itemID;
    utility = nextUtility;
    audioUtility = nextAudioUtility;
    const auto patternInstrument = edit->state.getProperty("rhinoPatternInstrument").toString();
    if (patternInstrument == "wave")
    {
        bool instrumentChanged = false;
        juce::ignoreUnused(switchTrackInstrument(*edit, *tracks[0], Instrument::RhinoWave, instrumentChanged));
    }
    else if (patternInstrument == "forge")
    {
        bool instrumentChanged = false;
        juce::ignoreUnused(switchTrackInstrument(*edit, *tracks[0], Instrument::RhinoForge, instrumentChanged,
                                                 forgeDescription ? &*forgeDescription : nullptr));
    }
    else
    {
        setPatternInstrument(patternInstrument == "drums");
    }
    ensureSceneSlots();
    ensureTrackMixers();
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
