#include "Arrangement.h"
#include <limits>

namespace rhino
{
// The lanes that scroll: what is left between the ruler and the pinned block
// of returns over the main row.
float Arrangement::laneContentHeight() const
{
    return std::max(1.0f, getHeight() - bottomInset - lanesTop - footHeight - masterLaneHeight - pinnedRowsHeight);
}

float Arrangement::returnsTop() const
{
    return lanesTop + laneContentHeight();
}

juce::Rectangle<float> Arrangement::rowArea(bool pinned) const
{
    const auto width = std::max(1.0f, static_cast<float>(getWidth()) - 14.0f);
    return pinned ? juce::Rectangle<float>(0.0f, returnsTop(), width, pinnedRowsHeight)
                  : juce::Rectangle<float>(0.0f, lanesTop, width, laneContentHeight());
}

int Arrangement::firstPinnedTrack() const
{
    return returns.empty() ? session.trackCount() : returns.front().track;
}

bool Arrangement::isPinnedTrack(int track) const
{
    return !returns.empty() && track >= returns.front().track && track < session.trackCount();
}

float Arrangement::lastLaneBottom() const
{
    return lanesTop + rowsHeight - static_cast<float>(trackScroll);
}

// A clip carried onto the returns, or under them, lands on a lane made for it
// above them, so its preview is drawn where that lane will appear rather than
// over a return's row.
juce::Rectangle<float> Arrangement::laneOrSlot(int track) const
{
    const auto first = firstPinnedTrack();
    if (track < first)
        return lane(track);
    const auto height = laneHeight();
    return {headerWidth, lastLaneBottom() + static_cast<float>(track - first) * height,
            std::max(1.0f, getWidth() - headerWidth - 14.0f), height};
}

// Measured against the default main row and the whole panel, so neither the
// pane floating over the foot of the arrangement nor a dragged main row can
// change it. A lane that resized as the pane was dragged made every clip on
// the timeline grow and shrink under the pointer. The returns pinned over the
// main row are taken out first: the lanes fit the room they actually have.
float Arrangement::fullLaneContentHeight() const
{
    return std::max(1.0f, getHeight() - lanesTop - footHeight - defaultMasterLaneHeight - pinnedRowsHeight);
}

void Arrangement::setBottomInset(float inset)
{
    inset = std::max(0.0f, inset);
    if (std::abs(inset - bottomInset) < 0.5f) return;
    bottomInset = inset;
    resized();
    repaint();
}

juce::Rectangle<float> Arrangement::masterLane() const
{
    return {0.0f, returnsTop() + pinnedRowsHeight, std::max(1.0f, getWidth() - 14.0f), masterLaneHeight};
}

bool Arrangement::isMasterSelected() const
{
    return selectedTrack == session.masterTrackIndex();
}

float Arrangement::laneHeight() const
{
    return std::max(mixerLaneHeight + 8.0f, std::min(96.0f, fullLaneContentHeight() / 2.0f));
}

float Arrangement::laneHeightFor(int track) const
{
    if (track == resizingTrack)
        return resizePreview;
    // Folded, a row is down to its one line, and keeps the height it unfolds to.
    if (session.isTrackFolded(track))
        return minimumLaneHeight;
    const auto chosen = session.trackLaneHeight(track);
    if (chosen > 0.0f)
        return juce::jlimit(minimumLaneHeight, maximumLaneHeight, chosen);
    return isPinnedTrack(track) ? defaultReturnLaneHeight : laneHeight();
}

// Rows are variable height once automation lanes are revealed, so a track's
// lane is looked up in the row stack rather than multiplied out. The fallback
// covers construction, before the first sync has built the stack.
juce::Rectangle<float> Arrangement::lane(int track) const
{
    if (juce::isPositiveAndBelow(track, static_cast<int>(trackRowIndex.size())))
        if (const auto row = trackRowIndex[static_cast<size_t>(track)]; row >= 0)
            return rowBounds(row);
    const auto height = laneHeightFor(track);
    return {headerWidth, lanesTop + track * height - static_cast<float>(trackScroll),
            std::max(1.0f, getWidth() - headerWidth - 14.0f), height};
}

float Arrangement::xFor(double seconds) const
{
    return headerWidth + static_cast<float>((seconds - viewStart) / viewSpan * lane(0).getWidth());
}

double Arrangement::timeAt(float x) const
{
    return viewStart + (x - headerWidth) / lane(0).getWidth() * viewSpan;
}

// The cut is the one the model will make on the drop (Session::moveClipRegion
// cuts by the same function), so the preview shows the edges the drop leaves.
RegionCut Arrangement::carriedCut(const ClipView& clip) const
{
    if (!dragging || gesture != ClipGesture::move || !timeSelection.covers(clip.track) || !timeSelection.isRange())
        return {};
    return cutClipByRegion(clip.position, timeSelection.start, timeSelection.end);
}

ClipGeometry Arrangement::displayedPosition(const ClipView& clip) const
{
    if (!dragging)
        return clip.position;
    if (gesture != ClipGesture::move)
        return clip.id == selected ? preview : clip.position;
    const auto cut = carriedCut(clip);
    if (!cut.inside)
        return clip.position;
    const auto delta = preview.start - original.start;
    return {cut.taken.start + delta, cut.taken.end + delta, cut.taken.offset};
}

int Arrangement::displayedTrack(const ClipView& clip) const
{
    if (!dragging || gesture != ClipGesture::move || !carriedCut(clip).inside)
        return clip.track;
    return clip.track + previewTrack - originalTrack;
}

// One piece until the selection is carried; then, for a clip it runs across,
// the part that travels at its previewed place between whatever stays behind
// either side of it, on the clip's own track.
Arrangement::DisplayedPieces Arrangement::displayedPieces(const ClipView& clip) const
{
    DisplayedPieces pieces;
    const auto add = [&pieces](ClipGeometry position, int track, bool carried)
    {
        pieces.items[static_cast<size_t>(pieces.count++)] = {position, track, carried};
    };
    const auto cut = carriedCut(clip);
    if (!cut.inside)
    {
        add(displayedPosition(clip), displayedTrack(clip), false);
        return pieces;
    }
    const auto delta = preview.start - original.start;
    if (cut.keepsLeft)
        add(cut.left, clip.track, false);
    add({cut.taken.start + delta, cut.taken.end + delta, cut.taken.offset}, clip.track + previewTrack - originalTrack, true);
    if (cut.keepsRight)
        add(cut.right, clip.track, false);
    return pieces;
}

juce::Rectangle<float> Arrangement::bounds(const ClipView& clip) const
{
    return boundsOf(displayedPosition(clip), displayedTrack(clip));
}

juce::Rectangle<float> Arrangement::boundsOf(ClipGeometry p, int track) const
{
    const auto row = laneOrSlot(track);
    return {xFor(p.start), row.getY() + 5.0f, std::max(1.0f, xFor(p.end) - xFor(p.start)), row.getHeight() - 10.0f};
}

double Arrangement::snapped(double seconds, bool bypass) const
{
    const auto enabled = gridSettings.mode != GridMode::off;
    if (enabled == bypass)
        return seconds;
    const auto beat = session.secondsToBeats(seconds);
    const auto snappedBeat = std::round(beat / resolvedGridBeats()) * resolvedGridBeats();
    return session.beatsToSeconds(snappedBeat);
}

// Rounding to the nearest line is what a clip edge wants; the selection line
// and a region edge want the cell the pointer is inside, which is a floor and
// a ceiling of the same division.
double Arrangement::snappedDown(double seconds, bool bypass) const
{
    if ((gridSettings.mode != GridMode::off) == bypass)
        return seconds;
    const auto beat = session.secondsToBeats(seconds);
    const auto grid = resolvedGridBeats();
    // Nudged by a thousandth of a cell so a pointer sitting exactly on a line
    // takes that line rather than the one before it, which floating point
    // would otherwise decide either way.
    const auto cell = std::floor(beat / grid + 1.0e-3);
    return session.beatsToSeconds(cell * grid);
}

double Arrangement::snappedUp(double seconds, bool bypass) const
{
    if ((gridSettings.mode != GridMode::off) == bypass)
        return seconds;
    const auto beat = session.secondsToBeats(seconds);
    const auto grid = resolvedGridBeats();
    const auto cell = std::ceil(beat / grid - 1.0e-3);
    return session.beatsToSeconds(cell * grid);
}

// Half of a short clip rather than a fixed strip: a row dragged right down
// still has to have both of its rows, and the name has to fit on the one that
// carries it.
float Arrangement::clipHeaderHeight(float boxHeight)
{
    return juce::jlimit(8.0f, 22.0f, boxHeight * 0.5f);
}

juce::Rectangle<float> Arrangement::clipHeaderBounds(const ClipView& clip) const
{
    const auto box = bounds(clip);
    return box.withHeight(clipHeaderHeight(box.getHeight()));
}

Arrangement::ClipZone Arrangement::clipZoneAt(juce::Point<float> point, const ClipView& clip) const
{
    const auto box = bounds(clip);
    const auto handle = std::min(7.0f, box.getWidth() * 0.25f);
    if (point.x - box.getX() < handle) return ClipZone::trimStart;
    if (box.getRight() - point.x < handle) return ClipZone::trimEnd;
    return point.y < box.getY() + clipHeaderHeight(box.getHeight()) ? ClipZone::header : ClipZone::body;
}

double Arrangement::snappedClipMoveStart(double desiredStart, double length, int targetTrack, bool bypass) const
{
    if ((gridSettings.mode != GridMode::off) == bypass)
        return desiredStart;

    const auto desiredEnd = desiredStart + length;
    auto bestStart = desiredStart;
    auto bestPixels = std::numeric_limits<float>::max();
    constexpr auto edgeSnapPixels = 14.0f;

    for (const auto& clip : clips)
    {
        // A clip the selection carries, in part or whole, is on its way
        // somewhere else, so its edges are nothing to snap to.
        if (clip.track != targetTrack || carriedCut(clip).inside)
            continue;
        for (const auto edge : {clip.position.start, clip.position.end})
        {
            const auto startPixels = std::abs(xFor(edge) - xFor(desiredStart));
            if (startPixels <= edgeSnapPixels && startPixels < bestPixels)
            {
                bestPixels = startPixels;
                bestStart = edge;
            }

            const auto endPixels = std::abs(xFor(edge) - xFor(desiredEnd));
            if (endPixels <= edgeSnapPixels && endPixels < bestPixels)
            {
                bestPixels = endPixels;
                bestStart = edge - length;
            }
        }
    }

    return std::max(0.0, bestPixels < std::numeric_limits<float>::max() ? bestStart : snapped(desiredStart, false));
}

int Arrangement::hit(juce::Point<float> point) const
{
    if (point.x < headerWidth) return -1;
    for (int i = static_cast<int>(clips.size()); --i >= 0;)
        if (bounds(clips[static_cast<size_t>(i)]).contains(point)) return i;
    return -1;
}

Arrangement::LoopGesture Arrangement::loopGestureAt(juce::Point<float> point) const
{
    if (!session.hasManualLoopRange() || point.y < rulerTop || point.y >= lanesTop || point.x < headerWidth)
        return LoopGesture::none;

    const auto loopRange = session.loopRange();
    const auto x1 = xFor(loopRange.getStart().inSeconds());
    const auto x2 = xFor(loopRange.getEnd().inSeconds());
    const auto left = std::min(x1, x2);
    const auto right = std::max(x1, x2);
    if (point.x < left || point.x > right)
        return LoopGesture::none;

    const auto handle = std::min(10.0f, std::max(4.0f, (right - left) * 0.3f));
    if (point.x - left <= handle)
        return LoopGesture::trimStart;
    if (right - point.x <= handle)
        return LoopGesture::trimEnd;
    return LoopGesture::move;
}

double Arrangement::snapUnitSeconds() const
{
    const auto start = session.beatsToSeconds(0.0);
    const auto end = session.beatsToSeconds(resolvedGridBeats());
    return std::max(0.0001, end - start);
}

// A ghost automation row answers with the track it clones, so dropping or
// selecting on one behaves exactly as it does on the track itself.
int Arrangement::trackAt(float y) const
{
    if (const auto row = rowAt(y); row >= 0)
        return rows[static_cast<size_t>(row)].track;
    return -1;
}
}
