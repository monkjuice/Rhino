#pragma once

#include <algorithm>
#include <array>
#include <cmath>

// The sixteen ducking curves, as pure arithmetic.
//
// A curve is a gain, 0 to 1, over one cycle of phase, 0 to 1. Every picture of
// a curve - the sixteen thumbnails, the line over the scope - and the gain the
// audio runs are the one function, curveGain, so a picture cannot disagree
// with the sound.
//
// A curve is a list of points: the phase a segment ends at, the level it
// reaches there and how it travels there from the level before. After the
// last point the level holds to the end of the cycle. The cycle's wrap, from
// the last level back to the first, is the duck itself on most curves: the
// cut from 1 to 0 on the beat.
//
// Two adjustments bend a curve by hand and are kept per curve: Slope moves
// the mobile points, the sloped part, left or right as far as the fixed
// points either side allow, and Shift moves the whole curve round the cycle.
namespace rhino::duck
{
constexpr int curveCount = 16;

struct CurveAdjust
{
    float slope = 0.0f;   // -1 to 1: the mobile points moved all the way left, or right
    float shift = 0.0f;   // -0.5 to 0.5 of a cycle

    bool operator== (const CurveAdjust& other) const noexcept
    {
        return slope == other.slope && shift == other.shift;
    }
};

enum class Family { sidechain, subBass, kickTrim, reverse, rhythm };

// How a segment travels from the level before it to its own.
enum class Shape { linear, smooth, easeOut, easeIn };

struct Point
{
    float at = 1.0f;        // the phase the segment ends at
    float level = 1.0f;     // the gain reached there
    Shape shape = Shape::linear;
    bool mobile = false;    // moves with Slope
};

struct CurveDef
{
    const char* name;
    Family family;
    float start;            // the gain at phase 0
    int count;
    std::array<Point, 6> points;
};

// Families in the order the grid shows them: the first eight for ducking a
// bassline under a kick, two with slower rises for a sub, two that cut a
// kick's own tail short, two that suck a sound away mid-cycle, two that pulse.
inline constexpr std::array<CurveDef, curveCount> curves { {
    { "Classic", Family::sidechain, 0.0f, 2, { { { 0.06f, 0.0f, Shape::linear, true }, { 0.52f, 1.0f, Shape::smooth, true } } } },
    { "Hook", Family::sidechain, 0.14f, 3, { { { 0.05f, 0.0f, Shape::smooth, false }, { 0.12f, 0.0f, Shape::linear, true }, { 0.60f, 1.0f, Shape::smooth, true } } } },
    { "Tight", Family::sidechain, 0.0f, 2, { { { 0.10f, 0.0f, Shape::linear, true }, { 0.40f, 1.0f, Shape::smooth, true } } } },
    { "Late", Family::sidechain, 0.0f, 2, { { { 0.20f, 0.0f, Shape::linear, true }, { 0.68f, 1.0f, Shape::smooth, true } } } },
    { "Ramp", Family::sidechain, 0.0f, 2, { { { 0.05f, 0.0f, Shape::linear, true }, { 0.60f, 1.0f, Shape::linear, true } } } },
    { "Soft", Family::sidechain, 0.0f, 2, { { { 0.02f, 0.0f, Shape::linear, true }, { 0.75f, 1.0f, Shape::easeIn, true } } } },
    { "Snap", Family::sidechain, 0.0f, 2, { { { 0.10f, 0.0f, Shape::linear, true }, { 0.50f, 1.0f, Shape::easeOut, true } } } },
    { "Long", Family::sidechain, 0.0f, 2, { { { 0.14f, 0.0f, Shape::linear, true }, { 0.86f, 1.0f, Shape::smooth, true } } } },
    { "Sub Slow", Family::subBass, 0.0f, 4, { { { 0.10f, 0.0f, Shape::linear, true }, { 0.74f, 1.0f, Shape::smooth, true }, { 0.90f, 1.0f, Shape::linear, false }, { 1.00f, 0.0f, Shape::smooth, false } } } },
    { "Sub Deep", Family::subBass, 0.0f, 4, { { { 0.18f, 0.0f, Shape::linear, true }, { 0.80f, 1.0f, Shape::easeIn, true }, { 0.92f, 1.0f, Shape::linear, false }, { 1.00f, 0.0f, Shape::smooth, false } } } },
    { "Trim", Family::kickTrim, 1.0f, 2, { { { 0.14f, 1.0f, Shape::linear, true }, { 0.30f, 0.0f, Shape::smooth, true } } } },
    { "Trim Slow", Family::kickTrim, 1.0f, 2, { { { 0.08f, 1.0f, Shape::linear, true }, { 0.70f, 0.0f, Shape::easeIn, true } } } },
    { "Suction", Family::reverse, 1.0f, 4, { { { 0.42f, 1.0f, Shape::linear, true }, { 0.62f, 0.05f, Shape::smooth, true }, { 0.78f, 0.05f, Shape::linear, true }, { 0.95f, 1.0f, Shape::smooth, true } } } },
    { "Suction Wide", Family::reverse, 1.0f, 4, { { { 0.28f, 1.0f, Shape::linear, true }, { 0.50f, 0.05f, Shape::easeIn, true }, { 0.72f, 0.05f, Shape::linear, true }, { 0.90f, 1.0f, Shape::easeOut, true } } } },
    { "Swell", Family::rhythm, 0.0f, 2, { { { 0.68f, 1.0f, Shape::smooth, true }, { 1.00f, 0.0f, Shape::smooth, false } } } },
    { "Double", Family::rhythm, 0.0f, 4, { { { 0.24f, 1.0f, Shape::smooth, true }, { 0.50f, 0.25f, Shape::smooth, true }, { 0.74f, 1.0f, Shape::smooth, true }, { 1.00f, 0.0f, Shape::smooth, false } } } },
} };

inline int clampCurve(int index) noexcept { return std::clamp(index, 0, curveCount - 1); }

inline const char* curveName(int index) noexcept { return curves[static_cast<size_t>(clampCurve(index))].name; }

inline Family curveFamily(int index) noexcept { return curves[static_cast<size_t>(clampCurve(index))].family; }

inline const char* familyName(Family family) noexcept
{
    switch (family)
    {
        case Family::sidechain: return "Sidechain";
        case Family::subBass:   return "Sub bass";
        case Family::kickTrim:  return "Kick trim";
        case Family::reverse:   return "Reverse";
        case Family::rhythm:    return "Rhythm";
    }
    return "";
}

inline float travel(Shape shape, float u) noexcept
{
    u = std::clamp(u, 0.0f, 1.0f);
    switch (shape)
    {
        case Shape::linear:  return u;
        case Shape::smooth:  return u * u * (3.0f - 2.0f * u);
        case Shape::easeOut: return 1.0f - (1.0f - u) * (1.0f - u);
        case Shape::easeIn:  return u * u;
    }
    return u;
}

// The room a curve's mobile points have: the phases they may be moved down
// to and up to without crossing a fixed point, and where they stand unmoved.
struct MobileRoom
{
    float lowest = 0.01f, highest = 0.99f;
    float first = 1.0f, last = 0.0f;
    bool any = false;
};

inline MobileRoom mobileRoom(const CurveDef& def) noexcept
{
    MobileRoom room;
    for (int i = 0; i < def.count; ++i)
    {
        const auto& point = def.points[static_cast<size_t>(i)];
        if (!point.mobile) continue;
        room.any = true;
        room.first = std::min(room.first, point.at);
        room.last = std::max(room.last, point.at);
    }
    if (!room.any) return room;
    for (int i = 0; i < def.count; ++i)
    {
        const auto& point = def.points[static_cast<size_t>(i)];
        if (point.mobile) continue;
        if (point.at < room.first) room.lowest = std::max(room.lowest, point.at + 0.02f);
        if (point.at > room.last) room.highest = std::min(room.highest, point.at - 0.02f);
    }
    return room;
}

// How far Slope has moved the mobile points, in phase. Negative is left.
inline float mobileOffset(int index, float slope) noexcept
{
    const auto room = mobileRoom(curves[static_cast<size_t>(clampCurve(index))]);
    if (!room.any) return 0.0f;
    slope = std::clamp(slope, -1.0f, 1.0f);
    return slope < 0.0f ? slope * (room.first - room.lowest) : slope * (room.highest - room.last);
}

// The Slope that puts the mobile points at this offset: what a drag on the
// sloped part writes.
inline float slopeForOffset(int index, float offset) noexcept
{
    const auto room = mobileRoom(curves[static_cast<size_t>(clampCurve(index))]);
    if (!room.any) return 0.0f;
    const auto roomBelow = room.first - room.lowest;
    const auto roomAbove = room.highest - room.last;
    if (offset < 0.0f) return roomBelow > 1.0e-6f ? std::clamp(offset / roomBelow, -1.0f, 0.0f) : 0.0f;
    return roomAbove > 1.0e-6f ? std::clamp(offset / roomAbove, 0.0f, 1.0f) : 0.0f;
}

// Where the sloped part stands on the display, in phase, adjustments applied.
// The span may run past 1: a shifted curve wraps, and the caller draws it
// modulo the cycle.
struct PhaseSpan
{
    float from = 0.0f, to = 0.0f;
};

inline PhaseSpan mobileSpan(int index, CurveAdjust adjust) noexcept
{
    const auto room = mobileRoom(curves[static_cast<size_t>(clampCurve(index))]);
    if (!room.any) return {};
    const auto offset = mobileOffset(index, adjust.slope);
    auto from = room.first + offset + adjust.shift;
    from -= std::floor(from);
    return { from, from + (room.last - room.first) };
}

// The gain of curve `index` at `phase`, bent by `adjust`. Pure, and the one
// function both the audio and every picture call.
inline float curveGain(int index, float phase, CurveAdjust adjust) noexcept
{
    const auto& def = curves[static_cast<size_t>(clampCurve(index))];
    const auto offset = mobileOffset(index, adjust.slope);
    auto t = phase - adjust.shift;
    t -= std::floor(t);
    auto fromAt = 0.0f;
    auto fromLevel = def.start;
    for (int i = 0; i < def.count; ++i)
    {
        const auto& point = def.points[static_cast<size_t>(i)];
        const auto at = point.mobile ? point.at + offset : point.at;
        if (t <= at)
        {
            const auto span = at - fromAt;
            const auto u = span > 1.0e-6f ? (t - fromAt) / span : 1.0f;
            return fromLevel + (point.level - fromLevel) * travel(point.shape, u);
        }
        fromAt = at;
        fromLevel = point.level;
    }
    return fromLevel;
}
}
