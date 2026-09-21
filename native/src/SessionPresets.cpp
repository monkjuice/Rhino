#include "SessionInternal.h"
#include <algorithm>
#include <set>

// Pattern presets and instrument selection.

namespace rhino
{

void Session::clearPattern()
{
    if (pattern().getSequence().getNumNotes() == 0) return;
    edit->getUndoManager().beginNewTransaction("Clear pattern");
    pattern().getSequence().removeAllNotes(&edit->getUndoManager());
    pattern().state.setProperty(starterPlaceholderID, true, &edit->getUndoManager());
    markModified();
    edit->getUndoManager().beginNewTransaction();
    sendSynchronousChangeMessage();
}

void Session::applyPatternPreset(PatternPreset preset)
{
    const auto data = presetPattern(preset);
    edit->getUndoManager().beginNewTransaction("Load " + data.name);
    if (data.useRhinoWave)
    {
        bool instrumentChanged = false;
        juce::ignoreUnused(switchTrackInstrument(*edit, *te::getAudioTracks(*edit)[0], Instrument::RhinoWave, instrumentChanged));
        edit->state.setProperty("rhinoPatternInstrument", "wave", &edit->getUndoManager());
    }
    else
    {
        setPatternInstrument(data.useDrums);
    }
    if (data.synthPatch != SynthPatch::Default)
        if (auto* fourOsc = findFourOsc(*te::getAudioTracks(*edit)[0]))
            applySynthPatch(data.synthPatch, *fourOsc, edit->getUndoManager());
    if (data.useRhinoWave)
        if (auto* wave = findRhinoWave(*te::getAudioTracks(*edit)[0]))
            applyRhinoWavePatch(preset, *wave);
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
    if (const auto prepared = preparePresetTrack(trackIndex, data, preset); prepared.failed())
        return prepared;
    auto clip = track->insertMIDIClip(data.name, {start, end}, nullptr);
    if (clip == nullptr)
        return juce::Result::fail("The pattern clip could not be added.");
    clip->setColour(presetColour(preset));
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

// An empty one-bar MIDI clip at the position the user asked for. Only tracks
// with an instrument can hold one; an audio track takes recordings and files.
juce::Result Session::createClip(int trackIndex, double startSeconds, te::EditItemID* created)
{
    if (!std::isfinite(startSeconds) || startSeconds < 0.0)
        return juce::Result::fail("Invalid clip position.");
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(trackIndex, tracks.size()))
        return juce::Result::fail("Select a track first.");
    if (isGroupBusTrack(trackIndex))
        return juce::Result::fail("A group track carries its members' audio, so it takes no clips.");
    if (!trackHasInstrument(trackIndex))
        return juce::Result::fail("Drop an instrument on this track before adding clips to it.");
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
        placeholder->setColour(instrumentColour(activeTrackInstrument(*track)));
        patternClip = placeholder;
    }
    else
    {
        auto clip = track->insertMIDIClip("Clip", {start, end}, nullptr);
        if (clip == nullptr)
            return juce::Result::fail("The clip could not be created.");
        clip->setColour(instrumentColour(activeTrackInstrument(*track)));
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

void Session::setPatternInstrument(bool useDrums)
{
    // Never the bus a group at the top of the stack puts at index zero: an
    // instrument there has nothing to play and replaces the sum of every
    // member feeding it, which is a whole project reopening into silence.
    auto* track = patternTrackOf(*edit);
    if (track == nullptr) return;
    bool changed = false;
    juce::ignoreUnused(switchTrackInstrument(*edit, *track,
                                             useDrums ? Instrument::Drums : Instrument::FourOsc, changed));
    edit->state.setProperty("rhinoPatternInstrument", useDrums ? "drums" : "synth", &edit->getUndoManager());
}

// The instrument belongs to the track the edited pattern sits on, so this is a
// lookup rather than a cached flag.
te::Plugin* Session::patternInstrument() const
{
    auto* track = patternClip != nullptr ? patternClip->getClipTrack() : nullptr;
    auto* audioTrack = dynamic_cast<te::AudioTrack*>(track);
    if (audioTrack == nullptr)
        audioTrack = patternTrackOf(*edit);
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
        if (type == RhinoWaveDevice::xmlTypeName) return Instrument::RhinoWave;
        if (isForgePlugin(*plugin)) return Instrument::RhinoForge;
    }
    return Instrument::FourOsc;
}

bool Session::isPatternDrums() const
{
    return patternInstrumentKind() == Instrument::Drums;
}

// Puts a track into the state a preset expects: the right instrument, its
// patch, and the pattern-track bookkeeping track 0 carries. Shared by the
// timeline and clip-slot insertion paths so the two cannot drift apart.
juce::Result Session::preparePresetTrack(int trackIndex, const PresetPattern& data, PatternPreset preset)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(trackIndex, tracks.size()))
        return juce::Result::fail("Drop clips on a track lane.");
    auto* track = tracks[trackIndex];
    if (trackIndex == 0)
    {
        if (data.useRhinoWave)
        {
            bool instrumentChanged = false;
            const auto result = switchTrackInstrument(*edit, *track, Instrument::RhinoWave, instrumentChanged);
            if (result.failed())
                return result;
            edit->state.setProperty("rhinoPatternInstrument", "wave", &edit->getUndoManager());
        }
        else
        {
            setPatternInstrument(data.useDrums);
        }
    }
    else
    {
        bool instrumentChanged = false;
        const auto result = switchTrackInstrument(*edit, *track,
                                                  data.useDrums ? Instrument::Drums
                                                      : data.useRhinoWave ? Instrument::RhinoWave
                                                      : Instrument::FourOsc,
                                                  instrumentChanged);
        if (result.failed())
            return result;
    }
    if (data.synthPatch != SynthPatch::Default)
        if (auto* fourOsc = findFourOsc(*track))
            applySynthPatch(data.synthPatch, *fourOsc, edit->getUndoManager());
    if (data.useRhinoWave)
        if (auto* wave = findRhinoWave(*track))
            applyRhinoWavePatch(preset, *wave);
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
