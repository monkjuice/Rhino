#include "SessionInternal.h"

// The note editor's preview, Live's Preview switch.
//
// With it on, a note drawn, carried or clicked in the editor, or a key pressed
// on its keyboard, is heard through the open clip's own track: its instrument
// and every effect after it, armed or not and with the transport stopped. The
// note goes in with the engine's injectLiveMidiMessage, which the engine lets
// only the message thread call - the knob-held DJ preview of 2026-10-10 ran
// the same way - and which lands a message-loop turn late on a block boundary.
// That jitter mattered for a deck playing a pattern and is nothing for one
// note under the pointer.
//
// One note sounds at a time: a new one ends the last. The note-off goes to the
// track the note-on went to, found again by its id, so a clip switched or a
// track deleted in between cannot leave a note hanging on the wrong one.

namespace rhino
{
namespace
{
const char* auditionSettingKey = "noteAudition";

te::AudioTrack* trackWithID(const juce::Array<te::AudioTrack*>& tracks, te::EditItemID id)
{
    for (auto* track : tracks)
        if (track != nullptr && track->itemID == id)
            return track;
    return nullptr;
}
}

// Off until switched on, and kept per machine, as the browser's preview is.
// The test runs never read or write it, so a developer's choice cannot decide
// whether the suite passes.
bool Session::readNoteAuditionPreference()
{
    if (isCommandLineTestMode())
        return false;
    juce::PropertiesFile properties(rhinoSettingsOptions());
    return properties.getBoolValue(auditionSettingKey, false);
}

void Session::setNoteAuditionEnabled(bool enabled)
{
    if (noteAudition == enabled)
        return;
    noteAudition = enabled;
    if (!enabled)
        stopNoteAudition();
    if (!isCommandLineTestMode())
    {
        juce::PropertiesFile properties(rhinoSettingsOptions());
        properties.setValue(auditionSettingKey, enabled);
        properties.saveIfNeeded();
    }
    sendSynchronousChangeMessage();
}

void Session::startNoteAudition(int pitch, int velocity)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (!noteAudition || patternClip == nullptr || pitch < 0 || pitch > 127)
        return;
    stopNoteAudition();
    auto* track = trackHoldingClip(*edit, pattern());
    if (track == nullptr)
        return;
    // A default source id: the note is no MPE voice.
    track->injectLiveMidiMessage(juce::MidiMessage::noteOn(1, pitch, static_cast<juce::uint8>(juce::jlimit(1, 127, velocity))),
                                 te::MidiMessageArray::MPESourceID {});
    auditionPitch = pitch;
    auditionTrack = track->itemID;
}

void Session::stopNoteAudition()
{
    if (auditionPitch < 0)
        return;
    if (auto* track = trackWithID(audioTracks(), auditionTrack))
        track->injectLiveMidiMessage(juce::MidiMessage::noteOff(1, auditionPitch), te::MidiMessageArray::MPESourceID {});
    auditionPitch = -1;
    auditionTrack = {};
}

}
