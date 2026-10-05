#include "SessionInternal.h"
#include "CountInClick.h"
#include <algorithm>
#include <set>
#include <utility>
#include <vector>

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
namespace
{
// How the three settings are spelled in the document. Words rather than the
// enum's numbers, so a .rhinoedit says what it means when it is read by eye.
juce::String monitoringToken(Session::InputMonitoring mode)
{
    return mode == Session::InputMonitoring::on          ? "on"
         : mode == Session::InputMonitoring::automatic   ? "auto"
                                                         : "off";
}

Session::InputMonitoring defaultMonitoringFor(Session::RecordInput input)
{
    return input == Session::RecordInput::midi ? Session::InputMonitoring::automatic
                                               : Session::InputMonitoring::off;
}

// Off < Auto < On. The order a device shared by several tracks is reconciled
// with, and the only one that never silences a track that asked to hear
// itself.
int monitoringStrength(Session::InputMonitoring mode)
{
    return mode == Session::InputMonitoring::on ? 2 : mode == Session::InputMonitoring::automatic ? 1 : 0;
}
}

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

te::MidiInputDevice* Session::midiInputDevice() const
{
    auto& deviceManager = engine.getDeviceManager();
    if (auto* chosen = deviceManager.getDefaultMidiInDevice())
        return chosen;
    // Nothing chosen: the first that is switched on, and failing that the
    // first there is. On a machine with one keyboard those are the same thing.
    for (const auto& candidate : deviceManager.getMidiInDevices())
        if (candidate != nullptr && candidate->isEnabled())
            return candidate.get();
    for (const auto& candidate : deviceManager.getMidiInDevices())
        if (candidate != nullptr)
            return candidate.get();
    return nullptr;
}

bool Session::hasMidiInput() const
{
    return midiInputDevice() != nullptr;
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
    // Not while a take is running: rebuilding every input's destinations under
    // a recording can cut the tracks already capturing. The flag is kept, and
    // the next take is armed from it. A count-in is still before the take, so
    // a track armed during one is armed for it.
    auto applied = juce::Result::ok();
    if (!isRecording())
        applied = applyRecordArming();
    else
        armingDeferred = true;
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

// Rebuilds the engine's input destinations from the arm flags and the monitor
// settings. Everything is cleared first rather than diffed, because the answer
// is cheap to recompute and a stale destination records into the wrong track.
//
// Arming is no longer the only reason a track needs its input routed to it.
// Monitoring On means you hear the input whether or not a take would capture
// it, so such a track gets a destination of its own with recordEnabled left
// false: the input reaches the track and nothing is written.
// The one rule for whether a track needs its input routed to it: armed, so a
// take captures it, or monitoring On, so it is heard while unarmed.
bool Session::trackWantsInput(int track) const
{
    if (trackRecordInput(track) == RecordInput::none)
        return false;
    return isTrackArmed(track) || trackMonitoring(track) == InputMonitoring::on;
}

juce::Result Session::applyRecordArming()
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    const auto tracks = te::getAudioTracks(*edit);
    const auto wantsInput = [this](int track) { return trackWantsInput(track); };
    bool wantsMidi = false, wantsAudio = false;
    for (int track = 0; track < tracks.size(); ++track)
    {
        if (!wantsInput(track))
            continue;
        if (trackRecordInput(track) == RecordInput::midi) wantsMidi = true;
        else wantsAudio = true;
    }
    if (!wantsMidi && !wantsAudio)
    {
        clearRecordArming();
        return juce::Result::ok();
    }

    auto& deviceManager = engine.getDeviceManager();
    // Creating the typing keyboard's device rescans the MIDI device list and
    // rebuilds every device object with it, so it happens before anything here
    // has resolved a device to a pointer.
    if (wantsMidi)
        for (int track = 0; track < tracks.size(); ++track)
            if (wantsInput(track) && trackMidiInput(track) == midiInputKeyboardToken())
            {
                ensureComputerKeyboardDevice();
                break;
            }
    // Every track names its own input, audio as well as MIDI, so this is a
    // device per track rather than one for the document. All Ins resolves to
    // the engine's merge of the physical inputs, and a physical input only
    // feeds that merge while it is open - so enabling them is part of what All
    // Ins means.
    std::vector<te::InputDevice*> trackDevice(static_cast<size_t>(tracks.size()), nullptr);
    auto wantsAllIns = false;
    for (int track = 0; track < tracks.size(); ++track)
    {
        if (!wantsInput(track))
            continue;
        if (trackRecordInput(track) == RecordInput::midi)
        {
            trackDevice[static_cast<size_t>(track)] = midiInputDeviceForTrack(track);
            if (trackMidiInput(track) == midiInputAllInsToken())
                wantsAllIns = true;
        }
        else
        {
            trackDevice[static_cast<size_t>(track)] = audioInputDeviceForTrack(track);
        }
    }
    if (wantsAllIns)
        enablePhysicalMidiInputs();

    // Monitoring is decided before anything is armed, because the engine makes
    // a live input audible as soon as the destination exists and the mode
    // allows it. The mode belongs to the *device* and the only per-track term
    // in it is whether that track is armed, so two tracks sharing one input
    // cannot hold two modes at once: the device takes the strongest any of
    // them asked for. On beats Auto beats Off, because that is the only order
    // that never silences a track which asked to hear itself - and giving the
    // two tracks different inputs gives each exactly what its card says.
    std::vector<std::pair<te::InputDevice*, InputMonitoring>> deviceModes;
    for (int track = 0; track < tracks.size(); ++track)
    {
        auto* device = trackDevice[static_cast<size_t>(track)];
        if (device == nullptr)
            continue;
        const auto asked = trackMonitoring(track);
        auto existing = std::find_if(deviceModes.begin(), deviceModes.end(),
                                     [device](const auto& entry) { return entry.first == device; });
        if (existing == deviceModes.end())
            deviceModes.push_back({device, asked});
        else if (monitoringStrength(asked) > monitoringStrength(existing->second))
            existing->second = asked;
    }
    for (const auto& [device, mode] : deviceModes)
        device->setMonitorMode(mode == InputMonitoring::on          ? te::InputDevice::MonitorMode::on
                               : mode == InputMonitoring::automatic ? te::InputDevice::MonitorMode::automatic
                                                                    : te::InputDevice::MonitorMode::off);

    // An input instance is built when the playback context is, and only for
    // devices that were enabled at the time. Enabling comes first for that
    // reason, and a context that predates it is rebuilt so the instance exists.
    // Asking for an input is what switches it on: one nobody had enabled is
    // exactly the one the user has just asked to record from.
    auto enabledSomething = false;
    for (const auto& entry : deviceModes)
    {
        auto* device = entry.first;
        if (device->isEnabled())
            continue;
        // A wave input is opened through the device manager, which reopens the
        // audio device around it; a MIDI input opens on its own.
        if (auto* wave = dynamic_cast<te::WaveInputDevice*>(device))
            deviceManager.setDeviceEnabled(*wave, true);
        else
            device->setEnabled(true);
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
        if (!wantsInput(track))
            continue;
        const auto wanted = trackRecordInput(track);
        auto* device = trackDevice[static_cast<size_t>(track)];
        if (device == nullptr)
        {
            // A track set to None is doing exactly what it was asked to, so it
            // is not a problem to report, and neither is one whose device the
            // engine is still building - the watcher arms again when it lands.
            if (wanted == RecordInput::midi
                && (trackMidiInput(track) == midiInputNoneToken() || awaitingMidiDeviceScan))
                continue;
            if (wanted == RecordInput::audio && trackAudioInput(track) == audioInputNoneToken())
                continue;
            problem = wanted == RecordInput::midi
                          ? trackName(track) + " has no MIDI input to record from. Pick one on its card, "
                            "or connect a keyboard and enable it in Audio settings."
                      : trackAudioInput(track).isEmpty()
                          ? "No audio input is available. Choose one in Audio settings."
                          : trackName(track) + " records from " + trackAudioInputName(track)
                            + ", which this machine does not have. Pick an input on its card.";
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
        // The input is *added* to this track rather than moved onto it: the
        // engine's move flag clears every other destination first, so arming a
        // second track would quietly disarm the first and only the last one
        // armed would capture anything. Every destination is cleared above, so
        // adding is the whole of what is wanted here.
        auto destination = instance->setTarget(tracks[track]->itemID, false, nullptr);
        if (!destination)
        {
            problem = destination.error();
            continue;
        }
        // Arming is what records. A track that is here only because it
        // monitors at all times keeps the input routed to it and captures
        // nothing, which is the whole difference between On and Auto.
        (*destination)->recordEnabled = isTrackArmed(track);
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
    return mode == InputMonitoring::on          ? "On"
         : mode == InputMonitoring::automatic   ? "Auto"
                                                : "Off";
}

// What a track is set to, with the default for its kind standing in for a
// track that has never been asked. Auto on MIDI, because that is the only
// setting under which playing an armed instrument track makes a sound; off on
// audio, because a microphone and speakers in one room feed back.
Session::InputMonitoring Session::trackMonitoring(int trackIndex) const
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(trackIndex, tracks.size()))
        return InputMonitoring::off;
    const auto stored = tracks[trackIndex]->state.getProperty(trackMonitorID, juce::String()).toString();
    if (stored == monitoringToken(InputMonitoring::on)) return InputMonitoring::on;
    if (stored == monitoringToken(InputMonitoring::automatic)) return InputMonitoring::automatic;
    if (stored == monitoringToken(InputMonitoring::off)) return InputMonitoring::off;
    return defaultMonitoringFor(trackRecordInput(trackIndex));
}

// Changing this has to reach a track that is already armed, so the routing is
// reapplied rather than waiting for the next time it is touched - and On has
// to reach a track that is not armed at all, which is the whole point of it.
juce::Result Session::setTrackMonitoring(int trackIndex, InputMonitoring mode)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(trackIndex, tracks.size()))
        return juce::Result::fail("The main output takes no input, so there is nothing to monitor.");
    const auto wanted = trackRecordInput(trackIndex);
    if (wanted == RecordInput::none)
        return juce::Result::fail("A group track carries its members' audio, so it has no input to monitor.");
    if (trackMonitoring(trackIndex) == mode)
        return juce::Result::ok();
    // Not an edit of the music, so it opens no undo transaction - the same
    // treatment arming and the two input choosers get. The default for the
    // track's kind writes nothing, so the property is only ever there when
    // someone has said something other than the obvious.
    if (mode == defaultMonitoringFor(wanted))
        tracks[trackIndex]->state.removeProperty(trackMonitorID, nullptr);
    else
        tracks[trackIndex]->state.setProperty(trackMonitorID, monitoringToken(mode), nullptr);
    // Changing the monitor mode restarts the transports, so a take in progress
    // would be cut in half by it. The setting takes hold at the next arm
    // instead, which is the next moment it could matter.
    auto applied = juce::Result::ok();
    if (!isRecording() && !isCountingIn())
        applied = applyRecordArming();
    else
        armingDeferred = true;
    markModified();
    sendSynchronousChangeMessage();
    return applied;
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

// Nothing is left registered on the device while no count is running, so a
// session that never records never touches the audio callback list - and a
// count that has ended stops costing a block. Both ways out of a count come
// through here: cancelCountIn for one abandoned, and the completion callback
// for one that reached its last beat.
void Session::detachCountIn()
{
    if (countIn == nullptr || !countInAttached)
        return;
    engine.getDeviceManager().deviceManager.removeAudioCallback(countIn.get());
    countInAttached = false;
}

// Also the teardown. Called from the destructor and before the device closes,
// either of which can come first.
void Session::cancelCountIn()
{
    if (countIn == nullptr)
        return;
    countIn->cancel();
    detachCountIn();
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
            // This runs on the message thread, after the last beat has been
            // played out, so the click has no more work and comes off the
            // device before the transport takes over.
            detachCountIn();
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

// The engine keeps a short fifo of incoming notes per recording target,
// timestamped in edit time, precisely so a UI can draw a take while it is
// being made. This drains it and turns the note on/off pairs into spans. A
// note whose key is still down has no end yet and is left open, so the
// arrangement can draw it out to the playhead.
void Session::pollRecordingNotes()
{
    if (recordingStart < 0.0)
        return;
    auto* context = edit->getCurrentPlaybackContext();
    if (context == nullptr)
        return;
    const auto tracks = te::getAudioTracks(*edit);
    liveNotes.resize(static_cast<size_t>(tracks.size()));
    for (int track = 0; track < tracks.size(); ++track)
    {
        if (!isTrackArmed(track) || trackRecordInput(track) != RecordInput::midi)
            continue;
        auto& notes = liveNotes[static_cast<size_t>(track)];
        for (auto* input : context->getAllInputs())
        {
            if (input == nullptr)
                continue;
            auto fifo = input->getRecordingNotes(tracks[track]->itemID);
            if (fifo == nullptr)
                continue;
            juce::MidiMessage message;
            while (fifo->pop(message))
            {
                const auto time = message.getTimeStamp();
                if (message.isNoteOn())
                {
                    notes.push_back({time, -1.0, message.getNoteNumber()});
                    ++liveNoteRevision;
                }
                else if (message.isNoteOff())
                {
                    // The most recent still-open note of that pitch, so a
                    // repeated note closes the one it belongs to.
                    for (auto note = notes.rbegin(); note != notes.rend(); ++note)
                        if (note->pitch == message.getNoteNumber() && note->isHeld())
                        {
                            note->endSeconds = std::max(time, note->startSeconds);
                            ++liveNoteRevision;
                            break;
                        }
                }
            }
        }
    }
}

const std::vector<Session::RecordingNote>& Session::recordingNotes(int track) const
{
    static const std::vector<RecordingNote> none;
    if (!juce::isPositiveAndBelow(track, static_cast<int>(liveNotes.size())))
        return none;
    return liveNotes[static_cast<size_t>(track)];
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
    liveNotes.clear();
    ++liveNoteRevision;
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
    // The clips the engine just wrote say the same thing these did, and better.
    liveNotes.clear();
    ++liveNoteRevision;
    recordingStart = -1.0;
    recordingStarted = false;
    // Arming, monitoring or a device that changed while the take ran was kept
    // off the inputs so as not to cut it; now is when it takes effect.
    if (armingDeferred)
    {
        armingDeferred = false;
        juce::ignoreUnused(applyRecordArming());
    }
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
        // A take is a clip like any other, so it wears its track's colour
        // like one: nothing is stamped on it, which is what leaves it
        // matching the lane it was recorded onto.
        makeRoomForClip(*clip, added);
    }
    repairPatternClip();
    return true;
}

}
