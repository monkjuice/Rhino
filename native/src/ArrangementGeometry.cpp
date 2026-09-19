#include "Arrangement.h"
#include <limits>

namespace rhino
{
float Arrangement::laneContentHeight() const
{
    return std::max(1.0f, getHeight() - lanesTop - 18.0f - masterLaneHeight);
}

juce::Rectangle<float> Arrangement::masterLane() const
{
    return {0.0f, lanesTop + laneContentHeight(), std::max(1.0f, getWidth() - 14.0f), masterLaneHeight};
}

bool Arrangement::isMasterSelected() const
{
    return selectedTrack == session.masterTrackIndex();
}

float Arrangement::laneHeight() const
{
    return std::max(mixerLaneHeight + 8.0f, std::min(96.0f, laneContentHeight() / 2.0f));
}

float Arrangement::laneHeightFor(int track) const
{
    if (track == resizingTrack)
        return resizePreview;
    const auto chosen = session.trackLaneHeight(track);
    return chosen > 0.0f ? juce::jlimit(minimumLaneHeight, maximumLaneHeight, chosen) : laneHeight();
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

ClipGeometry Arrangement::displayedPosition(const ClipView& clip) const
{
    if (!dragging || !isSelected(clip.id) || gesture != ClipGesture::move)
        return dragging && clip.id == selected ? preview : clip.position;
    const auto delta = preview.start - original.start;
    return {clip.position.start + delta, clip.position.end + delta, clip.position.offset};
}

int Arrangement::displayedTrack(const ClipView& clip) const
{
    if (!dragging || !isSelected(clip.id) || gesture != ClipGesture::move)
        return dragging && clip.id == selected ? previewTrack : clip.track;
    return clip.track + previewTrack - originalTrack;
}

juce::Rectangle<float> Arrangement::bounds(const ClipView& clip) const
{
    const auto p = displayedPosition(clip);
    const auto track = displayedTrack(clip);
    return {xFor(p.start), lane(track).getY() + 5.0f,
            std::max(1.0f, xFor(p.end) - xFor(p.start)), lane(track).getHeight() - 10.0f};
}

double Arrangement::snapped(double seconds, bool bypass) const
{
    const auto enabled = gridSettings.mode != GridMode::off;
    if (enabled == bypass)
        return seconds;
    const auto beat = session.edit->tempoSequence.toBeats(tracktion::core::TimePosition::fromSeconds(seconds)).inBeats();
    const auto snappedBeat = std::round(beat / resolvedGridBeats()) * resolvedGridBeats();
    return session.edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(snappedBeat)).inSeconds();
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
        if (isSelected(clip.id) || clip.track != targetTrack)
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

    const auto loopRange = session.edit->getTransport().getLoopRange();
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
    const auto start = session.edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(0.0)).inSeconds();
    const auto end = session.edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(resolvedGridBeats())).inSeconds();
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
