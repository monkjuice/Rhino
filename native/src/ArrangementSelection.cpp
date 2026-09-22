#include "ArrangementInternal.h"
#include "Playhead.h"
#include <algorithm>

// The time selection and the clipboard commands built on it.
//
// One rule covers all four commands, and it is Live's: everything acts on a
// region - a span of time across a run of tracks - and the region is either
// the one dragged out in the lanes or the span of the clips that are selected.
// Copy takes what is inside it, paste drops that at the insert point replacing
// what it lands on, duplicate pastes at the region's own end and carries the
// region along so the next Ctrl+D keeps extending, and Delete empties it.

namespace rhino
{

void Arrangement::setTimeSelection(double start, double end, int firstTrack, int lastTrack)
{
    timeSelection.start = std::max(0.0, std::min(start, end));
    timeSelection.end = std::max(0.0, std::max(start, end));
    timeSelection.firstTrack = juce::jlimit(0, std::max(0, session.trackCount() - 1), std::min(firstTrack, lastTrack));
    timeSelection.lastTrack = juce::jlimit(timeSelection.firstTrack, std::max(0, session.trackCount() - 1),
                                           std::max(firstTrack, lastTrack));
    timeSelection.active = true;
    // Set here and re-set by setRegionFromSelectedClips, so a region only
    // follows clips when it was actually read off them.
    regionFollowsClips = false;
}

// A click with no drag. Paste lands here, and so does the material a duplicate
// pushes along when nothing is selected.
void Arrangement::setInsertPoint(double seconds, int track)
{
    setTimeSelection(seconds, seconds, track, track);
}

void Arrangement::clearTimeSelection()
{
    timeSelection = {};
    regionSelecting = false;
}

// Where the line is put is where playback starts from. The transport follows
// it only while it is stopped: a click made during playback would otherwise
// jump the song out from under whoever made it. The session is told either
// way, because Stop returns there rather than to the top of the song.
void Arrangement::moveTransportToSelectionStart()
{
    if (!timeSelection.active)
        return;
    const auto seconds = std::max(0.0, timeSelection.start);
    session.setPlaybackStart(seconds);
    auto& transport = session.edit->getTransport();
    if (transport.isPlaying() || session.isCountingIn())
        return;
    if (std::abs(transport.getPosition().inSeconds() - seconds) < 1.0e-9)
        return;
    transport.setPosition(tracktion::core::TimePosition::fromSeconds(seconds));
    updatePlayhead();
}

// Selected clips stand in for a region that was never dragged out, so clicking
// a clip and pressing Ctrl+D does exactly what dragging its span and pressing
// Ctrl+D does.
Arrangement::TimeSelection Arrangement::regionOfSelectedClips(bool previewed) const
{
    TimeSelection bounds;
    for (const auto& clip : clips)
        // The primary clip counts even when the gathered list is empty: a menu
        // and a keyboard command can both name one clip without gathering it.
        if (isSelected(clip.id) || (selectedClips.empty() && clip.id == selected))
        {
            const auto position = previewed ? displayedPosition(clip) : clip.position;
            const auto track = previewed ? displayedTrack(clip) : clip.track;
            if (!bounds.active)
            {
                bounds = {position.start, position.end, track, track, true};
                continue;
            }
            bounds.start = std::min(bounds.start, position.start);
            bounds.end = std::max(bounds.end, position.end);
            bounds.firstTrack = std::min(bounds.firstTrack, track);
            bounds.lastTrack = std::max(bounds.lastTrack, track);
        }
    return bounds;
}

Arrangement::TimeSelection Arrangement::effectiveRegion() const
{
    if (timeSelection.active && timeSelection.isRange())
        return timeSelection;
    const auto fromClips = regionOfSelectedClips(false);
    return fromClips.active ? fromClips : timeSelection;
}

// A drag previews rather than editing, so the region has to preview with it:
// waiting for the drop would leave the highlight sitting at the position the
// clip is being carried away from.
Arrangement::TimeSelection Arrangement::displayedTimeSelection() const
{
    if (!dragging || !regionFollowsClips)
        return timeSelection;
    const auto previewed = regionOfSelectedClips(true);
    return previewed.active ? previewed : timeSelection;
}

// Dragging a region also selects the clips it touches, so Delete, the clip
// colour key and the clip menu all keep working on what is highlighted.
void Arrangement::selectRegionContents()
{
    if (!timeSelection.isRange())
        return;
    std::vector<te::EditItemID> hits;
    for (const auto& clip : clips)
        if (timeSelection.covers(clip.track)
            && clip.position.start < timeSelection.end - 1.0e-7
            && clip.position.end > timeSelection.start + 1.0e-7)
            hits.push_back(clip.id);
    // The region is what changed the selection here, so it must not be
    // recomputed from the clips the change picked up.
    syncingSelection = true;
    setSelection(std::move(hits));
    syncingSelection = false;
    // Selecting clips makes the click a clip focus; the region is what Delete
    // should reach for, so it takes the focus back.
    focus = Focus::region;
}

// After a click or a marquee has changed which clips are selected, the region
// follows them. Deriving it rather than remembering it is what keeps the two
// from disagreeing.
void Arrangement::setRegionFromSelectedClips()
{
    clearTimeSelection();
    const auto region = effectiveRegion();
    if (region.active)
        setTimeSelection(region.start, region.end, region.firstTrack, region.lastTrack);
    regionFollowsClips = true;
}

bool Arrangement::beginRegionGesture(const juce::MouseEvent& event)
{
    if (event.position.x < headerWidth || event.position.y < lanesTop || event.position.y >= masterLane().getY())
        return false;
    const auto track = trackAt(event.position.y);
    if (track < 0)
        return false;
    const auto time = std::max(0.0, timeAt(event.position.x));
    const auto bypass = event.mods.isAltDown();
    // Shift drags the existing region's far edge rather than starting a new
    // one, which is how a selection is widened without redoing it. A region is
    // one rectangle, so there is nothing for Ctrl to add a second of: it
    // starts a new region like a plain drag.
    const auto extending = isExtendSelectionModifier(event.mods) && timeSelection.active;
    if (!extending)
    {
        regionAnchorTime = time;
        regionAnchorTrack = track;
    }
    // Cleared first: clearing the clip selection clears the region with it.
    setSelection({});
    regionSelecting = true;
    // A press that has not moved yet is a line, not a span, so both edges take
    // the cell the pointer is inside; a shift-click is already a span, because
    // the region it is widening was there before the press.
    const auto start = snappedDown(std::min(regionAnchorTime, time), bypass);
    setTimeSelection(start, extending ? snappedUp(std::max(regionAnchorTime, time), bypass) : start,
                     regionAnchorTrack, track);
    if (extending)
        selectRegionContents();
    focus = Focus::region;
    moveTransportToSelectionStart();
    return true;
}

void Arrangement::dragRegionGesture(const juce::MouseEvent& event)
{
    const auto time = std::max(0.0, timeAt(event.position.x));
    auto track = trackAt(event.position.y);
    if (track < 0)
        track = event.position.y < lanesTop ? 0 : std::max(0, session.trackCount() - 1);
    const auto bypass = event.mods.isAltDown();
    // Until the pointer has actually travelled, the gesture is still the click
    // that started it and leaves a line rather than a one-cell span.
    const auto dragged = event.getDistanceFromDragStart() >= 3;
    const auto first = std::min(regionAnchorTime, time);
    const auto last = std::max(regionAnchorTime, time);
    const auto start = snappedDown(first, bypass);
    setTimeSelection(start, dragged ? snappedUp(last, bypass) : start, regionAnchorTrack, track);
    selectRegionContents();
    moveTransportToSelectionStart();
    repaint();
}

void Arrangement::endRegionGesture()
{
    regionSelecting = false;
    selectRegionContents();
    moveTransportToSelectionStart();
    if (status && timeSelection.isRange())
    {
        const auto tracks = timeSelection.lastTrack - timeSelection.firstTrack + 1;
        status("Selected " + juce::String(timeSelection.length(), 2) + " s on "
               + juce::String(tracks) + (tracks == 1 ? " track" : " tracks"));
    }
    repaint();
}

void Arrangement::paintTimeSelection(juce::Graphics& g)
{
    if (!timeSelection.active)
        return;
    const auto shown = displayedTimeSelection();
    const auto left = std::max(headerWidth, xFor(shown.start));
    const auto right = std::min(static_cast<float>(getWidth() - 14), xFor(shown.end));
    // A track folded into a collapsed group is laid out at no height, so the
    // band is measured from the rows that are actually drawn.
    auto top = 0.0f, bottom = 0.0f;
    auto measured = false;
    for (int track = shown.firstTrack; track <= shown.lastTrack; ++track)
    {
        const auto row = lane(track);
        if (row.getHeight() <= 0.0f) continue;
        top = measured ? std::min(top, row.getY()) : row.getY();
        bottom = measured ? std::max(bottom, row.getBottom()) : row.getBottom();
        measured = true;
    }
    if (!measured || bottom <= top)
        return;
    juce::Graphics::ScopedSaveState scope(g);
    g.reduceClipRegion(juce::Rectangle<int>(static_cast<int>(headerWidth), static_cast<int>(lanesTop),
                                            std::max(1, getWidth() - static_cast<int>(headerWidth) - 14),
                                            std::max(1, static_cast<int>(laneContentHeight()))));
    if (shown.isRange() && right > left)
    {
        const juce::Rectangle<float> box {left, top, right - left, bottom - top};
        // The wash covers a clip's lower row and stops at its header, so the
        // strip that names the clip and carries it stays legible under a
        // selection that runs across it.
        juce::RectangleList<float> wash(box);
        for (const auto& clip : clips)
            if (shown.covers(displayedTrack(clip)))
                wash.subtract(clipHeaderBounds(clip));
        g.setColour(juce::Colour(0x2ac6d58c));
        g.fillRectList(wash);
        g.setColour(juce::Colour(0xffc6d58c));
        g.drawRect(box, 1.0f);
    }
    // The insert point is the region with no width: where a paste would land.
    const auto marker = xFor(shown.start);
    if (marker >= headerWidth && marker <= static_cast<float>(getWidth() - 14))
    {
        g.setColour(juce::Colour(0xffc6d58c));
        g.fillRect(marker - 1.0f, top, 2.0f, bottom - top);
        g.fillRect(marker - 4.0f, top, 9.0f, 3.0f);
        g.fillRect(marker - 4.0f, bottom - 3.0f, 9.0f, 3.0f);
    }
}

// "3.2" - the bar and beat something landed on, so the status line can say
// where a paste went rather than only that it went somewhere.
juce::String Arrangement::barPositionText(double seconds) const
{
    const auto beats = session.edit->tempoSequence
        .toBeats(tracktion::core::TimePosition::fromSeconds(std::max(0.0, seconds))).inBeats();
    const auto perBar = std::max(1.0, session.beatsPerBar());
    const auto bar = std::floor(beats / perBar);
    return juce::String(static_cast<int>(bar) + 1) + "."
         + juce::String(static_cast<int>(std::floor(beats - bar * perBar)) + 1);
}

void Arrangement::copySelection()
{
    const auto region = effectiveRegion();
    if (!region.isRange())
    {
        if (status) status("Select clips or drag a span of the timeline to copy");
        return;
    }
    clipboard = session.copyClipRegion(region.start, region.end, region.firstTrack, region.lastTrack);
    if (status)
        status(clipboard.clips.empty() ? "There is nothing inside the selection to copy"
                                       : "Copied " + juce::String(clipboard.clips.size())
                                             + (clipboard.clips.size() == 1 ? " clip" : " clips"));
}

void Arrangement::cutSelection()
{
    const auto region = effectiveRegion();
    copySelection();
    if (clipboard.clips.empty())
        return;
    const auto result = session.clearClipRegion(region.start, region.end, region.firstTrack, region.lastTrack);
    if (result.failed())
    {
        if (status) status(result.getErrorMessage());
        return;
    }
    setSelection({});
    setTimeSelection(region.start, region.end, region.firstTrack, region.lastTrack);
    focus = Focus::region;
    if (status) status("Cut " + juce::String(clipboard.clips.size())
                       + (clipboard.clips.size() == 1 ? " clip" : " clips"));
}

void Arrangement::pasteSelection()
{
    cancelDrag();
    if (clipboard.isEmpty())
    {
        if (status) status("Copy one or more clips first");
        return;
    }
    // Paste goes to the insert point, which is the start of the time selection
    // - dragged out, left by a click, or set by selecting a clip. Only when
    // there is no time selection at all does it fall back to the clips.
    const auto region = timeSelection.active ? timeSelection : effectiveRegion();
    const auto start = std::max(0.0, region.active ? region.start : 0.0);
    const auto track = juce::jlimit(0, std::max(0, session.trackCount() - 1),
                                    region.active ? region.firstTrack : selectedTrack);
    std::vector<te::EditItemID> pasted;
    const auto result = session.pasteClipRegion(clipboard, start, track, pasted);
    if (result.failed())
    {
        if (status) status(result.getErrorMessage());
        return;
    }
    // The region the paste occupies becomes the selection - the rectangle that
    // was copied, not the bounding box of the clips that happened to be in it,
    // so a lane that was empty stays part of it. A second paste then replaces
    // what the first one put down rather than stacking on it.
    setSelection(std::move(pasted));
    setTimeSelection(start, start + clipboard.spanSeconds, track, track + clipboard.trackSpan);
    focus = Focus::region;
    if (status) status("Pasted " + juce::String(clipboard.clips.size())
                       + (clipboard.clips.size() == 1 ? " clip at " : " clips at ") + barPositionText(start));
    repaint();
}

// Live's rule exactly: the copy lands flush against the end of what is
// selected, and the selection moves onto it, so holding Ctrl+D turns one bar
// into four without touching the pointer.
void Arrangement::duplicateSelected()
{
    cancelDrag();
    const auto region = effectiveRegion();
    if (!region.isRange())
    {
        if (status) status("Select a clip or a span of the timeline to duplicate");
        return;
    }
    const auto copied = session.copyClipRegion(region.start, region.end, region.firstTrack, region.lastTrack);
    if (copied.clips.empty())
    {
        if (status) status("There is nothing inside the selection to duplicate");
        return;
    }
    const auto destination = region.end;
    std::vector<te::EditItemID> pasted;
    const auto result = session.pasteClipRegion(copied, destination, region.firstTrack, pasted);
    if (result.failed())
    {
        if (status) status(result.getErrorMessage());
        return;
    }
    setSelection(std::move(pasted));
    setTimeSelection(destination, destination + region.length(), region.firstTrack, region.lastTrack);
    focus = Focus::region;
    if (status) status("Duplicated " + juce::String(selectedClips.size())
                       + (selectedClips.size() == 1 ? " clip" : " clips"));
    repaint();
}

void Arrangement::deleteSelection()
{
    // A dragged-out region empties the timeline it covers, cutting the clips
    // that cross its edges. Without one, Delete takes the selected clips whole.
    if (timeSelection.active && timeSelection.isRange())
    {
        const auto result = session.clearClipRegion(timeSelection.start, timeSelection.end,
                                                    timeSelection.firstTrack, timeSelection.lastTrack);
        if (result.failed() && status) status(result.getErrorMessage());
        setSelection({});
        setInsertPoint(timeSelection.start, timeSelection.firstTrack);
        focus = Focus::region;
        return;
    }
    if (selectedClips.empty() && selected != te::EditItemID()) selectedClips = {selected};
    for (const auto id : selectedClips) session.deleteClip(id);
    setSelection({});
}

}
