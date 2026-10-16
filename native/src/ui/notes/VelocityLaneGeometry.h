#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

// The velocity lane's arithmetic, free of JUCE and of Session so it is tested
// on its own (tests/VelocityLaneTest.cpp). StepGrid lays out, draws and
// hit-tests the lane with it.
namespace rhino::velocitylane
{
// The band between the rows and the lane that drags to resize it; its rule
// sits on the middle pixel.
inline constexpr float dividerHeight = 5.0f;
inline constexpr float defaultHeight = 64.0f;
// Room for the 127 and 1 labels. A lane that cannot have this folds away
// rather than squeezing the pitch rows.
inline constexpr float minimumHeight = 32.0f;
// One pixel per velocity step on the plot; a taller lane is no more precise.
inline constexpr float maximumHeight = 136.0f;
// Keeps a dot at 127 or 1 inside the lane, hovered ring included.
inline constexpr float valueInset = 5.0f;
// Below this the gutter keeps only its 127 and 1 labels, and the hovered
// note's value without its name.
inline constexpr float midLabelHeight = 44.0f;
// How near a press must be to a marker's dot, or to the bar that runs the
// note's length, to take it. A dot beats its bar at the same distance.
inline constexpr float headReach = 6.0f, barReach = 4.0f, barBias = 0.5f;
// Two markers this close to equally near are a tie, decided by rank.
inline constexpr float tieWindow = 0.25f;

// The lane's laid-out height in whole pixels: 0 when hidden, or when giving
// it room would push the pitch rows below rowsFloor - sixteen rows that keep
// their names. Whole pixels keep the divider and the lane on pixel rows.
inline float laidOutHeight(bool shown, float requested, float available, float rowsFloor)
{
    if (!shown)
        return 0.0f;
    const auto room = available - dividerHeight - rowsFloor;
    const auto height = std::floor(std::min(std::clamp(requested, minimumHeight, maximumHeight), room));
    return height >= minimumHeight ? height : 0.0f;
}

// 127 at the plot's top and 1 at its bottom. Velocity 0 is a note-off and is
// never written, so anything below 1 is drawn on the bottom rail.
inline float yForVelocity(float velocity, float plotTop, float plotHeight)
{
    return plotTop + (127.0f - std::clamp(velocity, 1.0f, 127.0f)) / 126.0f * plotHeight;
}

// Continuous, held to 1-127 however far outside the plot the pointer is.
inline float valueAtY(float y, float plotTop, float plotHeight)
{
    return std::clamp(127.0f - (y - plotTop) / plotHeight * 126.0f, 1.0f, 127.0f);
}

inline int clampVelocity(double value)
{
    return static_cast<int>(std::clamp(std::lround(value), 1L, 127L));
}

inline int velocityAtY(float y, float plotTop, float plotHeight)
{
    return clampVelocity(valueAtY(y, plotTop, plotHeight));
}

// A group drag's accumulated delta, held between the two rails. Pushed past
// one the group equalises there, and because the delta itself stops at the
// rail, dragging back answers at once and the group's spacing returns.
inline double clampGroupDelta(const std::vector<int>& origins, double delta)
{
    if (origins.empty())
        return 0.0;
    const auto [low, high] = std::minmax_element(origins.begin(), origins.end());
    return std::clamp(delta, 1.0 - *high, 127.0 - *low);
}

inline int draggedVelocity(int origin, double delta)
{
    return clampVelocity(origin + delta);
}

// The value a stroke from (fromStep, fromValue) to (toStep, toValue) has at
// `at`, held inside the segment. A stroke that has not moved gives toValue.
inline float strokeValueAt(double fromStep, float fromValue, double toStep, float toValue, double at)
{
    if (std::abs(toStep - fromStep) < 1.0e-9)
        return toValue;
    const auto t = (std::clamp(at, std::min(fromStep, toStep), std::max(fromStep, toStep)) - fromStep)
                 / (toStep - fromStep);
    return fromValue + static_cast<float>(t) * (toValue - fromValue);
}

// What one step column takes from a stroke segment: the column under the
// pointer takes the pointer's value, because a pencil follows the pointer,
// and a column the segment only crossed takes the value at its centre. That
// is what lets a fast stroke skip no column.
inline float columnValue(double fromStep, float fromValue, double toStep, float toValue, int column)
{
    if (static_cast<int>(std::floor(toStep)) == column)
        return toValue;
    return strokeValueAt(fromStep, fromValue, toStep, toValue, column + 0.5);
}

struct ColumnSpan
{
    int first = 0, last = -1;
    bool empty() const { return last < first; }
};

// The step columns a stroke segment crosses, held to the visible ones.
inline ColumnSpan strokeColumns(double fromStep, double toStep, int firstVisible, int lastVisible)
{
    return {std::max(firstVisible, static_cast<int>(std::floor(std::min(fromStep, toStep)))),
            std::min(lastVisible, static_cast<int>(std::floor(std::max(fromStep, toStep))))};
}

struct Marker
{
    float headX = 0.0f, endX = 0.0f, y = 0.0f;
    bool selected = false, inWindow = true;
};

// The marker a press at (x, y) takes, or -1. The nearest wins: a dot within
// headReach, or a bar within barReach of its line while x is inside the note.
// Only a tie is decided by rank - selected before unselected, then a note in
// the pitch window before one scrolled out of it, then the later index, which
// is drawn on top - so a selected marker never steals a press that lands
// squarely on another one. A dot left of leftEdge is under the gutter and
// cannot be taken; its bar still can.
inline int pickMarker(const std::vector<Marker>& markers, float x, float y, float leftEdge)
{
    int best = -1, bestRank = -1;
    auto bestDistance = std::numeric_limits<float>::max();
    for (int index = 0; index < static_cast<int>(markers.size()); ++index)
    {
        const auto& marker = markers[static_cast<size_t>(index)];
        const auto dy = std::abs(y - marker.y);
        auto distance = std::numeric_limits<float>::max();
        if (marker.headX >= leftEdge && std::abs(x - marker.headX) <= headReach && dy <= headReach)
            distance = std::hypot(x - marker.headX, y - marker.y);
        if (x >= std::max(marker.headX, leftEdge) && x <= marker.endX && dy <= barReach)
            distance = std::min(distance, dy + barBias);
        if (distance == std::numeric_limits<float>::max())
            continue;
        const auto rank = (marker.selected ? 2 : 0) + (marker.inWindow ? 1 : 0);
        if (best < 0 || distance < bestDistance - tieWindow)
        {
            best = index;
            bestRank = rank;
            bestDistance = distance;
        }
        // The nearest distance seen is kept on a tie, so a chain of near-ties
        // cannot drift outwards one window at a time.
        else if (std::abs(distance - bestDistance) <= tieWindow && rank >= bestRank)
        {
            best = index;
            bestRank = rank;
            bestDistance = std::min(bestDistance, distance);
        }
    }
    return best;
}
}
