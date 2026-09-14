#include "SessionInternal.h"
#include <algorithm>
#include <set>

namespace theta
{
namespace
{
bool commandLineTestMode = false;
}

void Session::setCommandLineTestMode(bool enabled)
{
    commandLineTestMode = enabled;
}

Session::Session() : engine(commandLineTestMode ? "Theta Native Tests" : "Theda Native")
{
    engine.getPluginManager().createBuiltInType<UtilityDevice>();
    engine.getPluginManager().createBuiltInType<DrumDevice>();
    engine.getPluginManager().createBuiltInType<ThetaSpaceDevice>();
    engine.getPluginManager().createBuiltInType<ThetaBloomDevice>();
    engine.getPluginManager().createBuiltInType<ThetaArpDevice>();
    engine.getPluginManager().createBuiltInType<ThetaWaveDevice>();
    initialiseExternalPlugins();
    edit = te::createEmptyEdit(engine, {});
    edit->state.setProperty("thetaFormatVersion", 1, nullptr);
    edit->clickTrackEnabled = false;
    edit->clickTrackEmphasiseBars = true;
    edit->clickTrackGain = -6.0f;
    edit->tempoSequence.getTempo(0)->setBpm(120.0);
    edit->ensureNumberOfAudioTracks(2);
    auto* track = te::getAudioTracks(*edit)[0];
    track->setName("Pattern synth");
    auto synthPlugin = edit->getPluginCache().createNewPlugin(te::FourOscPlugin::xmlTypeName, {});
    synth = dynamic_cast<te::FourOscPlugin*>(synthPlugin.get());
    track->pluginList.insertPlugin(synthPlugin, 0, nullptr);
    auto drumPlugin = edit->getPluginCache().createNewPlugin(DrumDevice::xmlTypeName, {});
    drums = dynamic_cast<DrumDevice*>(drumPlugin.get());
    drums->setEnabled(false);
    track->pluginList.insertPlugin(drumPlugin, 1, nullptr);
    auto wavePlugin = edit->getPluginCache().createNewPlugin(ThetaWaveDevice::xmlTypeName, {});
    thetaWave = dynamic_cast<ThetaWaveDevice*>(wavePlugin.get());
    if (thetaWave != nullptr)
    {
        thetaWave->setEnabled(false);
        track->pluginList.insertPlugin(wavePlugin, 2, nullptr);
    }
    auto device = edit->getPluginCache().createNewPlugin(UtilityDevice::xmlTypeName, {});
    utility = dynamic_cast<UtilityDevice*>(device.get());
    track->pluginList.insertPlugin(device, track->pluginList.size(), nullptr);
    utility->gain().setParameter(-12.0f, juce::dontSendNotification);
    const auto end = edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(beatsPerBar()));
    patternClip = track->insertMIDIClip("Pattern 1", {{}, end}, nullptr).get();
    if (patternClip != nullptr)
    {
        patternClip->setColour(presetColour(PatternPreset::WarmPulse));
        patternClip->state.setProperty(starterPlaceholderID, true, nullptr);
        patternClip->state.setProperty(editorStepsID, defaultSteps, nullptr);
    }
    patternClipID = patternClip->itemID;
    auto* audioTrack = te::getAudioTracks(*edit)[1];
    audioTrack->setName("Audio 1");
    auto audioDevice = edit->getPluginCache().createNewPlugin(UtilityDevice::xmlTypeName, {});
    audioUtility = dynamic_cast<UtilityDevice*>(audioDevice.get());
    audioTrack->pluginList.insertPlugin(audioDevice, 0, nullptr);
    ensureSceneSlots();
    refreshLoop();
    edit->getUndoManager().clearUndoHistory();
    edit->resetChangedStatus();
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
    snapshot.setProperty("thetaSnapshotRevision", changeRevision, nullptr);
    return snapshot;
}

void Session::markModified()
{
    ++changeRevision;
    edit->markAsChanged();
}

juce::Result Session::restoreProject(const juce::ValueTree& state, const juce::File& file)
{
    if (!state.hasType(te::IDs::EDIT) || static_cast<int>(state.getProperty("thetaFormatVersion")) != 1)
        return juce::Result::fail("This is not a supported Theta native project.");
    auto candidate = te::loadEditFromState(engine, state.createCopy());
    if (!candidate) return juce::Result::fail("The project could not be loaded.");
    candidate->editFileRetriever = [file] { return file; };
    const auto tracks = te::getAudioTracks(*candidate);
    if (tracks.size() < 2)
        return juce::Result::fail("This editor requires a pattern track and at least one audio track.");
    te::MidiClip* nextPattern = nullptr;
    UtilityDevice* nextUtility = nullptr;
    UtilityDevice* nextAudioUtility = nullptr;
    te::FourOscPlugin* nextSynth = nullptr;
    ThetaWaveDevice* nextThetaWave = nullptr;
    DrumDevice* nextDrums = nullptr;
    for (auto* track : tracks)
        for (auto* clip : track->getClips())
            if (auto* midi = dynamic_cast<te::MidiClip*>(clip))
            {
                if (track == tracks[0]) nextPattern = midi;
            }
    for (auto plugin : tracks[0]->pluginList)
    {
        if (auto* device = dynamic_cast<UtilityDevice*>(plugin)) nextUtility = device;
        if (auto* device = dynamic_cast<te::FourOscPlugin*>(plugin)) nextSynth = device;
        if (auto* device = dynamic_cast<ThetaWaveDevice*>(plugin)) nextThetaWave = device;
        if (auto* device = dynamic_cast<DrumDevice*>(plugin)) nextDrums = device;
    }
    for (auto plugin : tracks[1]->pluginList)
        if (auto* device = dynamic_cast<UtilityDevice*>(plugin)) nextAudioUtility = device;
    if (!nextPattern || !nextUtility || !nextSynth)
        return juce::Result::fail("The project is missing its pattern or synth devices.");
    if (!nextDrums)
    {
        auto device = candidate->getPluginCache().createNewPlugin(DrumDevice::xmlTypeName, {});
        nextDrums = dynamic_cast<DrumDevice*>(device.get());
        if (!nextDrums) return juce::Result::fail("The drum device could not be created.");
        nextDrums->setEnabled(false);
        tracks[0]->pluginList.insertPlugin(device, 1, nullptr);
    }
    if (!nextThetaWave)
    {
        auto device = candidate->getPluginCache().createNewPlugin(ThetaWaveDevice::xmlTypeName, {});
        nextThetaWave = dynamic_cast<ThetaWaveDevice*>(device.get());
        if (!nextThetaWave) return juce::Result::fail("The wavetable device could not be created.");
        nextThetaWave->setEnabled(false);
        tracks[0]->pluginList.insertPlugin(device, 2, nullptr);
    }
    if (!nextAudioUtility)
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
    synth = nextSynth;
    thetaWave = nextThetaWave;
    drums = nextDrums;
    const auto patternInstrument = edit->state.getProperty("thetaPatternInstrument").toString();
    if (patternInstrument == "wave")
    {
        bool instrumentChanged = false;
        juce::ignoreUnused(switchTrackInstrument(*edit, *tracks[0], Instrument::ThetaWave, instrumentChanged));
    }
    else if (patternInstrument == "forge")
    {
        bool instrumentChanged = false;
        juce::ignoreUnused(switchTrackInstrument(*edit, *tracks[0], Instrument::ThetaForge, instrumentChanged,
                                                 forgeDescription ? &*forgeDescription : nullptr));
    }
    else
    {
        setPatternInstrument(patternInstrument == "drums");
    }
    ensureSceneSlots();
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
    if (static_cast<juce::int64>(snapshot.getProperty("thetaSnapshotRevision")) == changeRevision)
    {
        savedRevision = changeRevision;
        edit->resetChangedStatus();
    }
    sendSynchronousChangeMessage();
}

}
