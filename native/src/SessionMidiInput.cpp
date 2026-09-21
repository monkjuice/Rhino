#include "SessionInternal.h"

// Which MIDI input a track listens to.
//
// Until this existed there was one answer for the whole document: the device
// Audio settings had made the default, or the first one there was. That is
// right for the common case and wrong for every case with two keyboards in it,
// and it left no way to say "this track is played from the typing keyboard and
// that one from the controller" -- which is the first thing you want the
// moment a second track exists.
//
// It is a property of the *track*, as it is in Live, so it travels with the
// track when the stack is reordered and is saved with the project. Absent
// means All Ins, which is both the default and what every document written
// before this says, so nothing has to be migrated in.
//
// The engine already has the two devices this needs and Rhino was not using
// either. "All MIDI Ins" is a virtual device the engine always makes, which
// merges every physical input that is open; it is what All Ins resolves to.
// The typing keyboard gets a virtual device of its own, created the first time
// a track asks for it, with no physical sources -- so it carries the notes
// Rhino puts into its keyboard state and nothing else. Both are ordinary MIDI
// inputs by the time a track sees them, which is what keeps the rest of the
// recording path from having to know any of this.

namespace rhino
{
namespace
{
const char* const computerKeyboardDeviceName = "Computer Keyboard";
const char* const devicePrefix = "device:";
}

const juce::Identifier trackMidiInputID ("rhinoMidiInput");

juce::String Session::midiInputAllInsToken() { return {}; }
juce::String Session::midiInputKeyboardToken() { return "keyboard"; }
juce::String Session::midiInputNoneToken() { return "none"; }

juce::String Session::midiInputDeviceToken(const juce::String& deviceName)
{
    return devicePrefix + deviceName;
}

// The engine's own merge of every physical input. It is always there -- the
// device manager inserts it ahead of any virtual device the user has made --
// so All Ins never has to fall back to guessing which keyboard was meant.
te::MidiInputDevice* Session::allMidiInsDevice() const
{
    for (const auto& candidate : engine.getDeviceManager().getMidiInDevices())
        if (auto* virtualDevice = dynamic_cast<te::VirtualMidiInputDevice*>(candidate.get());
            virtualDevice != nullptr && virtualDevice->useAllInputs)
            return virtualDevice;
    return nullptr;
}

te::MidiInputDevice* Session::computerKeyboardDevice() const
{
    for (const auto& candidate : engine.getDeviceManager().getMidiInDevices())
        if (auto* virtualDevice = dynamic_cast<te::VirtualMidiInputDevice*>(candidate.get());
            virtualDevice != nullptr && !virtualDevice->useAllInputs
            && virtualDevice->getName() == computerKeyboardDeviceName)
            return virtualDevice;
    return nullptr;
}

// Made on demand rather than at startup: creating one rescans the MIDI device
// list, which rebuilds every device object, and a document that never asks for
// the typing keyboard should not pay for it or carry it in Audio settings.
te::MidiInputDevice* Session::ensureComputerKeyboardDevice()
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (auto* existing = computerKeyboardDevice())
        return existing;
    engine.getDeviceManager().createVirtualMidiDevice(computerKeyboardDeviceName);
    return computerKeyboardDevice();
}

std::vector<Session::MidiInputChoice> Session::midiInputChoices() const
{
    std::vector<MidiInputChoice> choices;
    choices.push_back({midiInputAllInsToken(), "All Ins", true});
    choices.push_back({midiInputKeyboardToken(), computerKeyboardDeviceName, true});
    for (const auto& candidate : engine.getDeviceManager().getMidiInDevices())
    {
        if (candidate == nullptr)
            continue;
        // The two virtual devices are already offered above, under names that
        // say what they are rather than what the engine calls them.
        if (dynamic_cast<te::VirtualMidiInputDevice*>(candidate.get()) != nullptr)
            continue;
        choices.push_back({midiInputDeviceToken(candidate->getName()), candidate->getName(), true});
    }
    choices.push_back({midiInputNoneToken(), "None", true});
    return choices;
}

juce::String Session::trackMidiInput(int track) const
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size()))
        return midiInputAllInsToken();
    return tracks[track]->state.getProperty(trackMidiInputID, juce::String()).toString();
}

juce::String Session::trackMidiInputName(int track) const
{
    const auto token = trackMidiInput(track);
    if (token == midiInputNoneToken())
        return "None";
    if (token == midiInputKeyboardToken())
        return computerKeyboardDeviceName;
    if (token.startsWith(devicePrefix))
        return token.substring(static_cast<int>(std::string_view(devicePrefix).size()));
    return "All Ins";
}

juce::Result Session::setTrackMidiInput(int track, const juce::String& token)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size()))
        return juce::Result::fail("The main output has no MIDI input of its own.");
    if (trackRecordInput(track) != RecordInput::midi)
        return juce::Result::fail(trackName(track) + " is not a MIDI track, so it takes no MIDI input.");
    if (trackMidiInput(track) == token)
        return juce::Result::ok();

    // Choosing the typing keyboard is what creates its device, and creating it
    // rescans the device list -- so it happens before anything is armed
    // against the answer.
    if (token == midiInputKeyboardToken())
        ensureComputerKeyboardDevice();

    // Not an edit of the music, so it opens no undo transaction - the same
    // treatment arming and row height get. It is still written to the track,
    // so it is saved and follows the track when the stack is reordered.
    if (token.isEmpty())
        tracks[track]->state.removeProperty(trackMidiInputID, nullptr);
    else
        tracks[track]->state.setProperty(trackMidiInputID, token, nullptr);
    const auto applied = applyRecordArming();
    markModified();
    sendSynchronousChangeMessage();
    return applied;
}

// What a track's setting resolves to. Null means nothing to listen to, which
// is both what None asks for and what a device named by a project made on
// another machine comes to.
te::MidiInputDevice* Session::midiInputDeviceForTrack(int track) const
{
    const auto token = trackMidiInput(track);
    if (token == midiInputNoneToken())
        return nullptr;
    if (token == midiInputKeyboardToken())
        return computerKeyboardDevice();
    if (token.startsWith(devicePrefix))
    {
        const auto wanted = token.substring(static_cast<int>(std::string_view(devicePrefix).size()));
        for (const auto& candidate : engine.getDeviceManager().getMidiInDevices())
            if (candidate != nullptr && candidate->getName() == wanted)
                return candidate.get();
        return nullptr;
    }
    return allMidiInsDevice();
}

// All Ins is a merge of the physical inputs, and a physical input only feeds it
// while it is open. Enabling every one of them is therefore part of what All
// Ins *means*, rather than a side effect of it.
void Session::enablePhysicalMidiInputs()
{
    for (const auto& candidate : engine.getDeviceManager().getMidiInDevices())
        if (candidate != nullptr && dynamic_cast<te::VirtualMidiInputDevice*>(candidate.get()) == nullptr
            && !candidate->isEnabled())
            candidate->setEnabled(true);
}

// The engine's keyboard state is the entry a MIDI device's notes take, so a
// note put in here is indistinguishable from a played one by the time it
// reaches a track.
//
// It goes into both virtual devices, because the typing keyboard is one of the
// things All Ins means: a track on All Ins and a track on Computer Keyboard
// both have to hear it. A build with neither -- which is only the moment
// before the device list has been scanned -- falls back to whatever MIDI input
// there is, which is what this did before tracks could choose.
void Session::sendMidiInputNote(int midiNote, int velocity, bool isNoteOn)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (!juce::isPositiveAndBelow(midiNote, 128))
        return;
    te::MidiInputDevice* targets[] {allMidiInsDevice(), computerKeyboardDevice()};
    auto delivered = false;
    for (auto* device : targets)
    {
        if (device == nullptr)
            continue;
        delivered = true;
        if (isNoteOn)
            device->keyboardState.noteOn(1, midiNote, juce::jlimit(0.0f, 1.0f, velocity / 127.0f));
        else
            device->keyboardState.noteOff(1, midiNote, 0.0f);
    }
    if (delivered)
        return;
    if (auto* fallback = midiInputDevice())
    {
        if (isNoteOn)
            fallback->keyboardState.noteOn(1, midiNote, juce::jlimit(0.0f, 1.0f, velocity / 127.0f));
        else
            fallback->keyboardState.noteOff(1, midiNote, 0.0f);
    }
}

}
