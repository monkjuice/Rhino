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
// clip is splittable.
inline constexpr double minimumSplitSeconds = 0.01;
inline bool canSplitClipAt(ClipGeometry clip, double seconds)
{
    return std::isfinite(seconds)
        && seconds > clip.start + minimumSplitSeconds
        && seconds < clip.end - minimumSplitSeconds;
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
}
