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
