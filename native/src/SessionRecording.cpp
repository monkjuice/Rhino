#include "SessionInternal.h"
#include "CountInClick.h"
#include <set>

// Recording.
//
// Two decisions shape everything here.
//
// The first is that a *track* is armed, not an input. What a track records
// follows from what the track already is: one running an instrument takes the
// MIDI input, one without takes the audio input from Audio settings, and a
// group bus - which carries its members' audio and holds no clips - takes
// neither. There is nothing for the user to pair up and nothing that can
// disagree with the rest of the document. That arm state is a property of the
// track, so it travels with the track when it is reordered and is saved with
// the project, exactly as mute and solo are; the engine's own input
// destinations are rebuilt from it whenever it changes rather than being a
// second place the answer lives.
//
// The second is that the count-in counts with the playhead standing still. The
// engine offers its own, but it works by rolling the playhead backwards through
// the bars before the punch point, which is why it can only offer the one and
// two bar counts its CountIn enum names - and why it cannot count in at all at
// bar one, where there is no earlier time to roll from. Rhino counts with its
// own click instead (see CountInClick.h), which is what makes one, two, three
// and four bars all behave the same wherever the playhead is standing.
//
// Neither recording needs a clip to record into. The engine writes a new clip
// when the transport stops, and tidyRecordedClips then applies Rhino's own rule
// to it: the clip that just arrived wins the ground it landed on.

namespace rhino
{

juce::File RhinoEngineBehaviour::getFileForNewAudioRecording(te::Track& track, const juce::String& fileExtension)
{
    // Left to itself the engine builds this name from a pattern whose
    // %projectdir% resolves to nothing for a document that has never been
    // saved, which drops the take in the process's working directory. This
    // hook is consulted first, so Rhino simply says where the file goes.
    if (!recordingDirectory)
        return {};
    const auto folder = recordingDirectory();
    if (folder == juce::File{} || !folder.createDirectory().wasOk())
        return {};
    auto name = juce::File::createLegalFileName(track.getName().trim());
    if (name.isEmpty())
        name = "Track";
    for (int take = 1; take < 10000; ++take)
    {
        const auto file = folder.getChildFile(name + " Take " + juce::String(take) + fileExtension);
        if (!file.exists())
            return file;
    }
    return {};
}

// Beside the project once it has been saved, so a project folder carries its
// own audio. Until then the takes go to the application's folder: an untitled
// document has no folder of its own to put them in, and the alternative the
// engine falls back to is the working directory.
juce::File Session::recordingDirectory() const
{
    if (projectFile != juce::File{})
        return projectFile.getParentDirectory().getChildFile(projectFile.getFileNameWithoutExtension() + " Recordings");
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Rhino").getChildFile("Recordings");
}

Session::RecordInput Session::trackRecordInput(int trackIndex) const
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(trackIndex, tracks.size()))
        return RecordInput::none;
    if (isGroupBusTrack(trackIndex))
        return RecordInput::none;
    return trackType(trackIndex) == TrackType::midi ? RecordInput::midi : RecordInput::audio;
}

bool Session::isTrackArmed(int trackIndex) const
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(trackIndex, tracks.size()))
        return false;
    return static_cast<bool>(tracks[trackIndex]->state.getProperty(trackArmedID, false));
}

bool Session::anyTrackArmed() const
{
    const auto tracks = te::getAudioTracks(*edit);
    for (int track = 0; track < tracks.size(); ++track)
        if (isTrackArmed(track))
            return true;
    return false;
}

// Arming succeeds whenever the track is one that can be armed, and the result
// reports whether the input behind it is actually there. Those are two
// different questions and the user is owed both answers at once: the dot
// lights, and the status line says why nothing would be captured yet.
juce::Result Session::setTrackArmed(int trackIndex, bool armed)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(trackIndex, tracks.size()))
        return juce::Result::fail("The main output records nothing. Arm a track instead.");
    if (trackRecordInput(trackIndex) == RecordInput::none)
        return juce::Result::fail("A group track carries its members' audio, so it records nothing of its own.");
    if (isTrackArmed(trackIndex) == armed)
        return juce::Result::ok();
    // Arming is not an edit of the music, so it opens no undo transaction -
    // the same treatment a track's row height gets. It is still written to the
    // track, so it is saved and it follows the track when the stack is
    // reordered.
    tracks[trackIndex]->state.setProperty(trackArmedID, armed, nullptr);
    const auto applied = applyRecordArming();
    markModified();
    sendSynchronousChangeMessage();
    return applied;
}

void Session::toggleTrackArmed(int trackIndex)
{
    setTrackArmed(trackIndex, !isTrackArmed(trackIndex));
}

bool Session::isRecording() const
{
    return edit->getTransport().isRecording();
}

// Rebuilds the engine's input destinations from the arm flags. Everything is
// cleared first rather than diffed, because the answer is cheap to recompute
// and a stale destination records into the wrong track.
juce::Result Session::applyRecordArming()
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    const auto tracks = te::getAudioTracks(*edit);
    bool wantsMidi = false, wantsAudio = false;
    for (int track = 0; track < tracks.size(); ++track)
    {
        if (!isTrackArmed(track))
            continue;
        if (trackRecordInput(track) == RecordInput::midi) wantsMidi = true;
        else if (trackRecordInput(track) == RecordInput::audio) wantsAudio = true;
    }
    if (!wantsMidi && !wantsAudio)
    {
        clearRecordArming();
        return juce::Result::ok();
    }

    auto& deviceManager = engine.getDeviceManager();
    // The audio input is the one Audio settings made the default, or the first
    // one the device offers when nothing has been chosen. Arming turns it on:
    // an input nobody had enabled is exactly the one the user has just asked
    // to record from.
    te::WaveInputDevice* wave = nullptr;
    if (wantsAudio)
    {
        wave = deviceManager.getDefaultWaveInDevice();
        if (wave == nullptr)
            for (auto* candidate : deviceManager.getWaveInputDevices())
                if (candidate != nullptr) { wave = candidate; break; }
    }
    te::MidiInputDevice* midi = nullptr;
    if (wantsMidi)
    {
        midi = deviceManager.getDefaultMidiInDevice();
        if (midi == nullptr)
            for (const auto& candidate : deviceManager.getMidiInDevices())
                if (candidate != nullptr && candidate->isEnabled()) { midi = candidate.get(); break; }
        if (midi == nullptr)
            for (const auto& candidate : deviceManager.getMidiInDevices())
                if (candidate != nullptr) { midi = candidate.get(); break; }
    }

    // Monitoring is decided before anything is armed, because the engine makes
    // a live input audible as soon as a destination is record-enabled and the
    // mode is the one it defaults to. Audio follows the preference, which is
    // off; MIDI is always monitored, or playing an armed instrument track would
    // be silent.
    if (wave != nullptr)
        wave->setMonitorMode(monitorAudioInput == InputMonitoring::on        ? te::InputDevice::MonitorMode::on
                             : monitorAudioInput == InputMonitoring::automatic ? te::InputDevice::MonitorMode::automatic
                                                                              : te::InputDevice::MonitorMode::off);
    if (midi != nullptr)
        midi->setMonitorMode(te::InputDevice::MonitorMode::automatic);

    // An input instance is built when the playback context is, and only for
    // devices that were enabled at the time. Enabling comes first for that
    // reason, and a context that predates it is rebuilt so the instance exists.
    auto enabledSomething = false;
    if (wave != nullptr && !wave->isEnabled())
    {
        deviceManager.setDeviceEnabled(*wave, true);
        enabledSomething = true;
    }
    if (midi != nullptr && !midi->isEnabled())
    {
        midi->setEnabled(true);
        enabledSomething = true;
    }
    // Nothing to arm against while the device is shut. Saying so is better
    // than allocating a playback context that has no input to give it.
    if (engine.getDeviceManager().deviceManager.getCurrentAudioDevice() == nullptr)
        return juce::Result::fail("Rhino has no audio device open. Choose one in Audio settings.");
    auto& transport = edit->getTransport();
    if (enabledSomething && !transport.isPlaying())
        transport.freePlaybackContext();
    transport.ensureContextAllocated();
    auto* context = edit->getCurrentPlaybackContext();
    if (context == nullptr)
        return juce::Result::fail("Rhino could not open the audio device to record with.");
    edit->dispatchPendingUpdatesSynchronously();

    for (auto* input : context->getAllInputs())
        if (input != nullptr)
            (void) te::clearFromTargets(*input, nullptr);

    juce::String problem;
    for (int track = 0; track < tracks.size(); ++track)
    {
        if (!isTrackArmed(track))
            continue;
        const auto wanted = trackRecordInput(track);
        te::InputDevice* device = wanted == RecordInput::midi ? static_cast<te::InputDevice*>(midi)
                                                              : static_cast<te::InputDevice*>(wave);
        if (device == nullptr)
        {
            problem = wanted == RecordInput::midi
                          ? "No MIDI input is available. Connect a keyboard, then enable it in Audio settings."
                          : "No audio input is available. Choose one in Audio settings.";
            continue;
        }
        auto* instance = context->getInputFor(device);
        if (instance == nullptr)
        {
            problem = "Rhino could not open " + device->getName() + " to record from.";
            continue;
        }
        // A MIDI recording replaces what it covers rather than merging into it,
        // which is what makes a take over an existing part a new take.
        if (auto* midiDevice = dynamic_cast<te::MidiInputDevice*>(device))
        {
            midiDevice->mergeRecordings = false;
            midiDevice->replaceExistingClips = true;
        }
        auto destination = instance->setTarget(tracks[track]->itemID, true, nullptr);
        if (!destination)
        {
            problem = destination.error();
            continue;
        }
        (*destination)->recordEnabled = true;
    }
    edit->dispatchPendingUpdatesSynchronously();
    return problem.isEmpty() ? juce::Result::ok() : juce::Result::fail(problem);
}

void Session::clearRecordArming()
{
    if (auto* context = edit->getCurrentPlaybackContext())
        for (auto* input : context->getAllInputs())
            if (input != nullptr)
                (void) te::clearFromTargets(*input, nullptr);
}

juce::String Session::inputMonitoringName(InputMonitoring mode)
{
    return mode == InputMonitoring::on          ? "In"
         : mode == InputMonitoring::automatic   ? "Auto"
                                                : "Off";
}

// Changing this has to reach a track that is already armed, so the arming is
// reapplied rather than waiting for the next time it is touched.
void Session::setInputMonitoring(InputMonitoring mode)
{
    if (monitorAudioInput == mode)
        return;
    monitorAudioInput = mode;
    // It describes the hardware in front of you rather than the song, so it is
    // a preference of this machine and not a property of the document.
    if (!isCommandLineTestMode())
    {
        juce::PropertiesFile properties(rhinoSettingsOptions());
        properties.setValue("monitorAudioInput", static_cast<int>(mode));
        properties.saveIfNeeded();
    }
    // Changing the monitor mode restarts the transports, so a take in progress
    // would be cut in half by it. The setting takes hold at the next arm
    // instead, which is the next moment it could matter.
    if (!isRecording() && !isCountingIn())
        juce::ignoreUnused(applyRecordArming());
    sendSynchronousChangeMessage();
}

// Off unless asked for - see the note on the enum. A developer's setting
// cannot decide what the suite does, so the test runs always read the default.
Session::InputMonitoring Session::readInputMonitoringPreference()
{
    if (isCommandLineTestMode())
        return InputMonitoring::off;
    juce::PropertiesFile properties(rhinoSettingsOptions());
    const auto stored = properties.getIntValue("monitorAudioInput", static_cast<int>(InputMonitoring::off));
    return stored == static_cast<int>(InputMonitoring::on)        ? InputMonitoring::on
         : stored == static_cast<int>(InputMonitoring::automatic) ? InputMonitoring::automatic
                                                                  : InputMonitoring::off;
}

int Session::countInBars() const
{
    return juce::jlimit(0, maximumCountInBars, static_cast<int>(edit->state.getProperty(countInBarsID, 0)));
}

void Session::setCountInBars(int bars)
{
    bars = juce::jlimit(0, maximumCountInBars, bars);
    if (countInBars() == bars)
        return;
    edit->state.setProperty(countInBarsID, bars, nullptr);
    markModified();
    sendSynchronousChangeMessage();
}

CountInClick& Session::countInClick()
{
    if (countIn == nullptr)
        countIn = std::make_unique<CountInClick>();
    return *countIn;
}

// A second callback on the engine's own device, which JUCE sums with the
// transport's - the arrangement the browser preview already uses. Adding and
// removing a callback takes the device manager's own lock, which is what makes
// "configure, then attach" enough on its own: nothing can be rendering through
// the generator while start() is rewriting it.
void Session::attachCountIn()
{
    if (countIn == nullptr || countInAttached)
        return;
    engine.getDeviceManager().deviceManager.addAudioCallback(countIn.get());
    countInAttached = true;
}

bool Session::isCountingIn() const
{
    return countIn != nullptr && countIn->isRunning();
}

int Session::countInBarsRemaining() const
{
    return countIn != nullptr ? countIn->barsRemaining() : 0;
}

// Also the teardown: nothing is left registered on the device while no count is
// running, so a session that never records never touches the audio callback
// list. Called from the destructor and before the device closes, either of
// which can come first.
void Session::cancelCountIn()
{
    if (countIn == nullptr)
        return;
    countIn->cancel();
    if (countInAttached)
    {
        engine.getDeviceManager().deviceManager.removeAudioCallback(countIn.get());
        countInAttached = false;
    }
}

juce::Result Session::toggleRecording()
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (isCountingIn())
    {
        cancelCountIn();
        sendSynchronousChangeMessage();
        return juce::Result::ok();
    }
    if (isRecording())
    {
        stopRecording();
        return juce::Result::ok();
    }
    if (!anyTrackArmed())
        return juce::Result::fail("Arm a track first: the dot on its card is what records into it.");
    const auto armed = applyRecordArming();
    if (armed.failed())
        return armed;

    const auto bars = countInBars();
    const auto haveDevice = engine.getDeviceManager().deviceManager.getCurrentAudioDevice() != nullptr;
    // A count-in belongs to a recording that starts from a standstill. Punching
    // in while the transport is already rolling has nothing to count into, and
    // a count with no device to play it on would never finish.
    if (bars > 0 && haveDevice && !edit->getTransport().isPlaying())
    {
        CountInClick::Settings settings;
        settings.tempoBpm = tempo();
        settings.beatsPerBar = beatsPerBar();
        settings.bars = bars;
        settings.gainDb = clickTrackGain();
        settings.emphasiseBars = clickTrackEmphasiseBars();
        const auto rate = engine.getDeviceManager().getSampleRate();
        // cancelCountIn above has already taken the callback off the device, so
        // the count is configured with nothing rendering through it.
        cancelCountIn();
        countInClick().start(settings, rate > 0.0 ? rate : 44100.0, [this]
        {
            beginTransportRecording();
            sendSynchronousChangeMessage();
        });
        attachCountIn();
        sendSynchronousChangeMessage();
        return juce::Result::ok();
    }
    beginTransportRecording();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

void Session::beginTransportRecording()
{
    auto& transport = edit->getTransport();
    // Rhino loops the whole song by default. A recording that wrapped round to
    // the start would lay the next take over the one just played, so the loop
    // is lifted for the duration and refreshLoop puts it back on the way out.
    transport.looping = false;
    clipsBeforeRecording.clear();
    for (auto* track : te::getAudioTracks(*edit))
        for (auto* clip : track->getClips())
            clipsBeforeRecording.push_back(clip->itemID);
    recordingStart = std::max(0.0, transport.getPosition().inSeconds());
    recordingStarted = false;
    edit->getUndoManager().beginNewTransaction("Record");
    transport.record(false, false);
}

void Session::stopRecording()
{
    auto& transport = edit->getTransport();
    cancelCountIn();
    transport.stop(false, false);
    releasePlayingNotes();
    finishRecording();
}

// Polled by the shell, because the engine neither begins nor ends a recording
// on the message thread and broadcasts neither. record() is asked for here and
// takes effect on the audio thread, so there is a window in which a recording
// has been started and the transport still says it is not recording: without
// recordingStarted this would read that window as a recording already over.
void Session::recordingStopped()
{
    if (recordingStart < 0.0)
        return;
    if (edit->getTransport().isRecording())
    {
        recordingStarted = true;
        return;
    }
    if (!recordingStarted)
        return;
    finishRecording();
}

void Session::finishRecording()
{
    if (recordingStart < 0.0)
        return;
    const auto changed = tidyRecordedClips();
    clipsBeforeRecording.clear();
    recordingStart = -1.0;
    recordingStarted = false;
    refreshLoop();
    edit->getUndoManager().beginNewTransaction();
    if (changed)
        markModified();
    sendSynchronousChangeMessage();
}

// The engine drops a recorded clip on top of whatever was already on the track.
// Rhino's rule is that the clip which just arrived wins the ground it landed on,
// so the same call every drag, trim and drop ends in is applied to it here.
bool Session::tidyRecordedClips()
{
    const std::set<te::EditItemID> before(clipsBeforeRecording.begin(), clipsBeforeRecording.end());
    std::vector<te::EditItemID> added;
    for (auto* track : te::getAudioTracks(*edit))
        for (auto* clip : track->getClips())
            if (!before.contains(clip->itemID))
                added.push_back(clip->itemID);
    if (added.empty())
        return false;
    // Looked up by id rather than held as pointers: making room for one clip
    // can delete another, and a recording into two tracks adds two at once.
    for (const auto id : added)
    {
        auto* clip = findClip(id);
        if (clip == nullptr)
            continue;
        // A take is a clip like any other, so it is coloured the way the path
        // that would otherwise have made it colours one: the engine's own
        // green for a recording would be the only clip in the document
        // wearing it.
        if (dynamic_cast<te::WaveAudioClip*>(clip) != nullptr)
            clip->setColour(juce::Colour(0xff4d6975));
        else if (auto* track = dynamic_cast<te::AudioTrack*>(clip->getTrack()))
            clip->setColour(instrumentColour(activeTrackInstrument(*track)));
        makeRoomForClip(*clip, added);
    }
    repairPatternClip();
    return true;
}

}
