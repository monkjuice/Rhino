#include "SessionInternal.h"
#include <algorithm>
#include <cmath>
#include <optional>

// Moving a region: the selection carried by a clip header, and the arrow keys
// nudging one. The region's other edits - copy, clear and paste - are in
// SessionRegion.cpp; this is the same class in a second translation unit.
//
// The clips the region runs across are cut at its edges first, by the floor a
// copy and a clear keep (cutClipByRegion, model/ClipGeometry.h), so what
// travels is whole clips; then the destination gives way as it does to a
// paste, the whole rectangle, and the pieces land through the same edit a
// dragged clip makes (moveClipsInEdit). Dragging a selection and cutting and
// pasting it are therefore the same edit, except that the clips keep their
// identities.

namespace rhino
{

juce::Result Session::moveClipRegion(double startSeconds, double endSeconds, int firstTrack, int lastTrack,
                                     double destinationStart, int destinationTrack,
                                     std::vector<te::EditItemID>* moved)
{
    if (moved != nullptr)
        moved->clear();
    if (!std::isfinite(startSeconds) || !std::isfinite(endSeconds) || endSeconds <= startSeconds + regionTolerance)
        return juce::Result::fail("Select a span of the timeline first.");
    if (!std::isfinite(destinationStart) || destinationStart < 0.0 || destinationTrack < 0)
        return juce::Result::fail("Drop the selection on a track lane.");
    // The returns are past the last lane, as for a paste: carried onto them,
    // the material lands on lanes made for it above them.
    destinationTrack = std::min(destinationTrack, firstReturnTrack());
    const auto tracks = audioTracks();
    const auto lanesBefore = firstReturnTrack();
    firstTrack = std::max(0, firstTrack);
    lastTrack = std::min(lastTrack, tracks.size() - 1);
    if (firstTrack > lastTrack)
        return juce::Result::fail("Select a track first.");
    const auto timeDelta = destinationStart - startSeconds;
    const auto trackDelta = destinationTrack - firstTrack;
    if (std::abs(timeDelta) < regionTolerance && trackDelta == 0)
        return juce::Result::ok();

    // What each row carries, read before anything is cut: the clips the
    // region runs across, each with the cut the region makes in it, and the
    // kind of lane each row needs. A track holds one kind of clip, so a row
    // is one kind or empty.
    struct Carried
    {
        te::Clip* clip = nullptr;
        RegionCut cut;
        int track = 0;
    };
    std::vector<Carried> carried;
    std::vector<std::optional<TrackType>> rowType(static_cast<size_t>(lastTrack - firstTrack) + 1);
    for (int track = firstTrack; track <= lastTrack; ++track)
        for (auto* clip : tracks[track]->getClips())
        {
            if (clip == nullptr || !shouldShowClipInArrangement(*clip))
                continue;
            const auto position = clip->getPosition();
            const auto cut = cutClipByRegion({position.time.getStart().inSeconds(), position.time.getEnd().inSeconds(),
                                              position.offset.inSeconds()},
                                             startSeconds, endSeconds);
            if (!cut.inside)
                continue;
            carried.push_back({clip, cut, track});
            rowType[static_cast<size_t>(track - firstTrack)] = dynamic_cast<te::MidiClip*>(clip) != nullptr
                                                                   ? TrackType::midi : TrackType::audio;
        }
    if (carried.empty())
        return juce::Result::ok();
    // Every row is checked before anything is cut, so a refused move leaves
    // the document exactly as it was. The same refusals a paste makes: a row
    // the move will make is made as the kind the row wants, so only the rows
    // already there can be the wrong kind.
    auto lastCarriedRow = 0;
    for (size_t row = 0; row < rowType.size(); ++row)
    {
        if (!rowType[row].has_value())
            continue;
        lastCarriedRow = static_cast<int>(row);
        const auto index = destinationTrack + static_cast<int>(row);
        if (index >= lanesBefore)
            continue;
        if (carriesOtherTracks(index))
            return juce::Result::fail(noClipsReason(index));
        if (trackType(index) != *rowType[row])
            return juce::Result::fail(*rowType[row] == TrackType::midi
                ? "That is an audio track. Move MIDI clips to a MIDI track."
                : "That is a MIDI track. Move audio clips to an audio track.");
    }

    auto& transport = edit->getTransport();
    const auto wasPlaying = transport.isPlaying();
    const auto hadPlaybackContext = transport.isPlayContextActive();
    auto& undoManager = edit->getUndoManager();
    undoManager.beginNewTransaction("Move selection");
    const auto rollBack = [this, &undoManager](juce::Result failure)
    {
        undoManager.undoCurrentTransactionOnly();
        repairPatternClip(false);
        sendSynchronousChangeMessage();
        return failure;
    };
    // Lanes for the rows that reach past the last track, through the last row
    // that carries something; a row between them that carries nothing is
    // made as a paste makes one, so the rows keep their distances.
    auto addedLanes = false;
    while (firstReturnTrack() < destinationTrack + lastCarriedRow + 1)
    {
        const auto row = firstReturnTrack() - destinationTrack;
        const auto type = juce::isPositiveAndBelow(row, static_cast<int>(rowType.size()))
                              ? rowType[static_cast<size_t>(row)].value_or(TrackType::audio)
                              : TrackType::audio;
        if (appendTrack(type) == nullptr)
            return rollBack(juce::Result::fail("Could not create a track for the moved clips."));
        addedLanes = true;
    }
    if (addedLanes)
    {
        ensureTrackMixers();
        ensureSceneSlots();
        reconcileTrackGroups();
    }

    // The cuts. A clip crossing an edge by at least the floor is split there
    // and the part inside is what travels; the piece's position is read back
    // after the cut rather than worked out, so a reversed clip's window is
    // whatever the engine's split left it.
    std::vector<ClipMove> moves;
    for (const auto& each : carried)
    {
        auto* inside = each.clip;
        if (each.cut.keepsLeft)
        {
            inside = splitClipAt(*tracks[each.track], *inside, startSeconds);
            if (inside == nullptr)
                return rollBack(juce::Result::fail("The selection could not be cut at its start."));
        }
        if (each.cut.keepsRight && splitClipAt(*tracks[each.track], *inside, endSeconds) == nullptr)
            return rollBack(juce::Result::fail("The selection could not be cut at its end."));
        const auto position = inside->getPosition();
        const auto start = std::max(0.0, position.time.getStart().inSeconds() + timeDelta);
        moves.push_back({inside->itemID, {start, start + position.time.getLength().inSeconds(), position.offset.inSeconds()},
                         each.track + trackDelta});
    }
    std::vector<te::EditItemID> travelling;
    for (const auto& move : moves)
        travelling.push_back(move.id);
    // The destination gives way whole, as it does to a paste: the rectangle,
    // not only the spans the pieces fill, so a lane that was empty inside the
    // selection empties the lane it lands on. The pieces themselves are
    // spared, which is what lets a selection move by less than its own width.
    clearClipRegionInEdit(destinationStart, destinationStart + (endSeconds - startSeconds),
                          destinationTrack, destinationTrack + (lastTrack - firstTrack), travelling);
    ClipMoveOutcome outcome;
    if (const auto result = moveClipsInEdit(moves, outcome); result.failed())
        return rollBack(result);
    repairPatternClip();
    refreshLoop();
    closeUndoStep();
    // The rows the selection left and the rows it reached, cut and cleared
    // ones included, and the rows whose devices a MIDI clip changed.
    auto rows = outcome.reached;
    for (int track = firstTrack; track <= lastTrack; ++track)
        rows.push_back(track);
    for (int row = 0; row <= lastTrack - firstTrack; ++row)
        rows.push_back(destinationTrack + row);
    markClipsModified(rows);
    if (!outcome.gainedDevices.empty())
        markClipsModified(outcome.gainedDevices, ModelChange::devices);
    resumePlaybackAfterClipEdit(*edit, outcome.anyTrackChanged, wasPlaying, hadPlaybackContext);
    sendSynchronousChangeMessage();
    if (moved != nullptr)
        *moved = travelling;
    return juce::Result::ok();
}

}
