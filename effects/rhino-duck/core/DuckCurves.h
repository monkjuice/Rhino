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
// Five adjustments bend a curve by hand and are kept per curve, so a curve
// button is a starting point rather than a fixed shape:
//
// - Hold moves the mobile points, the sloped part, left or right as far as
//   the fixed points either side allow: how long the curve holds before it.
// - Release stretches the sloped part from its start, within the same room.
// - Shift moves the whole curve round the cycle.
// - Attack eases the cut on the beat: from the level the cycle ended at into
//   the curve, over its first stretch.
// - Depth keeps that much of the curve's dip, 1 the whole of it.
//
// Hold, Release and Attack are phases here. The processor keeps Attack in
// milliseconds and converts it at the current cycle's length.
namespace rhino::duck
{
constexpr int curveCount = 16;
constexpr float shortestRelease = 0.01f;   // the sloped part's least length, in phase
constexpr float longestAttack = 0.5f;      // in phase

struct CurveAdjust
{
    float hold = 0.0f;      // phase the sloped part has been moved by: negative is earlier
    float shift = 0.0f;     // -0.5 to 0.5 of a cycle
    float release = 1.0f;   // the sloped part's length, as a multiple of its design
    float attack = 0.0f;    // phase the fall into the curve takes from the beat
    float depth = 1.0f;     // how much of its dip the curve keeps, 0 to 1

    bool operator== (const CurveAdjust&) const noexcept = default;
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
    // The rhythm curves rise from the beat itself, so their sloped part
    // starts with a point at phase 0: Hold can then delay the rise, and
    // Release has a length to stretch.
    { "Swell", Family::rhythm, 0.0f, 3, { { { 0.0f, 0.0f, Shape::linear, true }, { 0.68f, 1.0f, Shape::smooth, true }, { 1.00f, 0.0f, Shape::smooth, false } } } },
    { "Double", Family::rhythm, 0.0f, 5, { { { 0.0f, 0.0f, Shape::linear, true }, { 0.24f, 1.0f, Shape::smooth, true }, { 0.50f, 0.25f, Shape::smooth, true }, { 0.74f, 1.0f, Shape::smooth, true }, { 1.00f, 0.0f, Shape::smooth, false } } } },
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
    // A sloped part designed to start on the beat may stay there.
    room.lowest = std::min(room.lowest, room.first);
    room.highest = std::max(room.highest, room.last);
    return room;
}

inline const CurveDef& curveDef(int index) noexcept { return curves[static_cast<size_t>(clampCurve(index))]; }

// Where Hold and Release leave the sloped part, in the curve's own time,
// before Shift: where its first mobile point stands, how long it runs to the
// last, and that length over the design's.
struct SlopedPart
{
    float start = 0.0f, length = 0.0f, scale = 1.0f;
    bool any = false;
};

inline SlopedPart slopedPart(const MobileRoom& room, CurveAdjust adjust) noexcept
{
    if (!room.any) return {};
    const auto design = room.last - room.first;
    if (design <= 1.0e-6f)
        return { std::clamp(room.first + adjust.hold, room.lowest, room.highest), 0.0f, 1.0f, true };
    const auto length = std::clamp(design * adjust.release, std::min(design, shortestRelease), room.highest - room.lowest);
    const auto start = std::clamp(room.first + adjust.hold, room.lowest, std::max(room.lowest, room.highest - length));
    return { start, length, length / design, true };
}

inline SlopedPart slopedPart(int index, CurveAdjust adjust) noexcept
{
    return slopedPart(mobileRoom(curveDef(index)), adjust);
}

// A stretch of phase. It may run past 1 where a shifted curve wraps; the
// caller draws it modulo the cycle.
struct PhaseSpan
{
    float from = 0.0f, to = 0.0f;
};

// Where the sloped part may start, given its length: the Hold knob's travel.
inline PhaseSpan holdRoom(int index, CurveAdjust adjust) noexcept
{
    const auto room = mobileRoom(curveDef(index));
    if (!room.any) return {};
    const auto part = slopedPart(room, adjust);
    return { room.lowest, std::max(room.lowest, room.highest - part.length) };
}

// How long the sloped part may run, given its start: the Release knob's.
inline PhaseSpan releaseRoom(int index, CurveAdjust adjust) noexcept
{
    const auto room = mobileRoom(curveDef(index));
    const auto design = room.last - room.first;
    if (!room.any || design <= 1.0e-6f) return {};
    const auto part = slopedPart(room, adjust);
    const auto shortest = std::min(design, shortestRelease);
    return { shortest, std::max(shortest, room.highest - part.start) };
}

// The adjustment that starts the sloped part at `start`, kept in its room:
// what the Hold knob and a drag on the sloped part write.
inline CurveAdjust withHold(int index, CurveAdjust adjust, float start) noexcept
{
    const auto room = mobileRoom(curveDef(index));
    if (!room.any) return adjust;
    const auto span = holdRoom(index, adjust);
    adjust.hold = std::clamp(start, span.from, span.to) - room.first;
    return adjust;
}

// The adjustment that makes the sloped part `length` long, kept in its room:
// what the Release knob and a drag on its end write.
inline CurveAdjust withRelease(int index, CurveAdjust adjust, float length) noexcept
{
    const auto room = mobileRoom(curveDef(index));
    const auto design = room.last - room.first;
    if (!room.any || design <= 1.0e-6f) return adjust;
    const auto span = releaseRoom(index, adjust);
    adjust.release = std::clamp(length, span.from, span.to) / design;
    return adjust;
}

// Where the sloped part stands on the display, in phase, every adjustment
// applied.
inline PhaseSpan mobileSpan(int index, CurveAdjust adjust) noexcept
{
    const auto part = slopedPart(index, adjust);
    if (!part.any) return {};
    auto from = part.start + adjust.shift;
    from -= std::floor(from);
    return { from, from + part.length };
}

// The level a curve holds at the end of its cycle, which the next cycle's
// Attack falls from.
inline float endLevel(const CurveDef& def) noexcept
{
    return def.count > 0 ? def.points[static_cast<size_t>(def.count - 1)].level : def.start;
}

// The gain of curve `index` at `phase`, bent by `adjust`. Pure, and the one
// function both the audio and every picture call.
inline float curveGain(int index, float phase, CurveAdjust adjust) noexcept
{
    const auto& def = curveDef(index);
    const auto room = mobileRoom(def);
    const auto part = slopedPart(room, adjust);
    auto t = phase - adjust.shift;
    t -= std::floor(t);

    auto level = endLevel(def);
    auto fromAt = 0.0f;
    auto fromLevel = def.start;
    for (int i = 0; i < def.count; ++i)
    {
        const auto& point = def.points[static_cast<size_t>(i)];
        const auto at = point.mobile ? part.start + (point.at - room.first) * part.scale : point.at;
        if (t <= at)
        {
            const auto span = at - fromAt;
            const auto u = span > 1.0e-6f ? (t - fromAt) / span : 1.0f;
            level = fromLevel + (point.level - fromLevel) * travel(point.shape, u);
            break;
        }
        fromAt = at;
        fromLevel = point.level;
    }

    const auto attack = std::clamp(adjust.attack, 0.0f, longestAttack);
    if (attack > 1.0e-6f && t < attack)
    {
        const auto from = endLevel(def);
        level = from + (level - from) * travel(Shape::smooth, t / attack);
    }
    return 1.0f - std::clamp(adjust.depth, 0.0f, 1.0f) * (1.0f - level);
}
}
