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

// Selected clips stand in for a region that was never dragged out, so clicking
// a clip and pressing Ctrl+D does exactly what dragging its span and pressing
// Ctrl+D does.
Arrangement::TimeSelection Arrangement::effectiveRegion() const
{
    if (timeSelection.active && timeSelection.isRange())
        return timeSelection;
    TimeSelection fromClips;
    for (const auto& clip : clips)
        // The primary clip counts even when the gathered list is empty: a menu
        // and a keyboard command can both name one clip without gathering it.
        if (isSelected(clip.id) || (selectedClips.empty() && clip.id == selected))
        {
            if (!fromClips.active)
            {
                fromClips = {clip.position.start, clip.position.end, clip.track, clip.track, true};
                continue;
            }
            fromClips.start = std::min(fromClips.start, clip.position.start);
            fromClips.end = std::max(fromClips.end, clip.position.end);
            fromClips.firstTrack = std::min(fromClips.firstTrack, clip.track);
            fromClips.lastTrack = std::max(fromClips.lastTrack, clip.track);
        }
    return fromClips.active ? fromClips : timeSelection;
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
}

bool Arrangement::beginRegionGesture(const juce::MouseEvent& event)
{
    if (event.position.x < headerWidth || event.position.y < lanesTop || event.position.y >= masterLane().getY())
        return false;
    const auto track = trackAt(event.position.y);
    if (track < 0)
        return false;
    const auto time = snapped(std::max(0.0, timeAt(event.position.x)), event.mods.isAltDown());
    // Shift drags the existing region's far edge rather than starting a new
    // one, which is how a selection is widened without redoing it.
    if (!event.mods.isShiftDown() || !timeSelection.active)
    {
        regionAnchorTime = time;
        regionAnchorTrack = track;
    }
    // Cleared first: clearing the clip selection clears the region with it.
    setSelection({});
    regionSelecting = true;
    setTimeSelection(regionAnchorTime, time, regionAnchorTrack, track);
    selectRegionContents();
    focus = Focus::region;
    return true;
}

void Arrangement::dragRegionGesture(const juce::MouseEvent& event)
{
    const auto time = snapped(std::max(0.0, timeAt(event.position.x)), event.mods.isAltDown());
    auto track = trackAt(event.position.y);
    if (track < 0)
        track = event.position.y < lanesTop ? 0 : std::max(0, session.trackCount() - 1);
    setTimeSelection(regionAnchorTime, time, regionAnchorTrack, track);
    selectRegionContents();
    repaint();
}

void Arrangement::endRegionGesture()
{
    regionSelecting = false;
    selectRegionContents();
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
    const auto left = std::max(headerWidth, xFor(timeSelection.start));
    const auto right = std::min(static_cast<float>(getWidth() - 14), xFor(timeSelection.end));
    // A track folded into a collapsed group is laid out at no height, so the
    // band is measured from the rows that are actually drawn.
    auto top = 0.0f, bottom = 0.0f;
    auto measured = false;
    for (int track = timeSelection.firstTrack; track <= timeSelection.lastTrack; ++track)
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
    if (timeSelection.isRange() && right > left)
    {
        const juce::Rectangle<float> box {left, top, right - left, bottom - top};
        g.setColour(juce::Colour(0x2ac6d58c));
        g.fillRect(box);
        g.setColour(juce::Colour(0xffc6d58c));
        g.drawRect(box, 1.0f);
    }
    // The insert point is the region with no width: where a paste would land.
    const auto marker = xFor(timeSelection.start);
    if (marker >= headerWidth && marker <= static_cast<float>(getWidth() - 14))
    {
        g.setColour(juce::Colour(0xffc6d58c));
        g.fillRect(marker - 1.0f, top, 2.0f, bottom - top);
        g.fillRect(marker - 4.0f, top, 9.0f, 3.0f);
        g.fillRect(marker - 4.0f, bottom - 3.0f, 9.0f, 3.0f);
    }
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
        status(clipboard.empty() ? "There is nothing inside the selection to copy"
                                 : "Copied " + juce::String(clipboard.size())
                                       + (clipboard.size() == 1 ? " clip" : " clips"));
}

void Arrangement::cutSelection()
{
    const auto region = effectiveRegion();
    copySelection();
    if (clipboard.empty())
        return;
    const auto result = session.clearClipRegion(region.start, region.end, region.firstTrack, region.lastTrack);
    if (result.failed())
    {
        if (status) status(result.getErrorMessage());
        return;
    }
    setSelection({});
    setInsertPoint(region.start, region.firstTrack);
    focus = Focus::region;
    if (status) status("Cut " + juce::String(clipboard.size()) + (clipboard.size() == 1 ? " clip" : " clips"));
}

void Arrangement::pasteSelection()
{
    cancelDrag();
    if (clipboard.empty())
    {
        if (status) status("Copy one or more clips first");
        return;
    }
    // Paste goes to the insert point, which is the start of the time selection
    // - dragged out, left by a click, or set by selecting a clip. Only when
    // there is no time selection at all does it fall back to the clips.
    const auto region = timeSelection.active ? timeSelection : effectiveRegion();
    const auto start = region.active ? region.start : 0.0;
    const auto track = region.active ? region.firstTrack : selectedTrack;
    std::vector<te::EditItemID> pasted;
    const auto result = session.pasteClipSnapshots(clipboard, std::max(0.0, start),
                                                   juce::jlimit(0, std::max(0, session.trackCount() - 1), track), pasted);
    if (result.failed())
    {
        if (status) status(result.getErrorMessage());
        return;
    }
    // What was pasted becomes the selection, so a second paste replaces it
    // rather than stacking on it, and Ctrl+D carries on from there. The clip
    // views are already rebuilt: the session broadcast its change before it
    // returned.
    setSelection(std::move(pasted));
    setTimeSelection(start, start + Session::snapshotSpanSeconds(clipboard),
                     track, track + Session::snapshotTrackSpan(clipboard));
    focus = Focus::region;
    if (status) status("Pasted " + juce::String(selectedClips.size())
                       + (selectedClips.size() == 1 ? " clip" : " clips"));
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
    const auto snapshots = session.copyClipRegion(region.start, region.end, region.firstTrack, region.lastTrack);
    if (snapshots.empty())
    {
        if (status) status("There is nothing inside the selection to duplicate");
        return;
    }
    const auto destination = region.end;
    std::vector<te::EditItemID> pasted;
    const auto result = session.pasteClipSnapshots(snapshots, destination, region.firstTrack, pasted);
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
