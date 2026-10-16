#include "VelocityLaneTest.h"
#include "ui/notes/VelocityLaneGeometry.h"
#include <cmath>
#include <stdexcept>
#include <string>

namespace rhino
{
namespace
{
void require(bool condition, const std::string& what)
{
    if (!condition)
        throw std::runtime_error("Velocity lane: " + what);
}

bool closeTo(double a, double b) { return std::abs(a - b) < 1.0e-4; }
}

void checkVelocityLane()
{
    using namespace velocitylane;

    // Sixteen rows of 8 px are the floor the lane gives way to.
    constexpr auto rows = 128.0f;
    for (const auto available : {250.0f, 236.0f, 211.0f, 197.0f, 200.0f})
        require(laidOutHeight(true, 64.0f, available, rows) == 64.0f, "A pane with room lays the lane out as asked");
    require(laidOutHeight(true, 64.0f, 186.0f, rows) == 53.0f, "A pane short of room shrinks the lane to what is left");
    require(laidOutHeight(true, 64.0f, 169.0f, rows) == 36.0f, "and keeps shrinking it");
    require(laidOutHeight(true, 64.0f, 165.0f, rows) == 32.0f, "down to its minimum");
    for (const auto available : {164.0f, 155.0f, 152.0f, 62.0f})
        require(laidOutHeight(true, 64.0f, available, rows) == 0.0f,
                "A lane that cannot have its minimum folds away instead of squeezing the rows");
    require(laidOutHeight(false, 64.0f, 250.0f, rows) == 0.0f, "A hidden lane takes no room");
    require(laidOutHeight(true, 500.0f, 400.0f, rows) == maximumHeight, "No lane is taller than its maximum");
    require(laidOutHeight(true, 10.0f, 250.0f, rows) == minimumHeight, "No lane is shorter than its minimum");
    require(laidOutHeight(true, 100.0f, 250.0f, rows) == 100.0f, "A lane between the two is as asked");
    for (int available = 0; available <= 600; available += 3)
        for (int requested = 0; requested <= 300; requested += 7)
        {
            const auto height = laidOutHeight(true, static_cast<float>(requested), static_cast<float>(available), rows);
            require(height == 0.0f
                        || (height >= minimumHeight && height <= maximumHeight && height == std::floor(height)
                            && available - height - dividerHeight >= rows),
                    "Every laid-out lane is whole pixels, in range, and leaves the rows their floor");
        }

    for (const auto plot : {22.0f, 54.0f, 126.0f})
    {
        for (int velocity = 1; velocity <= 127; ++velocity)
        {
            require(velocityAtY(yForVelocity(static_cast<float>(velocity), 10.0f, plot), 10.0f, plot) == velocity,
                    "Every velocity reads back from where it is drawn");
            require(velocity == 1 || yForVelocity(static_cast<float>(velocity), 10.0f, plot)
                                         < yForVelocity(static_cast<float>(velocity - 1), 10.0f, plot),
                    "A louder note is drawn higher");
        }
        require(yForVelocity(127.0f, 10.0f, plot) == 10.0f && yForVelocity(1.0f, 10.0f, plot) == 10.0f + plot,
                "127 is the plot's top and 1 its bottom");
        require(yForVelocity(0.0f, 10.0f, plot) == yForVelocity(1.0f, 10.0f, plot), "A stored 0 is drawn on the bottom rail");
        require(valueAtY(-50.0f, 10.0f, plot) == 127.0f && valueAtY(500.0f, 10.0f, plot) == 1.0f,
                "A pointer past either edge reads as that edge");
    }
    for (int tenth = -10000; tenth <= 10000; tenth += 3)
    {
        const auto velocity = clampVelocity(tenth / 10.0);
        require(velocity >= 1 && velocity <= 127, "Nothing writes a note-off or a value past 127");
    }

    // Pushed into a rail, a group goes on until every note is on it, which is
    // how Live equalises a group: drag it to the top, then back to the value.
    require(closeTo(clampGroupDelta({80, 50, 100}, 200.0), 77.0), "A group pushed up stops once even its quietest is at 127");
    require(closeTo(clampGroupDelta({80, 50, 100}, -200.0), -99.0), "and pushed down once even its loudest is at 1");
    require(closeTo(clampGroupDelta({80, 50, 100}, 10.0), 10.0), "A delta inside the rails is kept");
    require(clampGroupDelta({}, 10.0) == 0.0, "An empty group moves nowhere");
    require(draggedVelocity(100, 27.4) == 127 && draggedVelocity(40, -99.0) == 1, "A dragged velocity stays in range");
    {
        const std::vector<int> origins {20, 64, 120};
        const auto pushed = clampGroupDelta(origins, 40.0);
        require(draggedVelocity(120, pushed) == 127, "The loudest of a group meets the rail");
        const auto back = clampGroupDelta(origins, pushed - pushed);
        for (const auto origin : origins)
            require(draggedVelocity(origin, back) == origin, "and dragging back restores the group's spacing");
    }

    require(closeTo(columnValue(0.5, 40.0f, 8.5, 120.0f, 4), 80.0), "A column a stroke crosses takes the value at its centre");
    require(closeTo(columnValue(0.5, 40.0f, 8.5, 120.0f, 8), 120.0), "The column under the pointer takes the pointer's value");
    require(closeTo(columnValue(0.5, 40.0f, 8.5, 120.0f, 0), 40.0), "The column the stroke left takes its own end");
    require(closeTo(columnValue(8.5, 120.0f, 0.5, 40.0f, 4), 80.0)
                && closeTo(columnValue(8.5, 120.0f, 0.5, 40.0f, 0), 40.0)
                && closeTo(columnValue(8.5, 120.0f, 0.5, 40.0f, 8), 120.0),
            "A stroke drawn leftwards gives the same columns");
    require(closeTo(columnValue(3.2, 50.0f, 3.1, 90.0f, 3), 90.0), "Inside one column the pointer's value wins");
    require(closeTo(columnValue(2.5, 60.0f, 2.5, 60.0f, 2), 60.0), "A stroke that has not moved gives its value");
    require(closeTo(strokeValueAt(0.0, 20.0f, 10.0, 100.0f, 5.0), 60.0), "Halfway along a stroke is the mean of its ends");
    {
        const auto span = strokeColumns(1.2, 8.7, 0, 15);
        const auto reversed = strokeColumns(8.7, 1.2, 0, 15);
        require(span.first == 1 && span.last == 8 && reversed.first == 1 && reversed.last == 8,
                "A stroke crosses the columns between its ends, whichever way it runs");
        const auto clipped = strokeColumns(-3.0, 2.5, 0, 15);
        const auto pastEnd = strokeColumns(14.5, 40.0, 0, 15);
        require(clipped.first == 0 && clipped.last == 2 && pastEnd.first == 14 && pastEnd.last == 15,
                "and only the visible ones");
        require(strokeColumns(20.0, 30.0, 0, 15).empty(), "A stroke wholly out of view crosses nothing");
    }

    const auto marker = [](float x, float end, float y, bool selected = false, bool inWindow = true)
    {
        return Marker {x, end, y, selected, inWindow};
    };
    require(pickMarker({marker(100, 100, 50), marker(110, 110, 50)}, 108.0f, 50.0f, 54.0f) == 1,
            "The nearer of two dots is taken");
    require(pickMarker({marker(100, 100, 50), marker(90, 140, 52)}, 100.0f, 51.0f, 54.0f) == 0,
            "A dot beats a bar the pointer is as close to");
    require(pickMarker({marker(100, 140, 50), marker(100, 140, 50), marker(100, 140, 50)}, 100.0f, 50.0f, 54.0f) == 2,
            "Among identical stacked dots the one drawn on top is taken");
    require(pickMarker({marker(100, 140, 50), marker(100, 140, 50, true), marker(100, 140, 50)}, 100.0f, 50.0f, 54.0f) == 1,
            "a selected one before it");
    require(pickMarker({marker(100, 140, 50, false, true), marker(100, 140, 50, false, false)}, 100.0f, 50.0f, 54.0f) == 0,
            "and a note in the pitch window before one scrolled out of it");
    require(pickMarker({marker(100, 140, 50), marker(105, 140, 50, true)}, 100.0f, 50.0f, 54.0f) == 0,
            "Selection breaks a tie; it never steals a press that lands on another dot");
    require(pickMarker({marker(100, 140, 50)}, 120.0f, 51.0f, 54.0f) == 0
                && pickMarker({marker(100, 140, 50)}, 141.0f, 51.0f, 54.0f) < 0
                && pickMarker({marker(100, 140, 50)}, 93.0f, 51.0f, 54.0f) < 0,
            "A bar is taken only along the note");
    require(pickMarker({marker(50, 90, 50)}, 52.0f, 50.0f, 54.0f) < 0
                && pickMarker({marker(50, 90, 50)}, 60.0f, 50.0f, 54.0f) == 0,
            "A dot under the gutter cannot be taken, but its bar can");
    require(pickMarker({marker(100, 140, 50)}, 100.0f, 62.0f, 54.0f) < 0, "Empty lane takes nothing");
    require(pickMarker({marker(100, 140, 51.0f), marker(100, 140, 51.2f), marker(100, 140, 51.4f)}, 100.0f, 50.0f, 54.0f) == 1,
            "A chain of near-ties cannot drift outwards");
}
}
