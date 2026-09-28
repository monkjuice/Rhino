#include "ClipGeometryTest.h"
#include "../../ClipGeometry.h"
#include "../../ArrangementGrid.h"
#include "../../Playhead.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace rhino
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

        if (!canSplitClipAt(geometry, 1.0))
            throw std::runtime_error("A line inside a clip splits it");
        if (canSplitClipAt(geometry, 0.5) || canSplitClipAt(geometry, 2.0))
            throw std::runtime_error("A line outside a clip does not split it");
        if (canSplitClipAt(geometry, geometry.start) || canSplitClipAt(geometry, geometry.end))
            throw std::runtime_error("A line on a clip edge leaves nothing to cut");
        if (canSplitClipAt(geometry, geometry.start + minimumSplitSeconds * 0.5)
            || canSplitClipAt(geometry, geometry.end - minimumSplitSeconds * 0.5))
            throw std::runtime_error("A cut has to leave something playable on both sides");
        if (canSplitClipAt(geometry, std::nan("")))
            throw std::runtime_error("A line with no position splits nothing");

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

        // The wash alternates from bar zero, so a grid line can ask which shade
        // of lane it is about to be drawn on without repeating the band maths.
        if (isWashedBar(0.0, 1) || !isWashedBar(1.0, 1) || isWashedBar(2.0, 1))
            throw std::runtime_error("One bar to a band washes every other bar");
        if (isWashedBar(3.0, 4) || !isWashedBar(4.0, 4) || !isWashedBar(7.0, 4) || isWashedBar(8.0, 4))
            throw std::runtime_error("A wider band washes whole groups of bars");
        if (!isWashedBar(-1.0, 1) || isWashedBar(-2.0, 1))
            throw std::runtime_error("Bars before the start alternate the same way");
        if (isWashedBar(1.0, 0))
            throw std::runtime_error("A band of no bars washes nothing");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
}
