#include "SessionInternal.h"
#include <algorithm>

// A track's clip slots: one per scene on every track, each holding at most one
// clip. They are the cells a DJ console's clips grid shows (DjClipGrid.cpp),
// the DJ view's own clips, apart from the arrangement's: a deck plays one
// cell's clip through the track's live chain. Nothing launches a slot: the
// booth starts its decks itself, and a merge keeps the slots out of its render
// (te::Renderer::ScopedClipSlotDisabler).
//
// A cell's clip reaches only the decks that play its track, so every edit here
// names the track it changed, and the other decks of a set are left alone.

namespace rhino
{
namespace
{
// A cell's clip loops, and a deck plays it round, so a preset's is one bar.
constexpr int presetSlotBars = 1;
}

// One guard for every way a slot is reached - reading, filling, copying. A
// group track carries the audio of the tracks under it, so it has no clips of
// its own.
te::ClipSlot* Session::clipSlotAt(int track, int scene) const
{
    const auto tracks = audioTracks();
    if (!juce::isPositiveAndBelow(track, tracks.size())) return nullptr;
    if (isGroupBusTrack(track)) return nullptr;
    const auto slots = tracks[track]->getClipSlotList().getClipSlots();
    if (!juce::isPositiveAndBelow(scene, slots.size())) return nullptr;
    return slots[scene];
}

void Session::ensureSceneSlots(int minimumScenes)
{
    auto& scenes = edit->getSceneList();
    const auto wanted = std::max(minimumScenes, scenes.getNumScenes());
    scenes.ensureNumberOfScenes(wanted);
    // A track restored from an older document, or added before the scene list
    // existed, can still be short of slots.
    for (auto* track : audioTracks())
        track->getClipSlotList().ensureNumberOfSlots(wanted);
}

int Session::sceneCount() const
{
    return edit->getSceneList().getNumScenes();
}

Session::SlotClip Session::slotClip(int track, int scene) const
{
    SlotClip info;
    auto* slot = clipSlotAt(track, scene);
    if (slot == nullptr) return info;
    auto* clip = slot->getClip();
    if (clip == nullptr) return info;
    info.hasClip = true;
    info.name = clip->getName();
    // Rhino's colour, not the engine's: te::Clip::getColour makes one up for
    // a clip nobody has coloured, and such a clip wears its track's.
    info.colour = clipColour(*clip);
    info.clipID = clip->itemID;
    info.isMidi = dynamic_cast<te::MidiClip*>(clip) != nullptr;
    return info;
}

juce::Result Session::deleteSlotClip(int track, int scene)
{
    auto* slot = clipSlotAt(track, scene);
    if (slot == nullptr)
        return juce::Result::fail("That clip slot does not exist.");
    auto* clip = slot->getClip();
    if (clip == nullptr)
        return juce::Result::fail("That slot is already empty.");
    edit->getUndoManager().beginNewTransaction("Delete slot clip");
    clip->removeFromParent();
    // The note editor may have been pointed at it, since a console opens a
    // slot clip there; left pointing, the next paint read a freed clip.
    repairPatternClip();
    edit->getUndoManager().beginNewTransaction();
    markClipsModified({track});
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::insertPatternPresetInSlot(PatternPreset preset, int track, int scene)
{
    if (!juce::isPositiveAndBelow(track, trackCount()) || isGroupBusTrack(track) || scene < 0)
        return juce::Result::fail("Drop clips on a cell.");
    const auto data = presetPattern(preset);
    edit->getUndoManager().beginNewTransaction("Add " + data.name + " to slot");
    auto devicesChanged = false;
    if (const auto prepared = preparePresetTrack(track, data, devicesChanged); prepared.failed())
        return prepared;
    // A drop on the + row makes its scene here, once nothing is left to
    // refuse it, so a refused drop leaves the grid as it was.
    if (scene >= sceneCount())
        ensureSceneSlots(scene + 1);
    auto* slot = clipSlotAt(track, scene);
    if (slot == nullptr)
        return juce::Result::fail("Drop clips on a cell.");
    // A pattern on a filled cell takes its clip's place. The engine's insert
    // would remove that clip itself, but the note editor may have it open, as
    // a console opens a cell's clip there, so it goes here with the repair.
    if (auto* existing = slot->getClip())
    {
        existing->removeFromParent();
        repairPatternClip();
    }
    const auto beats = static_cast<double>(presetSlotBars) * beatsPerBar();
    const auto end = edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(beats));
    auto clip = te::insertMIDIClip(*slot, data.name, {tracktion::core::TimePosition(), end});
    if (clip == nullptr)
        return juce::Result::fail("The pattern clip could not be added to that slot.");
    fillMidiClip(*clip, data, edit->getUndoManager());
    clip->setLoopRangeBeats({tracktion::core::BeatPosition(), tracktion::core::BeatPosition::fromBeats(beats)});
    edit->getUndoManager().beginNewTransaction();
    // A preset may switch the track's instrument, which the track's own
    // arrangement plays too: every deck on the track is behind, and no other,
    // on its devices only when the preset changed them.
    markModified(track, devicesChanged ? ModelChange::notes | ModelChange::devices : ModelChange::notes);
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

// Why a lane may not take a clip of this kind, or ok. The slot paths ask the
// same question the arrangement's drops, moves and pastes already ask, so a
// slot cannot become the way round the rule that a track's kind is fixed.
juce::Result Session::clipLaneRefusal(int track, bool midi) const
{
    if (isGroupBusTrack(track))
        return juce::Result::fail("A group track carries its members' audio, so it takes no clips.");
    if (trackType(track) != (midi ? TrackType::midi : TrackType::audio))
        return juce::Result::fail(midi ? "That is an audio track. Put MIDI clips on a MIDI track."
                                       : "That is a MIDI track. Put audio clips on an audio track.");
    return juce::Result::ok();
}

juce::Result Session::insertAudioFileInSlot(const juce::File& file, int track, int scene)
{
    if (!juce::isPositiveAndBelow(track, trackCount()) || scene < 0)
        return juce::Result::fail("Drop audio on a cell.");
    if (const auto refused = clipLaneRefusal(track, false); refused.failed())
        return refused;
    if (!file.existsAsFile())
        return juce::Result::fail("That audio file could not be found.");
    const te::AudioFile audioFile(engine, file);
    if (!audioFile.isValid())
        return juce::Result::fail("Rhino could not read " + file.getFileName() + ".");
    edit->getUndoManager().beginNewTransaction("Add audio to slot");
    // As for a pattern: the + row's scene is made once the drop is known to
    // land.
    if (scene >= sceneCount())
        ensureSceneSlots(scene + 1);
    auto* slot = clipSlotAt(track, scene);
    if (slot == nullptr)
        return juce::Result::fail("Drop audio on a cell.");
    auto clip = te::insertWaveClip(*slot, file.getFileNameWithoutExtension(), file,
                                   {{tracktion::core::TimePosition(),
                                     tracktion::core::TimePosition::fromSeconds(audioFile.getLength())}},
                                   te::DeleteExistingClips::yes);
    if (clip == nullptr)
        return juce::Result::fail("The audio clip could not be added to that slot.");
    // The clip the slot held is gone, and the note editor may have had it.
    repairPatternClip();
    edit->getUndoManager().beginNewTransaction();
    markModified(track, ModelChange::audioClips);
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}


// Crossing between the views.
//
// A cell's clip and a timeline clip are separate objects, so moving work
// between the DJ view and the arrangement means copying rather than revealing.
// These are the two directions across, and both copy: the original stays
// where it was.

int Session::firstFreeSlot(int track) const
{
    for (int scene = 0; scene < sceneCount(); ++scene)
        if (auto* slot = clipSlotAt(track, scene))
            if (slot->getClip() == nullptr)
                return scene;
    return -1;
}

// Shared by both directions: rebuild a clip inside a new owner. Wave clips are re-inserted from the same source
// file rather than copying media, and MIDI clips clone their sequence. The
// copy carries the source's own colour, or none, as Rhino records it.
te::Clip* copyClipInto(te::ClipOwner& destination, te::Clip& source, tracktion::core::TimeRange range,
                       const juce::String& name)
{
    const auto offset = source.getPosition().offset;
    if (auto* audio = dynamic_cast<te::WaveAudioClip*>(&source))
    {
        auto copy = te::insertWaveClip(destination, name, audio->getSourceFileReference().getFile(),
                                       {range, offset}, te::DeleteExistingClips::no);
        if (copy != nullptr)
            setClipColour(*copy, Session::clipColour(source), nullptr);
        return copy.get();
    }
    if (auto* midi = dynamic_cast<te::MidiClip*>(&source))
    {
        auto copy = te::insertMIDIClip(destination, name, range);
        if (copy == nullptr)
            return nullptr;
        copy->cloneFrom(midi);
        // The clone carries the source's name across; the name asked for wins.
        copy->setName(name);
        copy->setPosition({range, offset});
        setClipColour(*copy, Session::clipColour(source), nullptr);
        return copy.get();
    }
    return nullptr;
}

// A name no other clip on the track wears, in its slots or on its timeline:
// a copy of "Clip 1" beside it becomes "Clip 2", a copy of "Break" becomes
// "Break 2", and a trailing number counts on from where it stood.
juce::String uniqueClipNameOnTrack(te::AudioTrack& track, const juce::String& wanted)
{
    juce::StringArray taken;
    for (auto* clip : track.getClips())
        taken.add(clip->getName());
    for (auto* slot : track.getClipSlotList().getClipSlots())
        if (auto* clip = slot->getClip())
            taken.add(clip->getName());
    if (!taken.contains(wanted))
        return wanted;
    auto stem = wanted.trimEnd();
    int number = 1;
    const auto digits = stem.length() - stem.trimCharactersAtEnd("0123456789").length();
    if (digits > 0 && digits < stem.length())
    {
        number = stem.getLastCharacters(digits).getIntValue();
        stem = stem.dropLastCharacters(digits).trimEnd();
    }
    for (int n = number + 1; n < number + 1000; ++n)
        if (const auto candidate = stem + " " + juce::String(n); !taken.contains(candidate))
            return candidate;
    return wanted;
}

juce::Result Session::copySlotClipToArrangement(int track, int scene, double startSeconds, int targetTrack)
{
    auto* slot = clipSlotAt(track, scene);
    if (slot == nullptr)
        return juce::Result::fail("That clip slot does not exist.");
    auto* source = slot->getClip();
    if (source == nullptr)
        return juce::Result::fail("That slot is empty.");
    const auto tracks = audioTracks();
    const auto lane = targetTrack < 0 ? track : targetTrack;
    if (!juce::isPositiveAndBelow(lane, tracks.size()))
        return juce::Result::fail("That track does not exist.");
    if (const auto refused = clipLaneRefusal(lane, dynamic_cast<te::MidiClip*>(source) != nullptr);
        refused.failed())
        return refused;
    const auto start = tracktion::core::TimePosition::fromSeconds(std::max(0.0, startSeconds));
    const auto range = tracktion::core::TimeRange(start, start + source->getPosition().time.getLength());
    edit->getUndoManager().beginNewTransaction("Copy clip to arrangement");
    auto* copy = copyClipInto(*tracks[lane], *source, range, source->getName());
    if (copy == nullptr)
        return juce::Result::fail("That clip could not be copied to the arrangement.");
    // On the timeline a clip occupies its own span rather than repeating, so
    // the slot clip's loop is dropped in the copy.
    copy->setLoopRangeBeats({});
    copy->state.removeProperty(starterPlaceholderID, &edit->getUndoManager());
    // It lands on a timeline that may already have something there, and the
    // clip that arrives is the one that wins that ground.
    makeRoomForClip(*copy);
    refreshLoop();
    edit->getUndoManager().beginNewTransaction();
    // The cell is untouched; the lane's arrangement is what changed.
    markClipsModified({lane});
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::copyClipToSlot(te::EditItemID id, int scene)
{
    auto* source = findClip(id);
    if (source == nullptr)
        return juce::Result::fail("Select a clip to copy.");
    // The copy stays on the clip's own track.
    auto* clipTrack = source->getClipTrack();
    const auto tracks = audioTracks();
    int track = -1;
    for (int index = 0; index < tracks.size(); ++index)
        if (clipTrack != nullptr && tracks[index] == clipTrack)
            track = index;
    if (track < 0)
        return juce::Result::fail("That clip is not on a track.");
    return copyClipToTrackSlot(id, track, scene);
}

// The copy into any track's slot, which a console's grid takes a clip from
// the timeline or from another console by; the lane rule holds here as it
// does for a drop on a lane.
juce::Result Session::copyClipToTrackSlot(te::EditItemID id, int track, int scene)
{
    auto* source = findClip(id);
    if (source == nullptr)
        return juce::Result::fail("Select a clip to copy.");
    const auto tracks = audioTracks();
    if (!juce::isPositiveAndBelow(track, tracks.size()))
        return juce::Result::fail("That track does not exist.");
    if (const auto refused = clipLaneRefusal(track, dynamic_cast<te::MidiClip*>(source) != nullptr); refused.failed())
        return refused;
    if (scene < 0)
    {
        scene = firstFreeSlot(track);
        if (scene < 0)
            scene = sceneCount();
    }
    edit->getUndoManager().beginNewTransaction("Copy clip to cell");
    if (scene >= sceneCount())
        ensureSceneSlots(scene + 1);
    auto* slot = clipSlotAt(track, scene);
    if (slot == nullptr)
        return juce::Result::fail("That clip slot does not exist.");
    if (auto* existing = slot->getClip())
    {
        existing->removeFromParent();
        repairPatternClip();
    }
    const auto length = source->getPosition().time.getLength();
    // A name of its own beside the clip it was copied from.
    const auto name = uniqueClipNameOnTrack(*tracks[track], source->getName());
    auto* copy = copyClipInto(*slot, *source, {tracktion::core::TimePosition(),
                                               tracktion::core::TimePosition() + length}, name);
    if (copy == nullptr)
        return juce::Result::fail("That clip could not be copied to a slot.");
    // A cell's clip loops, so the copy loops over its own length.
    const auto beats = edit->tempoSequence.toBeats(tracktion::core::TimePosition() + length).inBeats();
    copy->setLoopRangeBeats({tracktion::core::BeatPosition(), tracktion::core::BeatPosition::fromBeats(beats)});
    copy->state.removeProperty(starterPlaceholderID, &edit->getUndoManager());
    edit->getUndoManager().beginNewTransaction();
    markClipsModified({track});
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

// An empty one-bar MIDI clip in a slot, named for its cell. An audio track's
// slot takes a file instead, and the message says so rather than naming the
// lane rule.
juce::Result Session::createSlotClip(int track, int scene)
{
    const auto tracks = audioTracks();
    if (!juce::isPositiveAndBelow(track, tracks.size()) || scene < 0)
        return juce::Result::fail("That clip slot does not exist.");
    if (const auto refused = clipLaneRefusal(track, true); refused.failed())
        return juce::Result::fail(!isGroupBusTrack(track) && trackType(track) == TrackType::audio
                                      ? "An audio track's cell takes a sample or an audio clip: drop one on it."
                                      : refused.getErrorMessage());
    edit->getUndoManager().beginNewTransaction("New clip in slot");
    if (scene >= sceneCount())
        ensureSceneSlots(scene + 1);
    auto* slot = clipSlotAt(track, scene);
    if (slot == nullptr)
        return juce::Result::fail("That clip slot does not exist.");
    if (slot->getClip() != nullptr)
        return juce::Result::fail("That cell already holds a clip.");
    const auto beats = beatsPerBar();
    const auto end = edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(beats));
    const auto name = uniqueClipNameOnTrack(*tracks[track], "Clip " + juce::String(scene + 1));
    auto clip = te::insertMIDIClip(*slot, name, {tracktion::core::TimePosition(), end});
    if (clip == nullptr)
        return juce::Result::fail("The clip could not be added to that slot.");
    clip->setLoopRangeBeats({tracktion::core::BeatPosition(), tracktion::core::BeatPosition::fromBeats(beats)});
    edit->getUndoManager().beginNewTransaction();
    markModified(track, ModelChange::notes);
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

}
