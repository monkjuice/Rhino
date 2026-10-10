#include "SessionInternal.h"
#include <algorithm>
#include <set>

// Arrangement clip lookup and editing, and track mute/solo. Serves Arrangement.

namespace rhino
{

// On a track's timeline first, then in its clip slots: a slot clip opens in
// the same editors once a console opens it, and the engine's own lookup
// reads the timeline only.
te::Clip* Session::findClip(te::EditItemID id) const
{
    const auto tracks = te::getAudioTracks(*edit);
    for (auto* track : tracks)
        if (auto* clip = track->findClipForID(id))
            return clip;
    for (auto* track : tracks)
        for (auto* slot : track->getClipSlotList().getClipSlots())
            if (auto* clip = slot->getClip(); clip != nullptr && clip->itemID == id)
                return clip;
    return nullptr;
}

te::WaveAudioClip* Session::findAudioClip(te::EditItemID id) const
{
    return dynamic_cast<te::WaveAudioClip*>(findClip(id));
}

bool Session::shouldShowClipInArrangement(te::Clip& clip) const
{
    auto* midi = dynamic_cast<te::MidiClip*>(&clip);
    if (midi == nullptr)
        return true;
    return !static_cast<bool>(clip.state.getProperty(starterPlaceholderID, false))
        || midi->getSequence().getNumNotes() > 0;
}

namespace
{
// A clip that changed lanes needs a new playback graph, not a restart of the
// old one: the old graph has no node for it on the lane it arrived on.
void resumePlaybackAfterClipEdit(te::Edit& edit, bool laneChanged, bool wasPlaying, bool hadPlaybackContext)
{
    auto& transport = edit.getTransport();
    if (laneChanged && (wasPlaying || hadPlaybackContext))
    {
        transport.freePlaybackContext();
        transport.ensureContextAllocated(true);
        if (wasPlaying)
            transport.play(true);
    }
    else if (wasPlaying)
    {
        edit.restartPlayback();
    }
}
}

juce::Result Session::editClipInEdit(te::EditItemID id, ClipGeometry next, ClipGesture gesture, int targetTrack,
                                     const std::vector<te::EditItemID>& movingWith, bool& trackChanged,
                                     bool& changed)
{
    trackChanged = false;
    changed = false;
    auto* clip = findClip(id);
    if (!clip) return juce::Result::fail("Select a clip first.");
    if (!std::isfinite(next.start) || !std::isfinite(next.end) || !std::isfinite(next.offset)
        || next.start < 0.0 || next.end <= next.start || next.offset < -1.0e-8
        || next.end > te::Edit::getMaximumEditEnd().inSeconds())
        return juce::Result::fail("Invalid clip position.");
    const auto tracks = te::getAudioTracks(*edit);
    auto* oldTrack = clip->getClipTrack();
    const auto oldTrackIndex = tracks.indexOf(dynamic_cast<te::AudioTrack*>(oldTrack));
    const auto movingMidi = dynamic_cast<te::MidiClip*>(clip) != nullptr;
    if (targetTrack < 0 || gesture != ClipGesture::move)
        targetTrack = oldTrackIndex;
    if (targetTrack < 0 || targetTrack > tracks.size())
        return juce::Result::fail("Drop the clip on a track lane.");
    // A clip only ever lands on a lane of its own kind. Dropping one on the
    // wrong lane used to drag the lane's kind along with it - a MIDI clip
    // carried its instrument onto an audio track and silently made it MIDI.
    if (targetTrack != oldTrackIndex && targetTrack < tracks.size())
    {
        if (isGroupBusTrack(targetTrack))
            return juce::Result::fail("A group track carries its members' audio, so it takes no clips.");
        if (trackType(targetTrack) != (movingMidi ? TrackType::midi : TrackType::audio))
            return juce::Result::fail(movingMidi
                ? "That is an audio track. Move MIDI clips to a MIDI track."
                : "That is a MIDI track. Move audio clips to an audio track.");
    }
    const auto old = clip->getPosition();
    if (std::abs(old.time.getStart().inSeconds() - next.start) < 1.0e-8
        && std::abs(old.time.getEnd().inSeconds() - next.end) < 1.0e-8
        && std::abs(old.offset.inSeconds() - next.offset) < 1.0e-8
        && targetTrack == oldTrackIndex)
        return juce::Result::ok();
    if (targetTrack == tracks.size())
    {
        // Dragged off the bottom of the stack: the new lane is made for the
        // clip that is arriving, so a MIDI clip gets a MIDI track.
        if (appendTrack(movingMidi ? TrackType::midi : TrackType::audio) == nullptr)
            return juce::Result::fail("Could not create a track for the moved clip.");
        targetTrack = tracks.size();
        // A lane made by a drop is a lane like any other from the start: its
        // fader, its scene slots and its routing, not at the next addTrack.
        ensureTrackMixers();
        ensureSceneSlots();
        reconcileTrackGroups();
    }
    const auto refreshedTracks = te::getAudioTracks(*edit);
    if (targetTrack != oldTrackIndex)
    {
        auto* target = refreshedTracks[targetTrack];
        // A MIDI clip brings its instrument with it, bypassed or not. A clip
        // from a track that plays nothing changes nothing where it lands.
        if (const auto* instrument = movingMidi && oldTrackIndex >= 0 ? carriedInstrument(*tracks[oldTrackIndex])
                                                                      : nullptr)
        {
            bool instrumentChanged = false;
            const auto result = switchTrackInstrument(*edit, *target, *instrument, instrumentChanged,
                                                      forgeDescription ? &*forgeDescription : nullptr);
            if (result.failed())
                return result;
            // A drum clip brings its sounds too: a Drum Rack that arrives
            // blank takes the kit of the one the clip came from.
            if (const auto kit = drumKitOf(*tracks[oldTrackIndex]))
                fillBlankDrumRack(*target, *kit);
        }
        if (!clip->moveTo(*target))
            return juce::Result::fail("The clip could not be moved to that track.");
        trackChanged = true;
    }
    clip->setPosition({{tracktion::core::TimePosition::fromSeconds(next.start),
                       tracktion::core::TimePosition::fromSeconds(next.end)},
                       tracktion::core::TimeDuration::fromSeconds(std::max(0.0, next.offset))});
    // Whatever it landed on gives way, and the clips still travelling with it
    // are spared: a run carried one beat to the right must not have its
    // leading clip delete the one behind it.
    makeRoomForClip(*clip, movingWith);
    changed = true;
    return juce::Result::ok();
}

juce::Result Session::editClip(te::EditItemID id, ClipGeometry next, ClipGesture gesture, int targetTrack,
                               const std::vector<te::EditItemID>& movingWith)
{
    auto& transport = edit->getTransport();
    const auto wasPlaying = transport.isPlaying();
    const auto hadPlaybackContext = transport.isPlayContextActive();
    auto& undoManager = edit->getUndoManager();
    undoManager.beginNewTransaction(gesture == ClipGesture::move ? "Move clip" : "Trim clip");
    bool trackChanged = false, changed = false;
    const auto result = editClipInEdit(id, next, gesture, targetTrack, movingWith, trackChanged, changed);
    if (result.failed())
    {
        // Whatever part of the edit had happened is taken back, so a refusal
        // halfway through - a lane made, an instrument switched - leaves the
        // document as it was rather than half done.
        undoManager.undoCurrentTransactionOnly();
        repairPatternClip();
        return result;
    }
    // A press that never moved the clip is not an edit: nothing to mark,
    // nothing to restart, nothing to announce.
    if (!changed)
    {
        undoManager.beginNewTransaction();
        return juce::Result::ok();
    }
    refreshLoop();
    undoManager.beginNewTransaction();
    markModified();
    resumePlaybackAfterClipEdit(*edit, trackChanged, wasPlaying, hadPlaybackContext);
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::moveClips(const std::vector<ClipMove>& moves)
{
    if (moves.empty())
        return juce::Result::ok();
    if (moves.size() == 1)
        return editClip(moves.front().id, moves.front().position, ClipGesture::move, moves.front().track);
    std::vector<te::EditItemID> travelling;
    for (const auto& move : moves)
        travelling.push_back(move.id);
    auto& transport = edit->getTransport();
    const auto wasPlaying = transport.isPlaying();
    const auto hadPlaybackContext = transport.isPlayContextActive();
    auto& undoManager = edit->getUndoManager();
    undoManager.beginNewTransaction("Move clips");
    auto anyTrackChanged = false, anyChanged = false;
    for (const auto& move : moves)
    {
        bool trackChanged = false, changed = false;
        const auto result = editClipInEdit(move.id, move.position, ClipGesture::move, move.track, travelling,
                                           trackChanged, changed);
        if (result.failed())
        {
            undoManager.undoCurrentTransactionOnly();
            repairPatternClip();
            sendSynchronousChangeMessage();
            return result;
        }
        anyTrackChanged = anyTrackChanged || trackChanged;
        anyChanged = anyChanged || changed;
    }
    if (!anyChanged)
    {
        undoManager.beginNewTransaction();
        return juce::Result::ok();
    }
    refreshLoop();
    undoManager.beginNewTransaction();
    markModified();
    resumePlaybackAfterClipEdit(*edit, anyTrackChanged, wasPlaying, hadPlaybackContext);
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::splitClips(const std::vector<te::EditItemID>& ids, double splitTimeSeconds,
                                 int* splitCount)
{
    if (splitCount != nullptr) *splitCount = 0;
    if (!std::isfinite(splitTimeSeconds)) return juce::Result::fail("Invalid split position.");
    // Gathered before anything is cut: splitting inserts into the track's clip
    // array, and a clip's own bounds have to be read before its neighbour moves.
    std::vector<te::Clip*> cutting;
    for (const auto id : ids)
        if (auto* clip = findClip(id))
        {
            const auto time = clip->getPosition().time;
            if (canSplitClipAt({time.getStart().inSeconds(), time.getEnd().inSeconds()}, splitTimeSeconds))
                cutting.push_back(clip);
        }
    if (cutting.empty())
        return juce::Result::fail("Move the line inside the clip before splitting.");

    const auto split = tracktion::core::TimePosition::fromSeconds(splitTimeSeconds);
    edit->getUndoManager().beginNewTransaction(cutting.size() == 1 ? "Split clip" : "Split clips");
    auto cut = 0;
    for (auto* clip : cutting)
    {
        auto* track = clip->getClipTrack();
        if (track != nullptr && track->splitClip(*clip, split) != nullptr)
            ++cut;
    }
    if (cut == 0)
        return juce::Result::fail("The right-hand split clip could not be created.");
    if (splitCount != nullptr) *splitCount = cut;
    refreshLoop();
    edit->getUndoManager().beginNewTransaction();
    markModified();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

// One clip, for the callers that already know which one they mean - the audio
// editor is looking at it, and nothing else is in question.
juce::Result Session::splitClip(te::EditItemID id, double splitTimeSeconds)
{
    if (findClip(id) == nullptr) return juce::Result::fail("Select a clip to split.");
    return splitClips({id}, splitTimeSeconds);
}

// One clip is the smallest region there is, so duplicating it is the region
// command applied to its own span on its own track: the copy lands flush
// against its end, replacing whatever was sitting there.
juce::Result Session::duplicateClip(te::EditItemID id)
{
    auto* clip = findClip(id);
    if (!clip) return juce::Result::fail("Select a clip to duplicate.");
    const auto tracks = te::getAudioTracks(*edit);
    const auto track = tracks.indexOf(dynamic_cast<te::AudioTrack*>(clip->getClipTrack()));
    if (track < 0) return juce::Result::fail("The selected clip is not on a track.");
    const auto time = clip->getPosition().time;
    const auto region = copyClipRegion(time.getStart().inSeconds(), time.getEnd().inSeconds(), track, track);
    if (region.clips.empty()) return juce::Result::fail("The duplicate clip could not be created.");
    std::vector<te::EditItemID> pasted;
    return pasteClipRegion(region, time.getEnd().inSeconds(), track, pasted);
}

// Every path that can take the clip the note editor is pointed at ends here:
// deleting clips or a track, undo and redo, and reopening. The clip is found
// again by id first, because undo rebuilds clips from their state and the id
// outlives the object. Failing that the editor falls back to the first MIDI
// track - never an audio track, which may not hold a MIDI clip - and to a
// hidden starter clip there if it has none. A document with no MIDI track
// leaves the editor with no clip, which hasPatternClip() reports.
void Session::repairPatternClip()
{
    if (auto* midi = dynamic_cast<te::MidiClip*>(findClip(patternClipID)))
    {
        patternClip = midi;
        return;
    }
    patternClip = nullptr;
    patternClipID = {};
    auto* track = firstMidiTrackOf(*edit);
    if (track == nullptr)
        return;
    for (auto* existing : track->getClips())
        if (auto* midi = dynamic_cast<te::MidiClip*>(existing))
        {
            patternClip = midi;
            patternClipID = midi->itemID;
            return;
        }
    const auto end = edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(beatsPerBar()));
    patternClip = track->insertMIDIClip("Pattern 1", {{}, end}, nullptr).get();
    if (patternClip != nullptr)
    {
        patternClip->state.setProperty(starterPlaceholderID, true, nullptr);
        patternClipID = patternClip->itemID;
    }
}

void Session::deleteClip(te::EditItemID id)
{
    deleteClips({id});
}

void Session::deleteClips(const std::vector<te::EditItemID>& ids)
{
    auto found = 0;
    for (const auto id : ids)
        if (findClip(id) != nullptr)
            ++found;
    if (found == 0)
        return;
    edit->getUndoManager().beginNewTransaction(found == 1 ? "Delete clip" : "Delete clips");
    // Found again by id each time: removing one clip rebuilds its track's clip
    // list, and a pointer gathered before that may not survive it.
    for (const auto id : ids)
        if (auto* clip = findClip(id))
            clip->removeFromParent();
    repairPatternClip();
    refreshLoop();
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
}

juce::Colour Session::clipColour(const te::Clip& clip)
{
    const auto own = clip.state.getProperty(clipColourID).toString();
    return own.isEmpty() ? juce::Colour() : juce::Colour::fromString(own);
}

void setClipColour(te::Clip& clip, juce::Colour colour, juce::UndoManager* undoManager)
{
    if (colour.isTransparent())
        clip.state.removeProperty(clipColourID, undoManager);
    else
        clip.state.setProperty(clipColourID, colour.toString(), undoManager);
}

juce::Result Session::cycleClipColour(te::EditItemID id)
{
    auto* clip = findClip(id);
    if (clip == nullptr)
        return juce::Result::fail("Select a clip first.");
    edit->getUndoManager().beginNewTransaction("Color clip");
    setClipColour(*clip, nextClipColour(clipColour(*clip)), &edit->getUndoManager());
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

int Session::clipPluginCount(te::EditItemID id) const
{
    auto* clip = findClip(id);
    return clip != nullptr ? clipPluginCount(*clip) : 0;
}

int Session::clipPluginCount(te::Clip& clip)
{
    auto* audio = dynamic_cast<te::AudioClipBase*>(&clip);
    if (audio == nullptr || audio->getPluginList() == nullptr)
        return 0;
    return audio->getPluginList()->size();
}

void Session::toggleTrackMute(int track)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size())) return;
    edit->getUndoManager().beginNewTransaction("Track mute");
    tracks[track]->state.setProperty(te::IDs::mute, !tracks[track]->isMuted(false), &edit->getUndoManager());
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
}

void Session::toggleTrackSolo(int track)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size())) return;
    edit->getUndoManager().beginNewTransaction("Track solo");
    tracks[track]->state.setProperty(te::IDs::solo, !tracks[track]->isSolo(false), &edit->getUndoManager());
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
}

}
