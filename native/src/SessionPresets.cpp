#include "SessionInternal.h"
#include <algorithm>
#include <set>

// Pattern presets and instrument selection.

namespace rhino
{

void Session::clearPattern()
{
    if (patternClip == nullptr || pattern().getSequence().getNumNotes() == 0) return;
    edit->getUndoManager().beginNewTransaction("Clear pattern");
    pattern().getSequence().removeAllNotes(&edit->getUndoManager());
    pattern().state.setProperty(starterPlaceholderID, true, &edit->getUndoManager());
    markModified();
    edit->getUndoManager().beginNewTransaction();
    sendSynchronousChangeMessage();
}

// Loads a preset into the clip the note editor has open, and its sound onto
// that clip's own track - the same work a preset dropped on that lane does.
void Session::applyPatternPreset(PatternPreset preset)
{
    if (patternClip == nullptr) return;
    const auto trackIndex = te::getAudioTracks(*edit).indexOf(dynamic_cast<te::AudioTrack*>(patternClip->getClipTrack()));
    if (trackIndex < 0) return;
    const auto data = presetPattern(preset);
    edit->getUndoManager().beginNewTransaction("Load " + data.name);
    if (preparePresetTrack(trackIndex, data).failed())
    {
        edit->getUndoManager().beginNewTransaction();
        return;
    }
    fillMidiClip(pattern(), data, edit->getUndoManager());
    markModified();
    edit->getUndoManager().beginNewTransaction();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
}

juce::Result Session::insertPatternPreset(PatternPreset preset, int trackIndex, double startSeconds)
{
    if (!std::isfinite(startSeconds) || startSeconds < 0.0)
        return juce::Result::fail("Invalid pattern drop position.");
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(trackIndex, tracks.size()))
        return juce::Result::fail("Drop clips on a track lane.");
    const auto data = presetPattern(preset);
    const auto start = tracktion::core::TimePosition::fromSeconds(startSeconds);
    const auto startBeat = edit->tempoSequence.toBeats(start).inBeats();
    const auto end = edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(startBeat + beatsPerBar()));
    edit->getUndoManager().beginNewTransaction("Add " + data.name);
    auto* track = tracks[trackIndex];
    if (const auto prepared = preparePresetTrack(trackIndex, data); prepared.failed())
        return prepared;
    auto clip = track->insertMIDIClip(data.name, {start, end}, nullptr);
    if (clip == nullptr)
        return juce::Result::fail("The pattern clip could not be added.");
    fillMidiClip(*clip, data, edit->getUndoManager());
    makeRoomForClip(*clip);
    refreshLoop();
    markModified();
    edit->getUndoManager().beginNewTransaction();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

bool Session::trackHasInstrument(int trackIndex) const
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(trackIndex, tracks.size())) return false;
    return trackInstrument(*tracks[trackIndex]) != nullptr;
}

// An empty one-bar MIDI clip at the position the user asked for. Only MIDI
// tracks hold one; an audio track takes recordings and files. A MIDI track
// that runs no instrument still takes clips - the notes are there waiting for
// one, which is what every other DAW does and what the forced choice at
// creation time promises.
juce::Result Session::createClip(int trackIndex, double startSeconds, te::EditItemID* created)
{
    if (!std::isfinite(startSeconds) || startSeconds < 0.0)
        return juce::Result::fail("Invalid clip position.");
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(trackIndex, tracks.size()))
        return juce::Result::fail("Select a track first.");
    if (isGroupBusTrack(trackIndex))
        return juce::Result::fail("A group track carries its members' audio, so it takes no clips.");
    if (trackType(trackIndex) != TrackType::midi)
        return juce::Result::fail("That is an audio track. Add a MIDI track, or drop an instrument here.");
    auto* track = tracks[trackIndex];
    const auto start = tracktion::core::TimePosition::fromSeconds(startSeconds);
    const auto startBeat = edit->tempoSequence.toBeats(start).inBeats();
    const auto end = edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(startBeat + beatsPerBar()));
    // A new document's Pattern 1 is a placeholder: empty, hidden from the
    // arrangement, and waiting to be told where it goes. Double-clicking a lane
    // is the user saying where, so it is carried there and revealed rather than
    // refusing as though a clip were in the way - which is what made the very
    // first track behave differently from every track added after it.
    te::MidiClip* placeholder = nullptr;
    for (auto* existing : track->getClips())
    {
        const auto range = existing->getPosition().time;
        if (range.getEnd() <= start || range.getStart() >= end)
            continue;
        auto* midi = dynamic_cast<te::MidiClip*>(existing);
        if (placeholder != nullptr || midi == nullptr || shouldShowClipInArrangement(*midi))
            return juce::Result::fail("There is already a clip here.");
        placeholder = midi;
    }
    edit->getUndoManager().beginNewTransaction("Add clip");
    if (placeholder != nullptr)
    {
        placeholder->setPosition({{start, end}, {}});
        placeholder->state.removeProperty(starterPlaceholderID, &edit->getUndoManager());
        patternClip = placeholder;
    }
    else
    {
        auto clip = track->insertMIDIClip("Clip", {start, end}, nullptr);
        if (clip == nullptr)
            return juce::Result::fail("The clip could not be created.");
        patternClip = clip.get();
    }
    patternClipID = patternClip->itemID;
    if (created != nullptr) *created = patternClipID;
    refreshLoop();
    edit->getUndoManager().beginNewTransaction();
    markModified();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

// The instrument belongs to the track the edited pattern sits on, so this is a
// lookup rather than a cached flag.
te::Plugin* Session::patternInstrument() const
{
    auto* track = patternClip != nullptr ? patternClip->getClipTrack() : nullptr;
    auto* audioTrack = dynamic_cast<te::AudioTrack*>(track);
    if (audioTrack == nullptr) return nullptr;
    return trackInstrument(*audioTrack);
}

te::Plugin* Session::patternInstrumentForTrack(int trackIndex) const
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(trackIndex, tracks.size())) return nullptr;
    return trackInstrument(*tracks[trackIndex]);
}

Session::Instrument Session::patternInstrumentKind() const
{
    if (auto* plugin = patternInstrument())
    {
        const auto type = plugin->getPluginType();
        if (type == DrumDevice::xmlTypeName) return Instrument::Drums;
        if (isForgePlugin(*plugin)) return Instrument::RhinoForge;
    }
    return Instrument::FourOsc;
}

bool Session::isPatternDrums() const
{
    return patternInstrumentKind() == Instrument::Drums;
}

// Puts a track into the state a preset expects: the right instrument and its
// patch. Shared by the timeline and clip-slot insertion paths so the two cannot
// drift apart, and the same for every track: none is special for being first.
juce::Result Session::preparePresetTrack(int trackIndex, const PresetPattern& data)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(trackIndex, tracks.size()))
        return juce::Result::fail("Drop clips on a track lane.");
    // A pattern is notes and the instrument that plays them, so it lands where
    // a clip and an instrument each land on their own.
    if (trackType(trackIndex) != TrackType::midi)
        return juce::Result::fail("That is an audio track. Drop patterns on a MIDI track instead.");
    auto* track = tracks[trackIndex];
    bool instrumentChanged = false;
    const auto result = switchTrackInstrument(*edit, *track,
                                              data.useDrums ? Instrument::Drums : Instrument::FourOsc,
                                              instrumentChanged);
    if (result.failed())
        return result;
    if (data.synthPatch != SynthPatch::Default)
        if (auto* fourOsc = findFourOsc(*track))
            applySynthPatch(data.synthPatch, *fourOsc, edit->getUndoManager());
    return juce::Result::ok();
}

juce::Result Session::selectPatternClip(te::EditItemID id)
{
    auto* midi = dynamic_cast<te::MidiClip*>(findClip(id));
    if (midi == nullptr) return juce::Result::fail("Select a MIDI clip to edit notes.");
    patternClip = midi;
    patternClipID = id;
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

}
