#pragma once
#include "dsp/MultibandDynamicsEngine.h"
#include <algorithm>
#include <cmath>

// The arithmetic of Multiband Dynamics' display, free of JUCE and of Session
// so it is tested on its own (tests/MultibandDynamicsTest.cpp). The face lays
// out, draws and hit-tests the display with it (DeviceEditorPanelMultiband.cpp).
//
// Each band is a row across the levels Live's display spans, -80 to 0 dB. The
// Below block runs from the left end to the lower threshold and the Above
// block from the upper threshold to the right end. A block's edge is its
// threshold, dragged sideways; its "volume" is its ratio, dragged up for
// louder and down for quieter, as Live's are: up in the Above block expands
// upwards and down compresses, up in the Below block compresses upwards and
// down expands downwards.
namespace rhino::multibanddisplay
{
inline constexpr float floorDb = -80.0f, ceilingDb = 0.0f;
// How near a block's edge a press takes the edge rather than the block.
inline constexpr float edgeReach = 4.0f;

// Where a level stands across a row from `left`, `width` wide, the span's
// ends holding anything beyond them.
inline float xForDb(float decibels, float left, float width)
{
    return left + width * (std::clamp(decibels, floorDb, ceilingDb) - floorDb) / (ceilingDb - floorDb);
}

inline float dbForX(float x, float left, float width)
{
    return width > 0.0f ? std::clamp(floorDb + (x - left) / width * (ceilingDb - floorDb), floorDb, ceilingDb)
                        : floorDb;
}

// The slope a ratio takes from the level's distance past its threshold, the
// engine's own: 1 at an infinite ratio, 0 at 1:1, and down to minimumRatio's
// -3 for the steepest expansion.
inline float slopeOf(float ratio)
{
    return static_cast<float>(MultibandDynamicsEngine::slopeOf(ratio));
}

inline constexpr float steepestExpansion = 1.0f - 1.0f / MultibandDynamicsEngine::minimumRatio;
// The last slope a finite ratio reaches; anything past it reads as infinite.
inline constexpr float steepestFiniteSlope = 1.0f - 1.0f / MultibandDynamicsEngine::infiniteRatio;

// A block's volume, -1 (all the way down) to 1 (all the way up), 0 at 1:1.
// Compression takes the half of the travel it turns the block's signals
// towards, its slope as the distance; expansion the other half, scaled so the
// steepest reaches the end. In the Above block compression turns loud signals
// down, so it is the lower half; in the Below block it turns quiet signals
// up, so it is the upper.
inline float volumeForRatio(float ratio, bool above)
{
    const auto slope = slopeOf(ratio);
    const auto compressing = slope >= 0.0f;
    const auto distance = compressing ? slope : slope / steepestExpansion;
    return above == compressing ? -distance : distance;
}

inline float ratioForVolume(float volume, bool above)
{
    const auto v = std::clamp(volume, -1.0f, 1.0f);
    const auto compressing = above ? v <= 0.0f : v >= 0.0f;
    const auto slope = compressing ? std::abs(v) : std::abs(v) * steepestExpansion;
    if (slope >= steepestFiniteSlope)
        return MultibandDynamicsEngine::infiniteRatio;
    return std::clamp(1.0f / (1.0f - slope), MultibandDynamicsEngine::minimumRatio,
                      MultibandDynamicsEngine::infiniteRatio);
}

// What a press on a band's row takes hold of.
enum class Grip
{
    none,
    belowEdge,
    aboveEdge,
    belowBlock,
    aboveBlock
};

// `belowEdge` and `aboveEdge` are where the two thresholds stand on the row,
// the lower never right of the upper. An edge within reach beats its block;
// with both within reach, the nearer, and with the two on top of each other,
// the side of them the press is on. Between the edges is neither block.
inline Grip gripAt(float x, float belowEdge, float aboveEdge, float reach = edgeReach)
{
    const auto toBelow = std::abs(x - belowEdge), toAbove = std::abs(x - aboveEdge);
    if (toBelow <= reach || toAbove <= reach)
    {
        if (toBelow == toAbove)
            return x < belowEdge ? Grip::belowEdge : Grip::aboveEdge;
        return toBelow < toAbove ? Grip::belowEdge : Grip::aboveEdge;
    }
    if (x < belowEdge)
        return Grip::belowBlock;
    if (x > aboveEdge)
        return Grip::aboveBlock;
    return Grip::none;
}
}
