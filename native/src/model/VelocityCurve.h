#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

// How hard a keyboard's keys have to be struck to play loud.
//
// Some keyboards, an old Launchkey Mini among them, only reach a high
// velocity when a key is hit hard, and their firmware has no curve to change
// that. Rhino bends each note-on as it arrives from the port, before any track
// hears or records it, so what is recorded is what was heard. A pure header,
// so the curves are checked without a Session (tests/VelocityCurveTest.cpp).

namespace rhino
{

enum class VelocityCurve { softest, softer, soft, linear, hard, harder, fixed };

struct VelocityCurveInfo
{
    VelocityCurve curve;
    // What the settings file stores. A word rather than a position, so a
    // curve added to the list later does not change what an old file meant.
    std::string_view id;
    std::string_view name;
    // velocity out = 127 * (velocity in / 127) ^ exponent. Below 1 a lighter
    // touch plays louder; above 1 it takes a harder one.
    double exponent;
};

// In the order the chooser lists them, from the lightest touch to the hardest.
inline constexpr std::array<VelocityCurveInfo, 7> velocityCurves {{
    {VelocityCurve::softest, "softest", "Softest", 0.35},
    {VelocityCurve::softer, "softer", "Softer", 0.5},
    {VelocityCurve::soft, "soft", "Soft", 0.7},
    {VelocityCurve::linear, "linear", "Linear", 1.0},
    {VelocityCurve::hard, "hard", "Hard", 1.5},
    {VelocityCurve::harder, "harder", "Harder", 2.2},
    {VelocityCurve::fixed, "fixed", "Fixed at 127", 0.0},
}};

inline const VelocityCurveInfo& velocityCurveInfo(VelocityCurve curve)
{
    for (const auto& info : velocityCurves)
        if (info.curve == curve)
            return info;
    return velocityCurves[3];
}

// Linear for anything it does not recognise, which is what an absent setting
// means too.
inline VelocityCurve velocityCurveFromId(std::string_view id)
{
    for (const auto& info : velocityCurves)
        if (info.id == id)
            return info.curve;
    return VelocityCurve::linear;
}

// A note-on's velocity through the curve. A struck key never comes out at 0,
// because a note-on at velocity 0 is a note-off; and 0 stays 0 for the same
// reason.
inline int applyVelocityCurve(VelocityCurve curve, int velocity)
{
    if (velocity <= 0)
        return 0;
    velocity = std::min(velocity, 127);
    if (curve == VelocityCurve::fixed)
        return 127;
    if (curve == VelocityCurve::linear)
        return velocity;
    const auto shaped = 127.0 * std::pow(velocity / 127.0, velocityCurveInfo(curve).exponent);
    return std::clamp(static_cast<int>(std::lround(shaped)), 1, 127);
}

}
