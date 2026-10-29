#pragma once
#include <algorithm>
#include <cmath>

namespace rhino
{
struct ClipGeometry
{
    double start = 0.0, end = 0.0, offset = 0.0;
};
enum class ClipGesture { move, trimLeft, trimRight };

// A cut has to leave something playable on both sides, so a line sitting on a
// clip's edge - or within a hair of it - is not a split at all. Both the
// timeline, deciding which clips a line is drawn through, and the model, doing
// the cutting, ask this, which is what stops them disagreeing about whether a
// clip is splittable. Making room for a clip and copying a region keep to the
// same floor, scaled down for a region shorter than a tenth of a second
// (minimumRegionPieceSeconds).
inline constexpr double minimumSplitSeconds = 0.01;
inline bool canSplitClipAt(ClipGeometry clip, double seconds)
{
    return std::isfinite(seconds)
        && seconds > clip.start + minimumSplitSeconds
        && seconds < clip.end - minimumSplitSeconds;
}

// The shortest piece of a clip a region may take, or leave behind when it cuts:
// the 10 ms a split by hand must leave, or a tenth of the region when that is
// shorter. A region a beat long still drops the one sample a drum sample
// reaches past a grid line, but one cell of the grid at the deepest zoom, a few
// samples wide, cuts and copies exactly what it covers. Held at 10 ms for
// every region, that cell copied nothing.
inline double minimumRegionPieceSeconds(double regionSeconds)
{
    return std::min(minimumSplitSeconds, std::max(0.0, regionSeconds) * 0.1);
}

// How a region cuts a clip it runs across, by the floor above: the part
// inside the region, which a move carries and a copy takes, and what stays
// either side of it. A clip wholly inside is one piece. One the region only
// grazes - the part inside shorter than the floor - is left out, as a copy
// leaves it out. One crossing an edge by less than the floor goes whole, as a
// clear takes it whole, rather than leaving a sliver nobody could grab. The
// arrangement draws a carried selection from this and the model cuts by it
// (Session::moveClipRegion), which is what keeps the preview and the drop
// agreeing about every edge.
struct RegionCut
{
    bool inside = false;      // whether any of the clip goes with the region
    ClipGeometry taken;       // the part that goes, with its offset into the source
    bool keepsLeft = false;   // a piece stays behind before the region
    bool keepsRight = false;  // and one after it
    ClipGeometry left, right;
};

inline RegionCut cutClipByRegion(ClipGeometry clip, double regionStart, double regionEnd)
{
    constexpr auto tolerance = 1.0e-7;
    RegionCut cut;
    if (!std::isfinite(regionStart) || !std::isfinite(regionEnd)
        || !(clip.start < regionEnd - tolerance && clip.end > regionStart + tolerance))
        return cut;
    const auto shortest = std::max(tolerance, minimumRegionPieceSeconds(regionEnd - regionStart));
    const auto insideStart = std::max(clip.start, regionStart);
    const auto insideEnd = std::min(clip.end, regionEnd);
    const auto cropped = clip.start < regionStart - tolerance || clip.end > regionEnd + tolerance;
    if (cropped && insideEnd - insideStart < shortest)
        return cut;
    cut.inside = true;
    cut.keepsLeft = regionStart - clip.start >= shortest;
    cut.keepsRight = clip.end - regionEnd >= shortest;
    const auto takenStart = cut.keepsLeft ? regionStart : clip.start;
    const auto takenEnd = cut.keepsRight ? regionEnd : clip.end;
    cut.taken = {takenStart, takenEnd, clip.offset + takenStart - clip.start};
    cut.left = {clip.start, regionStart, clip.offset};
    cut.right = {regionEnd, clip.end, clip.offset + regionEnd - clip.start};
    return cut;
}

inline ClipGeometry previewClipEdit(ClipGeometry original, ClipGesture gesture,
                                    double target, double sourceDuration)
{
    if (!std::isfinite(target)) return original;
    const auto minimumLength = std::min(0.01, original.end - original.start);
    auto next = original;
    if (gesture == ClipGesture::move)
    {
        next.start = std::max(0.0, target);
        next.end = next.start + original.end - original.start;
    }
    else if (gesture == ClipGesture::trimLeft)
    {
        next.start = std::clamp(target, std::max(0.0, original.start - original.offset), original.end - minimumLength);
        next.offset += next.start - original.start;
    }
    else
    {
        const auto maximumEnd = std::max(original.start + minimumLength,
                                         original.start + sourceDuration - original.offset);
        next.end = std::clamp(target, original.start + minimumLength, maximumEnd);
    }
    return next;
}

// Live's Shift-drag from a warped clip's edge: the edge dragged goes to the
// target and the other stays, and all the clip shows is spread or squeezed
// over the new span, so its offset into its content scales by the same ratio.
// The ratio keeps to what the clip's tempo can reach (Session::clipStretchRange)
// and, from the left edge, to the start of the song. The model stretches by
// the ratio this gives (Session::stretchClip), so the drop lands on the preview.
inline ClipGeometry previewClipStretch(ClipGeometry original, ClipGesture edge, double target,
                                       double minimumRatio, double maximumRatio)
{
    const auto length = original.end - original.start;
    if (!std::isfinite(target) || !(length > 0.0) || edge == ClipGesture::move)
        return original;
    if (edge == ClipGesture::trimLeft)
        maximumRatio = std::min(maximumRatio, original.end / length);
    const auto wanted = (edge == ClipGesture::trimLeft ? original.end - target : target - original.start) / length;
    const auto ratio = std::clamp(wanted, std::min(minimumRatio, maximumRatio), maximumRatio);
    auto next = original;
    next.offset = original.offset * ratio;
    if (edge == ClipGesture::trimLeft)
        next.start = original.end - length * ratio;
    else
        next.end = original.start + length * ratio;
    return next;
}
}
