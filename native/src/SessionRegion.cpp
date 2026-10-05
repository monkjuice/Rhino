#include "SessionInternal.h"
#include <algorithm>
#include <cmath>
#include <optional>

// Region editing: the slice of the arrangement that copy, cut, paste, duplicate
// and delete all act on. A region is a span of time across a run of tracks -
// the rectangle the arrangement highlights - rather than a set of clips, so a
// clip crossing an edge is cut at that edge instead of being taken whole.
//
// What is copied is a snapshot rather than a list of clip ids. A cut deletes
// its sources, and so does an undo made between the copy and the paste, so a
// clipboard that only remembered where a clip was would paste nothing. Serves
// Arrangement, and Session::duplicateClip is the same operation on one clip.

namespace rhino
{
namespace
{
constexpr double regionTolerance = 1.0e-7;

// Touching an edge is not overlapping it: a region that ends exactly where the
// next clip starts must leave that clip alone, and a clip that ends exactly on
// the region start is outside it.
bool overlapsRegion(tracktion::core::TimeRange clip, double start, double end)
{
    return clip.getStart().inSeconds() < end - regionTolerance
        && clip.getEnd().inSeconds() > start + regionTolerance;
}

bool crossesEdge(tracktion::core::TimeRange clip, double edge)
{
    return clip.getStart().inSeconds() < edge - regionTolerance
        && clip.getEnd().inSeconds() > edge + regionTolerance;
}
}

Session::ClipRegion Session::copyClipRegion(double startSeconds, double endSeconds,
                                            int firstTrack, int lastTrack) const
{
    ClipRegion region;
    if (!std::isfinite(startSeconds) || !std::isfinite(endSeconds) || endSeconds <= startSeconds + regionTolerance)
        return region;
    const auto tracks = te::getAudioTracks(*edit);
    firstTrack = std::max(0, firstTrack);
    lastTrack = std::min(lastTrack, tracks.size() - 1);
    if (firstTrack > lastTrack)
        return region;
    // The rectangle, recorded before anything inside it is looked at: a lane
    // with nothing on it is still part of what was copied.
    region.spanSeconds = endSeconds - startSeconds;
    region.trackSpan = lastTrack - firstTrack;
    for (int track = firstTrack; track <= lastTrack; ++track)
        for (auto* clip : tracks[track]->getClips())
        {
            if (clip == nullptr || !shouldShowClipInArrangement(*clip))
                continue;
            const auto position = clip->getPosition();
            if (!overlapsRegion(position.time, startSeconds, endSeconds))
                continue;
            const auto clipStart = position.time.getStart().inSeconds();
            const auto visibleStart = std::max(clipStart, startSeconds);
            const auto visibleEnd = std::min(position.time.getEnd().inSeconds(), endSeconds);
            ClipSnapshot snapshot;
            snapshot.name = clip->getName();
            snapshot.colour = clipColour(*clip);
            snapshot.speed = clip->getSpeedRatio();
            snapshot.track = track - firstTrack;
            snapshot.start = visibleStart - startSeconds;
            snapshot.end = visibleEnd - startSeconds;
            // Clamping the left edge moves the same distance into the source,
            // exactly as dragging that edge does - see previewClipEdit.
            snapshot.offset = position.offset.inSeconds() + visibleStart - clipStart;
            if (auto* midi = dynamic_cast<te::MidiClip*>(clip))
            {
                snapshot.midi = true;
                if (const auto* instrument = carriedInstrument(*tracks[track]))
                    snapshot.instrumentId = instrument->id;
                // The whole sequence, not the part inside the region: a MIDI
                // clip is trimmed by its position and offset, so cropping the
                // notes as well would silence what a later trim should reveal.
                for (auto* note : midi->getSequence().getNotes())
                    snapshot.notes.push_back({note->getStartBeat().inBeats(), note->getLengthBeats().inBeats(),
                                              note->getNoteNumber(), note->getVelocity(), note->getColour()});
            }
            else if (dynamic_cast<te::WaveAudioClip*>(clip) != nullptr)
            {
                snapshot.sourceFile = clip->getSourceFileReference().getFile();
                if (snapshot.sourceFile == juce::File())
                    continue;
                // The clip's own mix and warp go with it, or a copy plays the
                // raw file at speed one instead of what the original plays.
                const auto mix = audioClipMix(clip->itemID);
                snapshot.gainDb = mix.gainDb;
                snapshot.pan = mix.pan;
                snapshot.pitchSemitones = mix.pitchSemitones;
                snapshot.fadeInSeconds = mix.fadeInSeconds;
                snapshot.fadeOutSeconds = mix.fadeOutSeconds;
                snapshot.muted = mix.muted;
                snapshot.reversed = mix.reversed;
                const auto warp = clipWarp(clip->itemID);
                snapshot.warped = warp.followsTempo;
                snapshot.warpMode = static_cast<int>(warp.mode);
                snapshot.warpBeats = warp.beats;
                snapshot.warpMarkers = warp.markersEnabled;
                for (const auto& marker : warp.markers)
                    snapshot.markers.emplace_back(marker.sourceSeconds, marker.warpSeconds);
            }
            else
            {
                continue;
            }
            region.clips.push_back(std::move(snapshot));
        }
    return region;
}

// Splitting at both edges first means the only question left is whether a whole
// clip is inside the region, which is also what leaves a clip spanning the
// region with a hole rather than losing it.
bool Session::clearClipRegionInEdit(double startSeconds, double endSeconds, int firstTrack, int lastTrack,
                                    const std::vector<te::EditItemID>& keep)
{
    const auto tracks = te::getAudioTracks(*edit);
    firstTrack = std::max(0, firstTrack);
    lastTrack = std::min(lastTrack, tracks.size() - 1);
    const auto kept = [&keep](const te::Clip& clip)
    {
        return std::find(keep.begin(), keep.end(), clip.itemID) != keep.end();
    };
    auto changed = false;
    for (int track = firstTrack; track <= lastTrack; ++track)
    {
        auto* clipTrack = tracks[track];
        // A hidden starter placeholder goes whole rather than being split into
        // two hidden fragments, neither of which anything could ever address.
        std::vector<te::Clip*> hidden;
        for (auto* clip : clipTrack->getClips())
            if (clip != nullptr && !kept(*clip) && !shouldShowClipInArrangement(*clip)
                && overlapsRegion(clip->getPosition().time, startSeconds, endSeconds))
                hidden.push_back(clip);
        for (auto* clip : hidden)
        {
            clip->removeFromParent();
            changed = true;
        }
        for (const auto edge : {startSeconds, endSeconds})
        {
            // Collected before anything is split: splitting inserts into the
            // array being walked.
            std::vector<te::Clip*> crossing;
            for (auto* clip : clipTrack->getClips())
                if (clip != nullptr && !kept(*clip) && crossesEdge(clip->getPosition().time, edge))
                    crossing.push_back(clip);
            for (auto* clip : crossing)
                if (clipTrack->splitClip(*clip, tracktion::core::TimePosition::fromSeconds(edge)) != nullptr)
                    changed = true;
        }
        std::vector<te::Clip*> inside;
        for (auto* clip : clipTrack->getClips())
            if (clip != nullptr && !kept(*clip) && overlapsRegion(clip->getPosition().time, startSeconds, endSeconds))
                inside.push_back(clip);
        for (auto* clip : inside)
        {
            clip->removeFromParent();
            changed = true;
        }
    }
    return changed;
}

// The clip that just arrived wins the ground it landed on. Splitting at its
// edges before deleting what is under it is what leaves the neighbours either
// side intact: a clip dropped into the middle of a long one cuts a hole and
// fills it, rather than replacing the whole thing.
void Session::makeRoomForClip(te::Clip& winner, const std::vector<te::EditItemID>& alsoKeep)
{
    const auto tracks = te::getAudioTracks(*edit);
    const auto track = tracks.indexOf(dynamic_cast<te::AudioTrack*>(winner.getClipTrack()));
    if (track < 0)
        return;
    auto keep = alsoKeep;
    keep.push_back(winner.itemID);
    const auto range = winner.getPosition().time;
    if (clearClipRegionInEdit(range.getStart().inSeconds(), range.getEnd().inSeconds(), track, track, keep))
        repairPatternClip();
}

juce::Result Session::clearClipRegion(double startSeconds, double endSeconds, int firstTrack, int lastTrack)
{
    if (!std::isfinite(startSeconds) || !std::isfinite(endSeconds) || endSeconds <= startSeconds + regionTolerance)
        return juce::Result::fail("Select a span of the timeline first.");
    if (firstTrack > lastTrack || firstTrack >= te::getAudioTracks(*edit).size())
        return juce::Result::fail("Select a track first.");
    edit->getUndoManager().beginNewTransaction("Delete time selection");
    if (!clearClipRegionInEdit(startSeconds, endSeconds, firstTrack, lastTrack))
    {
        edit->getUndoManager().beginNewTransaction();
        return juce::Result::ok();
    }
    repairPatternClip();
    refreshLoop();
    edit->getUndoManager().beginNewTransaction();
    markModified();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::pasteClipRegion(const ClipRegion& region, double destinationStart,
                                      int destinationTrack, std::vector<te::EditItemID>& pasted)
{
    pasted.clear();
    if (region.isEmpty())
        return juce::Result::fail("Copy one or more clips first.");
    if (!std::isfinite(destinationStart) || destinationStart < 0.0 || destinationTrack < 0)
        return juce::Result::fail("Choose a valid paste location.");

    // A clip lands on a lane of its own kind, so what each row of the region
    // needs is read off it before anything is created or cleared: a paste that
    // would put MIDI on an audio lane is refused whole rather than half done,
    // because clearing the destination has already happened by the time the
    // first clip is inserted.
    std::vector<std::optional<TrackType>> rowType(static_cast<size_t>(region.trackSpan) + 1);
    for (const auto& snapshot : region.clips)
        if (juce::isPositiveAndBelow(snapshot.track, static_cast<int>(rowType.size())))
            rowType[static_cast<size_t>(snapshot.track)] = snapshot.midi ? TrackType::midi : TrackType::audio;

    const auto trackCountBefore = te::getAudioTracks(*edit).size();
    for (size_t row = 0; row < rowType.size(); ++row)
    {
        // A row the paste will make is made as the kind that row wants, so
        // only the rows already there can be the wrong kind.
        const auto index = destinationTrack + static_cast<int>(row);
        if (!rowType[row].has_value() || index >= trackCountBefore)
            continue;
        if (isGroupBusTrack(index))
            return juce::Result::fail("A group track carries its members' audio, so it takes no clips.");
        if (trackType(index) != *rowType[row])
            return juce::Result::fail(*rowType[row] == TrackType::midi
                ? "That is an audio track. Paste MIDI clips on a MIDI track."
                : "That is a MIDI track. Paste audio clips on an audio track.");
    }

    edit->getUndoManager().beginNewTransaction("Paste clips");
    // A paste that fails part of the way through - a source file gone, an
    // instrument that will not load - takes back what it had already done,
    // the cleared destination and any lanes it made included.
    const auto rollBack = [this](juce::Result failure)
    {
        edit->getUndoManager().undoCurrentTransactionOnly();
        repairPatternClip();
        sendSynchronousChangeMessage();
        return failure;
    };
    auto addedLanes = false;
    while (te::getAudioTracks(*edit).size() < destinationTrack + region.trackSpan + 1)
    {
        const auto row = te::getAudioTracks(*edit).size() - destinationTrack;
        const auto type = juce::isPositiveAndBelow(row, static_cast<int>(rowType.size()))
                              ? rowType[static_cast<size_t>(row)].value_or(TrackType::audio)
                              : TrackType::audio;
        if (appendTrack(type) == nullptr)
            return rollBack(juce::Result::fail("Could not create a track for pasted clips."));
        addedLanes = true;
    }
    // A lane a paste made is a lane like any other from the start: its fader,
    // its scene slots and its routing, not at the next addTrack.
    if (addedLanes)
    {
        ensureTrackMixers();
        ensureSceneSlots();
        reconcileTrackGroups();
    }

    // Pasting replaces what it lands on, as it does in Live: what arrives is
    // what was copied rather than what was copied stacked on whatever was
    // already there. The whole rectangle is cleared, not only the spans the
    // clips occupy, so copying a lane that was empty empties the lane it lands
    // on - and so no insert can be undone by a later snapshot's clear.
    clearClipRegionInEdit(destinationStart, destinationStart + region.spanSeconds,
                          destinationTrack, destinationTrack + region.trackSpan);

    const auto tracks = te::getAudioTracks(*edit);
    for (const auto& snapshot : region.clips)
    {
        const auto targetIndex = destinationTrack + snapshot.track;
        auto* target = tracks[targetIndex];
        const auto start = destinationStart + snapshot.start;
        const tracktion::core::TimeRange range {tracktion::core::TimePosition::fromSeconds(start),
                                                tracktion::core::TimePosition::fromSeconds(start + snapshot.end - snapshot.start)};
        const auto offset = tracktion::core::TimeDuration::fromSeconds(std::max(0.0, snapshot.offset));
        te::Clip* copy = nullptr;
        if (snapshot.midi)
        {
            // A MIDI clip carries its instrument with it, the same way moving
            // one between tracks does - bypassed or not, so duplicating a clip
            // on its own track never swaps that track's instrument. A clip from
            // a track that played nothing changes nothing where it lands.
            if (const auto* instrument = snapshot.instrumentId.isEmpty() ? nullptr
                                                                          : DeviceCatalog::byId(snapshot.instrumentId))
            {
                bool instrumentChanged = false;
                const auto instrumentResult = switchTrackInstrument(*edit, *target, *instrument, instrumentChanged,
                                                                    forgeDescription ? &*forgeDescription : nullptr);
                if (instrumentResult.failed())
                    return rollBack(instrumentResult);
            }
            if (auto midiCopy = target->insertMIDIClip(snapshot.name, range, nullptr))
            {
                auto& sequence = midiCopy->getSequence();
                for (const auto& note : snapshot.notes)
                    sequence.addNote(note.pitch, tracktion::core::BeatPosition::fromBeats(note.startBeats),
                                     tracktion::core::BeatDuration::fromBeats(note.lengthBeats),
                                     note.velocity, note.colour, &edit->getUndoManager());
                midiCopy->setPosition({range, offset});
                copy = midiCopy.get();
            }
        }
        else if (snapshot.sourceFile.existsAsFile())
        {
            copy = target->insertWaveClip(snapshot.name, snapshot.sourceFile, {range, offset}, false).get();
            if (auto* audio = dynamic_cast<te::WaveAudioClip*>(copy))
            {
                // Warp or speed first, then the position re-asserted, because
                // both rescale what the clip covers; the fades last, because
                // they are limited by the length the clip ends up with.
                if (snapshot.warped)
                {
                    if (snapshot.warpBeats > 0.0)
                        audio->getLoopInfo().setNumBeats(snapshot.warpBeats);
                    applyWarpState(*audio, true, static_cast<WarpMode>(juce::jlimit(0, warpModeCount - 1,
                                                                                    snapshot.warpMode)));
                    if (snapshot.warpMarkers && snapshot.markers.size() >= 2)
                    {
                        auto& manager = audio->getWarpTimeManager();
                        audio->setWarpTime(true);
                        // Straightened, the manager keeps the two ends of the
                        // file; the copy's own markers go between them and the
                        // ends move to where the copy had them.
                        manager.removeAllMarkers();
                        for (size_t marker = 1; marker + 1 < snapshot.markers.size(); ++marker)
                            manager.insertMarker({tracktion::core::TimePosition::fromSeconds(snapshot.markers[marker].first),
                                                  tracktion::core::TimePosition::fromSeconds(snapshot.markers[marker].second)});
                        manager.moveMarker(0, tracktion::core::TimePosition::fromSeconds(snapshot.markers.front().second));
                        manager.moveMarker(manager.getMarkers().size() - 1,
                                           tracktion::core::TimePosition::fromSeconds(snapshot.markers.back().second));
                    }
                }
                else if (snapshot.speed > 0.0)
                {
                    audio->setSpeedRatio(snapshot.speed);
                }
                audio->setPosition({range, offset});
                audio->setGainDB(snapshot.gainDb);
                audio->setPan(snapshot.pan);
                audio->setPitchChange(snapshot.pitchSemitones);
                audio->setMuted(snapshot.muted);
                audio->setIsReversed(snapshot.reversed);
                audio->setFadeIn(tracktion::core::TimeDuration::fromSeconds(snapshot.fadeInSeconds));
                audio->setFadeOut(tracktion::core::TimeDuration::fromSeconds(snapshot.fadeOutSeconds));
            }
        }
        if (copy == nullptr)
            return rollBack(juce::Result::fail("The clip could not be pasted."));
        if (!snapshot.colour.isTransparent())
            setClipColour(*copy, snapshot.colour, &edit->getUndoManager());
        pasted.push_back(copy->itemID);
    }
    // Clearing the destination can have taken the clip the note editor is
    // pointed at, and pattern() dereferences that pointer. Repaired after the
    // inserts rather than before them, so a pasted MIDI clip is a candidate
    // instead of a starter clip being made and immediately buried.
    repairPatternClip();
    refreshLoop();
    edit->getUndoManager().beginNewTransaction();
    markModified();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

}
