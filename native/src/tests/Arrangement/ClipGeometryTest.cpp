#include "ClipGeometryTest.h"
#include "../../ClipGeometry.h"
#include "../../ArrangementGrid.h"
#include "../../Playhead.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace theta
{
int runArrangementGeometryTest()
{
    try
    {
        const auto close = [](double a, double b) { return std::abs(a - b) < 0.0001; };
        const ClipGeometry geometry {0.75, 1.25, 0.25};
        if (!close(previewClipEdit(geometry, ClipGesture::trimLeft, -5, 1).start, 0.5))
            throw std::runtime_error("Left extension stops at source zero");
        if (!close(previewClipEdit(geometry, ClipGesture::trimRight, 99, 1).end, 1.5))
            throw std::runtime_error("Right extension stops at source end");
        if (!close(previewClipEdit(geometry, ClipGesture::move, -5, 1).start, 0))
            throw std::runtime_error("Move stops at timeline zero");

        const juce::Rectangle<int> lane {100, 20, 800, 200};
        if (!playheadDamage(-1.0f, -1.0f, lane).isEmpty())
            throw std::runtime_error("Hidden playheads cause no damage");
        if (playheadDamage(-1.0f, 150.25f, lane) != juce::Rectangle<int>(148, 20, 7, 200))
            throw std::runtime_error("Fractional playhead damage rounds outwards");
        if (playheadDamage(150.25f, 155.75f, lane) != juce::Rectangle<int>(148, 20, 12, 200))
            throw std::runtime_error("Playhead movement covers old and new positions");
        if (playheadDamage(-1.0f, 99.0f, lane) != juce::Rectangle<int>(100, 20, 3, 200))
            throw std::runtime_error("Playhead damage stays inside its lane");

        if (!close(gridDivisionBeats(GridDivision::bar, 3.5), 3.5))
            throw std::runtime_error("Grid bars use the active meter");
        if (static_cast<int>(adaptiveGridDivision(160.0, 4.0, AdaptiveGridWidth::medium))
                <= static_cast<int>(adaptiveGridDivision(16.0, 4.0, AdaptiveGridWidth::medium)))
            throw std::runtime_error("Adaptive grid follows logical pixel density");
        if (narrowerGridDivision(GridDivision::sixtyFourth) != GridDivision::sixtyFourth
            || widerGridDivision(GridDivision::eightBars) != GridDivision::eightBars)
            throw std::runtime_error("Grid width commands clamp safely");
        GridSettings fixed {GridMode::fixed, AdaptiveGridWidth::medium, GridDivision::eighth, false};
        if (!close(resolvedGridBeats(fixed, 1.0, 4.0), resolvedGridBeats(fixed, 1000.0, 4.0)))
            throw std::runtime_error("Fixed grid ignores zoom");
        fixed.triplet = true;
        if (!close(resolvedGridBeats(fixed, 100.0, 4.0), 1.0 / 3.0))
            throw std::runtime_error("Triplet grid is two thirds of straight duration");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
}
