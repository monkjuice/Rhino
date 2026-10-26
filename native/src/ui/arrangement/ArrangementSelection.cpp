#include "ArrangementInternal.h"
#include "ui/shared/Playhead.h"
#include <algorithm>

// The selection and the clipboard commands built on it.
//
// There is one selection in the lanes, and it is Live's: a region, a span of
// time across a run of tracks. Clicking a clip's header selects the clip's
// own span on its track - the same selection a marquee swept over exactly the
// clip would make - dragging over the lanes sweeps one out, and Shift or Ctrl
// widens it. Every command reads that rectangle: copy takes what is inside
// it, paste drops that at the insert point replacing what it lands on,
// duplicate pastes at the region's own end and carries the region along so
// the next Ctrl+D keeps extending, Delete empties it, and dragging any clip
// header the region runs across carries all of its material
// (ArrangementGestures.cpp, Session::moveClipRegion).

namespace rhino
{

void Arrangement::setTimeSelection(double start, double end, int firstTrack, int lastTrack)
{
    timeSelection.start = std::max(0.0, std::min(start, end));
    timeSelection.end = std::max(0.0, std::max(start, end));
    // The lanes' rows only: the returns hold no clips, so a span stops at the
    // last lane above them.
    const auto lastLane = std::max(0, firstPinnedTrack() - 1);
    timeSelection.firstTrack = juce::jlimit(0, lastLane, std::min(firstTrack, lastTrack));
    timeSelection.lastTrack = juce::jlimit(timeSelection.firstTrack, lastLane, std::max(firstTrack, lastTrack));
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
    if (session.isPlaying() || session.isCountingIn())
        return;
    session.locate(seconds);
    updatePlayhead();
}

// Widens the selection to the one rectangle that holds both what was selected
// and the clip, which is what Shift-clicking a clip does in Live. With
// nothing selected the clip's own span is the selection.
void Arrangement::extendSelectionToClip(const ClipView& clip)
{
    if (!timeSelection.active)
    {
        setTimeSelection(clip.position.start, clip.position.end, clip.track, clip.track);
        return;
    }
    setTimeSelection(std::min(timeSelection.start, clip.position.start), std::max(timeSelection.end, clip.position.end),
                     std::min(timeSelection.firstTrack, clip.track), std::max(timeSelection.lastTrack, clip.track));
}

// A drag previews rather than editing, so the highlight has to preview with
// it: waiting for the drop would leave it sitting where the material is being
// carried away from.
Arrangement::TimeSelection Arrangement::displayedTimeSelection() const
{
    if (!dragging || gesture != ClipGesture::move || !timeSelection.active)
        return timeSelection;
    return timeSelection.shifted(preview.start - original.start, previewTrack - originalTrack);
}

// Live's rule: R reverses "a selection of audio material, even if it contains
// multiple audio clips", and mute, merge and the rest read the same way. A
// primary clip with no range around it - a click in the ruler leaves one -
// still names itself, so a key still has something to act on.
std::vector<te::EditItemID> Arrangement::clipsToActOn() const
{
    std::vector<te::EditItemID> hits;
    for (const auto& clip : clips)
        if (timeSelection.touches(clip.position, clip.track))
            hits.push_back(clip.id);
    if (hits.empty() && selected != te::EditItemID())
        hits.push_back(selected);
    return hits;
}

// Whether what the selection carries on one of its rows could land where the
// drag would put it: MIDI needs a MIDI lane, audio an audio lane, and a group
// bus takes neither. A prediction from the facts the sync gathered, drawn as
// Live's mark on the row; the model refuses the drop itself on release. A row
// past the last track takes a lane made for it, so it refuses nothing.
bool Arrangement::previewRowRefused(int row) const
{
    if (!dragging || gesture != ClipGesture::move || !timeSelection.active)
        return false;
    const auto source = timeSelection.firstTrack + row;
    const auto destination = source + previewTrack - originalTrack;
    // Past the lanes - a return's row included - the model makes a lane for
    // whatever lands, of its kind, so nothing there is refused.
    if (!juce::isPositiveAndBelow(destination, static_cast<int>(trackFacts.size())))
        return false;
    const auto& facts = factsFor(destination);
    if (facts.returnLetter.isNotEmpty())
        return false;
    for (const auto& clip : clips)
        if (clip.track == source && carriedCut(clip).inside && (facts.bus || facts.midi != clip.midi))
            return true;
    return false;
}

bool Arrangement::beginRegionGesture(const juce::MouseEvent& event)
{
    if (event.position.x < headerWidth || event.position.y < lanesTop || event.position.y >= returnsTop())
        return false;
    const auto track = trackAt(event.position.y);
    if (track < 0)
        return false;
    const auto time = std::max(0.0, timeAt(event.position.x));
    const auto bypass = event.mods.isAltDown();
    // Shift drags the existing selection's far edge rather than starting a
    // new one, which is how a selection is widened without redoing it. The
    // selection may have been made by clicking a clip, so the anchor is read
    // off the rectangle itself: the corner farthest from the pointer holds
    // and the near one follows. A region is one rectangle, so there is
    // nothing for Ctrl to add a second of: it starts a new region like a
    // plain drag.
    const auto extending = isExtendSelectionModifier(event.mods) && timeSelection.active;
    if (extending)
    {
        const auto farEdge = [](double at, double low, double high)
        {
            if (at < low) return high;
            if (at > high) return low;
            return at - low <= high - at ? high : low;
        };
        regionAnchorTime = farEdge(time, timeSelection.start, timeSelection.end);
        regionAnchorTrack = static_cast<int>(farEdge(track, timeSelection.firstTrack, timeSelection.lastTrack));
    }
    else
    {
        regionAnchorTime = time;
        regionAnchorTrack = track;
        // The primary clip stays as it is until the sweep ends, which names
        // the next one (nameSweptClip): the shell reads it on every change,
        // and clearing it here closed the note editor the sweep was about to
        // show a part of.
    }
    regionSelecting = true;
    regionExtending = extending;
    // A press that has not moved yet is a line, not a span, so both edges take
    // the cell the pointer is inside; a shift-click is already a span, because
    // the region it is widening was there before the press. The edges of the
    // clips on the rows the region spans cut the grid's cells too.
    const auto start = snappedDown(std::min(regionAnchorTime, time), bypass, regionAnchorTrack, track);
    setTimeSelection(start,
                     extending ? snappedUp(std::max(regionAnchorTime, time), bypass, regionAnchorTrack, track) : start,
                     regionAnchorTrack, track);
    focus = Focus::region;
    moveTransportToSelectionStart();
    return true;
}

void Arrangement::dragRegionGesture(const juce::MouseEvent& event)
{
    const auto time = std::max(0.0, timeAt(event.position.x));
    auto track = trackAt(event.position.y);
    if (track < 0)
        track = event.position.y < lanesTop ? 0 : std::max(0, firstPinnedTrack() - 1);
    const auto bypass = event.mods.isAltDown();
    // Until the pointer has actually travelled, the gesture is still the click
    // that started it and leaves a line rather than a one-cell span - unless
    // it is widening a selection, which was a span before the press.
    const auto dragged = regionExtending || event.getDistanceFromDragStart() >= 3;
    const auto first = std::min(regionAnchorTime, time);
    const auto last = std::max(regionAnchorTime, time);
    const auto start = snappedDown(first, bypass, regionAnchorTrack, track);
    setTimeSelection(start, dragged ? snappedUp(last, bypass, regionAnchorTrack, track) : start, regionAnchorTrack, track);
    moveTransportToSelectionStart();
    repaint();
}

void Arrangement::endRegionGesture()
{
    regionSelecting = false;
    moveTransportToSelectionStart();
    nameSweptClip();
    if (status && timeSelection.isRange())
    {
        const auto tracks = timeSelection.lastTrack - timeSelection.firstTrack + 1;
        status("Selected " + juce::String(timeSelection.length(), 2) + " s on "
               + juce::String(tracks) + (tracks == 1 ? " track" : " tracks"));
    }
    repaint();
}

// Of the clips a finished sweep runs across, the primary is the one the press
// landed in, or else the nearest to it, on the press's row before any other,
// as in Live, where selecting part of a clip shows that clip. A sweep that
// widened a selection keeps the primary it had while it still runs across
// it, and a span with no clip in it, or an insert point, names none. A MIDI
// clip becomes the note editor's clip, as a header press makes it, and the
// span is reported so the editor can show the part swept.
void Arrangement::nameSweptClip()
{
    te::EditItemID primary;
    auto midi = false;
    auto nearestRowsAway = 0;
    auto nearestGap = 0.0;
    for (const auto& clip : clips)
    {
        if (!timeSelection.touches(clip.position, clip.track))
            continue;
        if (regionExtending && clip.id == selected)
        {
            primary = clip.id;
            midi = clip.midi;
            break;
        }
        const auto rowsAway = std::abs(clip.track - regionAnchorTrack);
        const auto gap = std::max({0.0, clip.position.start - regionAnchorTime, regionAnchorTime - clip.position.end});
        if (primary == te::EditItemID() || rowsAway < nearestRowsAway
            || (rowsAway == nearestRowsAway && gap < nearestGap))
        {
            primary = clip.id;
            midi = clip.midi;
            nearestRowsAway = rowsAway;
            nearestGap = gap;
        }
    }
    const auto previous = selected;
    selected = primary;
    // The session announces this at once, so the shell already reads the new
    // primary, and the list of clips is rebuilt under the call.
    if (midi && !(session.hasPatternClip() && session.pattern().itemID == primary))
        if (const auto result = session.selectPatternClip(primary); result.failed() && status)
            status(result.getErrorMessage());
    if (selected != previous && clipSelected)
        clipSelected(selected);
    if (selected != te::EditItemID() && clipSpanSwept)
        clipSpanSwept(selected, timeSelection.start, timeSelection.end);
}

// Where the line is. The line the lanes draw is the region's start - the
// insert point - and while the transport is stopped the playhead is on it,
// because stopping returns to exactly this position. A document nothing has
// been clicked on has no line yet, and then the playhead is the only answer
// there is.
double Arrangement::insertPointTime() const
{
    return std::max(0.0, timeSelection.active ? timeSelection.start
                                              : session.playheadSeconds());
}

// A selection wins wherever the line genuinely crosses it, so cutting one clip
// out of a selection of several still cuts only that one. With nothing
// selected under the line - a click in a lane leaves the line and no clip -
// the line cuts whatever it is drawn through, on the tracks the region covers,
// so a line put down across four lanes cuts every clip it runs through. That
// rule is the one that matters: the click that aims the cut is the same click
// that leaves nothing selected to cut.
std::vector<te::EditItemID> Arrangement::clipsUnderLine(double seconds) const
{
    std::vector<te::EditItemID> selectedHits, laneHits;
    for (const auto& clip : clips)
    {
        if (!canSplitClipAt(clip.position, seconds))
            continue;
        if (timeSelection.touches(clip.position, clip.track) || clip.id == selected)
            selectedHits.push_back(clip.id);
        // No region at all means the line is the playhead, and the working
        // track is the only lane the timeline can say it is about.
        if (timeSelection.active ? timeSelection.covers(clip.track) : clip.track == selectedTrack)
            laneHits.push_back(clip.id);
    }
    return selectedHits.empty() ? laneHits : selectedHits;
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
        // Carried past the last lane, a row is drawn where its lane will be.
        const auto row = laneOrSlot(track);
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
        // A row whose material could not land where the drag would put it
        // wears the mark Live puts there, so the refusal shows before the
        // drop rather than as a message after it.
        for (int row = 0; row <= shown.lastTrack - shown.firstTrack; ++row)
        {
            if (!previewRowRefused(row)) continue;
            const auto target = lane(shown.firstTrack + row);
            const auto size = std::min(16.0f, target.getHeight() - 8.0f);
            if (size < 8.0f) continue;
            const juce::Rectangle<float> mark {left + 6.0f, target.getCentreY() - size * 0.5f, size, size};
            g.setColour(palette::recordAccent);
            g.drawEllipse(mark, 2.0f);
            const auto inset = size * 0.22f;
            g.drawLine(mark.getX() + inset, mark.getY() + inset, mark.getRight() - inset, mark.getBottom() - inset, 2.0f);
        }
    }
    // The insert point is the region with no width: where a paste would land.
    const auto marker = xFor(shown.start);
    if (marker >= headerWidth && marker <= static_cast<float>(getWidth() - 14))
    {
        g.setColour(juce::Colour(0xffc6d58c));
        // Left edge on the time, the same two pixels the playhead fills, so
        // that a parked playhead sits exactly on the line it was parked by.
        g.fillRect(marker, top, 2.0f, bottom - top);
    }
}

// "3.2" - the bar and beat something landed on, so the status line can say
// where a paste went rather than only that it went somewhere.
juce::String Arrangement::barPositionText(double seconds) const
{
    const auto beats = session.secondsToBeats(std::max(0.0, seconds));
    const auto signature = session.timeSignature();
    const auto place = musicalPosition(beats, signature.numerator, signature.denominator);
    return juce::String(place.bar) + "." + juce::String(place.count);
}

// A card that has the focus is what the clipboard commands act on, whole
// (ArrangementTrackClipboard.cpp); otherwise they act on the selection.
void Arrangement::copySelection()
{
    if (focus == Focus::track)
    {
        copyCards(selectedTracks);
        return;
    }
    const auto region = timeSelection;
    if (!region.active || !region.isRange())
    {
        if (status) status("Select clips or drag a span of the timeline to copy");
        return;
    }
    clipboard = session.copyClipRegion(region.start, region.end, region.firstTrack, region.lastTrack);
    trackClipboard = {};
    if (status)
        status(clipboard.clips.empty() ? "There is nothing inside the selection to copy"
                                       : "Copied " + juce::String(clipboard.clips.size())
                                             + (clipboard.clips.size() == 1 ? " clip" : " clips"));
}

void Arrangement::cutSelection()
{
    if (focus == Focus::track)
    {
        cutCards(selectedTracks);
        return;
    }
    const auto region = timeSelection;
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
    // Tracks on the clipboard land under the working card, wherever the last
    // click was, because what was copied decides what a paste is.
    if (!trackClipboard.isEmpty())
    {
        pasteCards(selectedTrack);
        return;
    }
    if (clipboard.isEmpty())
    {
        if (status) status("Copy one or more clips first");
        return;
    }
    // Paste goes to the insert point, which is the start of the selection -
    // dragged out, left by a click, or made by selecting a clip. With no
    // selection at all it goes to the top of the working track.
    const auto region = timeSelection;
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
    if (focus == Focus::track)
    {
        duplicateCards(selectedTracks);
        return;
    }
    const auto region = timeSelection;
    if (!region.active || !region.isRange())
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
    if (status) status("Duplicated " + juce::String(copied.clips.size())
                       + (copied.clips.size() == 1 ? " clip" : " clips"));
    repaint();
}

// The selection is a span whatever made it: a clicked clip's own span deletes
// the clip, and a swept one empties the timeline it covers, cutting the clips
// that cross its edges. The line is left where the selection started.
void Arrangement::deleteSelection()
{
    if (!timeSelection.active || !timeSelection.isRange())
    {
        if (status) status("Select a clip or a span of the timeline to delete");
        return;
    }
    // Read before the selection is cleared: the line goes where the region
    // started, not to the top of track one.
    const auto region = timeSelection;
    const auto result = session.clearClipRegion(region.start, region.end, region.firstTrack, region.lastTrack);
    if (result.failed() && status) status(result.getErrorMessage());
    setSelection({});
    setInsertPoint(region.start, region.firstTrack);
    focus = Focus::region;
}

}
