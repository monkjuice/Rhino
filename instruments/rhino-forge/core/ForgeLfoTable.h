#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace rhino::forge
{
inline constexpr int maxLfoPoints = 33;

// How far a curve handle may be pushed off its straight line, and how close to
// either end it may sit. The handle stays grabbable at both limits, and a bend
// that ran to the rails would only be clipped by the sample below anyway.
inline constexpr float maxLfoCurve = 2.0f;
inline constexpr float minLfoCurveAt = 0.1f;
inline constexpr float maxLfoCurveAt = 0.9f;

// A point is two things at once: where the line passes through, and how the
// segment leaving it to the right is bent. `curve` is the bend — the distance
// the curve stands off the straight line at the segment's middle, zero for a
// straight one — and `curveAt` is where along that segment its handle is drawn.
// Both belong to the segment rather than to the point, and the last point's are
// unused, which is what keeps one ordered array and one binary search.
struct LfoPoint
{
    float x = 0.0f;
    float y = 0.0f;
    float curve = 0.0f;
    float curveAt = 0.5f;
};

// Fixed storage makes a table cheap to copy into the audio block's Patch.
// Endpoints stay at 0 and 1; interior points may move between their neighbours.
struct LfoTable
{
    std::array<LfoPoint, maxLfoPoints> points {};
    int count = 0;
    int columns = 8;
    int rows = 8;
    bool custom = false;

    // The bend is a parabola standing off the straight line, so it costs two
    // multiplies on a path that runs per sample per voice per LFO. It bows a
    // flat segment as readily as a sloped one, which a time warp cannot do, and
    // it is allowed to overshoot its endpoints: an LFO is a shape, not an
    // envelope, and what the clamp keeps is drawn exactly as it is heard.
    static float bend(float mix, float curve) noexcept
    {
        return 4.0f * curve * mix * (1.0f - mix);
    }

    float sample(float phase) const noexcept
    {
        if (count < 2) return 0.0f;
        const auto x = std::clamp(phase, 0.0f, 1.0f);
        int lower = 0, upper = count - 1;
        while (upper - lower > 1)
        {
            const auto middle = (lower + upper) / 2;
            if (x > points[static_cast<size_t>(middle)].x) lower = middle;
            else upper = middle;
        }
        const auto& left = points[static_cast<size_t>(lower)];
        const auto& right = points[static_cast<size_t>(upper)];
        const auto width = right.x - left.x;
        const auto mix = width > 0.0f ? (x - left.x) / width : 0.0f;
        return std::clamp(left.y + (right.y - left.y) * mix + bend(mix, left.curve), -1.0f, 1.0f);
    }

    // Where a segment's curve handle sits, in the same coordinates as a point.
    // It rides the curve it describes, so sliding it along that curve changes
    // nothing and pulling it away from the line is the whole gesture.
    LfoPoint curveHandle(int segment) const noexcept
    {
        if (segment < 0 || segment + 1 >= count) return {};
        const auto& left = points[static_cast<size_t>(segment)];
        const auto& right = points[static_cast<size_t>(segment + 1)];
        const auto at = std::clamp(left.curveAt, minLfoCurveAt, maxLfoCurveAt);
        return {left.x + (right.x - left.x) * at,
                std::clamp(left.y + (right.y - left.y) * at + bend(at, left.curve),
                           -1.0f, 1.0f),
                left.curve, at};
    }

    // The bend that carries a segment through a handle let go at (at, value).
    // Both axes of the drop feed it, which is why the handle is worth moving
    // sideways: the same height nearer an end is a harder bend.
    static float curveThrough(const LfoPoint& left, const LfoPoint& right, float at, float value) noexcept
    {
        const auto along = std::clamp(at, minLfoCurveAt, maxLfoCurveAt);
        const auto straight = left.y + (right.y - left.y) * along;
        return std::clamp((value - straight) / (4.0f * along * (1.0f - along)),
                          -maxLfoCurve, maxLfoCurve);
    }

    bool valid() const noexcept
    {
        if (count < 2 || count > maxLfoPoints || columns < 2 || columns > 32 || rows < 2 || rows > 32)
            return false;
        if (points[0].x != 0.0f || points[static_cast<size_t>(count - 1)].x != 1.0f) return false;
        for (int i = 0; i < count; ++i)
        {
            const auto& point = points[static_cast<size_t>(i)];
            if (!std::isfinite(point.x) || !std::isfinite(point.y) || point.y < -1.0f || point.y > 1.0f)
                return false;
            if (!std::isfinite(point.curve) || std::abs(point.curve) > maxLfoCurve) return false;
            if (!std::isfinite(point.curveAt) || point.curveAt < minLfoCurveAt
                || point.curveAt > maxLfoCurveAt) return false;
            if (i > 0 && point.x <= points[static_cast<size_t>(i - 1)].x) return false;
        }
        return true;
    }
};

// How many steps a sample and hold holds: one per grid column, up to the most
// columns the grid can be set to, and one more for the first step of the cycle
// after this one, which is what the last step's ramp runs towards.
inline constexpr int maxLfoSteps = 33;
using LfoSteps = std::array<float, maxLfoSteps>;

// A point on a path's pad, both axes 0..1: x runs left to right and y runs
// bottom to top. Each becomes an output swinging -1..1, as every LFO's does.
struct LfoPathPoint
{
    float x = 0.5f;
    float y = 0.5f;
};

// A route across the pad that the phase travels along, Serum's Path type.
// Equal time per segment rather than equal speed: a short segment takes as
// long as a long one, so two points laid on top of each other are a pause,
// which constant speed could not draw at all. Straight segments only, so far.
struct LfoPath
{
    std::array<LfoPathPoint, maxLfoPoints> points {};
    int count = 0;
    bool custom = false;

    LfoPathPoint at(float phase) const noexcept
    {
        if (count < 1) return {};
        if (count < 2) return points[0];
        const auto along = std::clamp(phase, 0.0f, 1.0f) * static_cast<float>(count - 1);
        const auto segment = std::min(count - 2, static_cast<int>(along));
        const auto mix = along - static_cast<float>(segment);
        const auto& from = points[static_cast<size_t>(segment)];
        const auto& to = points[static_cast<size_t>(segment + 1)];
        return {from.x + (to.x - from.x) * mix, from.y + (to.y - from.y) * mix};
    }

    bool valid() const noexcept
    {
        if (count < 2 || count > maxLfoPoints) return false;
        for (int i = 0; i < count; ++i)
        {
            const auto& point = points[static_cast<size_t>(i)];
            if (!std::isfinite(point.x) || !std::isfinite(point.y)
                || point.x < 0.0f || point.x > 1.0f || point.y < 0.0f || point.y > 1.0f)
                return false;
        }
        return true;
    }
};

// The path an LFO starts with: a diamond, so X and Y are two triangles a
// quarter of a cycle apart and the pad is visibly two-dimensional at once.
constexpr LfoPath lfoDefaultPath() noexcept
{
    LfoPath path;
    path.count = 5;
    path.points[0] = {0.5f, 1.0f};
    path.points[1] = {1.0f, 0.5f};
    path.points[2] = {0.5f, 0.0f};
    path.points[3] = {0.0f, 0.5f};
    path.points[4] = {0.5f, 1.0f};
    return path;
}

inline constexpr LfoPath lfoDefaultPathTable = lfoDefaultPath();
}
