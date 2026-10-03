#include "ClipGeometryTest.h"
#include "../../ClipGeometry.h"
#include "../../ArrangementGrid.h"
#include "../../InfoHints.h"
#include "../../Playhead.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>

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

        // The ruler numbers bars; the row at the foot of the panel reads the
        // same bars as wall-clock times. A time is a far wider string, so the
        // two rows thin out at different zooms - and the one labelling less
        // often has to be labelling a subset of the other's bars, or a time
        // would stand under a bar with no number over it. Swept across four
        // decades of zoom at an irrational-ish ratio, so the step boundaries
        // are crossed rather than landed on.
        for (double pixelsPerBar = 0.5; pixelsPerBar < 4096.0; pixelsPerBar *= 1.37)
        {
            const auto numbers = barLabelStep(pixelsPerBar, barNumberMinimumPixels);
            const auto times = barLabelStep(pixelsPerBar, timeLabelMinimumPixels);
            if (times < numbers || !close(std::fmod(times, numbers), 0.0))
                throw std::runtime_error("Every labelled time sits on a numbered bar");
            if (numbers < 4096.0 && numbers * pixelsPerBar < barNumberMinimumPixels)
                throw std::runtime_error("Bar numbers are at least their minimum apart");
            if (times < 4096.0 && times * pixelsPerBar < timeLabelMinimumPixels)
                throw std::runtime_error("Times are at least their minimum apart");
            if (numbers > 1.0 && numbers * 0.5 * pixelsPerBar >= barNumberMinimumPixels)
                throw std::runtime_error("And neither row thins out further than it has to");

            // Wherever the view starts, a run of times starts on a bar the
            // number row labels too.
            const auto first = firstLabelledBar(37.0, times);
            if (first > 37.0 || first < 1.0 || !close(std::fmod(first - 1.0, numbers), 0.0))
                throw std::runtime_error("A run of times starts on a numbered bar at or before the view");

            // The marks between two times fill in as the view is zoomed in and
            // thin back out as it is zoomed out, and are always a binary
            // subdivision of the gap - so every label keeps a long mark and
            // every short one lands on a musical division.
            const auto marks = timeTicksPerLabel(pixelsPerBar, times);
            if (marks < 1 || marks > timeTickDivisions)
                throw std::runtime_error("A label is divided between once and timeTickDivisions times");
            if (marks > 1 && times * pixelsPerBar / marks < timeTickMinimumPixels)
                throw std::runtime_error("Ruler marks never crowd past their minimum spacing");
            if (marks < timeTickDivisions && times * pixelsPerBar / (marks * 2) >= timeTickMinimumPixels)
                throw std::runtime_error("And are as fine as that spacing allows");
        }

        // Inside a bar the ruler steps along one chain of spans in counts, each
        // dividing the one before it, so 4/4 is cut at the half bar and 6/8 at
        // the dotted quarter, and an odd bar drops straight to its counts.
        {
            const auto chainIs = [&close](double counts, std::vector<double> expected)
            {
                std::vector<double> chain;
                for (; counts >= rulerFinestSpan; counts = finerRulerSpan(counts))
                    chain.push_back(counts);
                return chain.size() == expected.size()
                    && std::equal(chain.begin(), chain.end(), expected.begin(), close);
            };
            if (!chainIs(4.0, {4.0, 2.0, 1.0, 0.5, 0.25}))
                throw std::runtime_error("A 4/4 bar halves to beats and then to sixteenths");
            if (!chainIs(6.0, {6.0, 3.0, 1.0, 0.5, 0.25}))
                throw std::runtime_error("A 6/8 bar is cut at the dotted quarter before the eighth");
            if (!chainIs(7.0, {7.0, 1.0, 0.5, 0.25}))
                throw std::runtime_error("An odd bar drops straight to its counts");
            if (!chainIs(24.0, {24.0, 12.0, 6.0, 3.0, 1.0, 0.5, 0.25}))
                throw std::runtime_error("Whole bars of 3/4 halve down to one bar before its counts");
        }
        for (const auto countsPerBar : {2, 3, 4, 5, 6, 7, 12})
            for (double pixelsPerCount = 1.0; pixelsPerCount < 2048.0; pixelsPerCount *= 1.37)
            {
                const auto labels = rulerLabelSpan(pixelsPerCount, countsPerBar, barNumberMinimumPixels);
                if (labels < 1.0 || !close(std::fmod(countsPerBar, labels), 0.0))
                    throw std::runtime_error("Count labels are a whole number of counts that divides the bar");
                if (labels < countsPerBar && labels * pixelsPerCount < barNumberMinimumPixels)
                    throw std::runtime_error("Count labels are at least a bar number's minimum apart");
                if (labels > 1.0 && finerRulerSpan(labels) * pixelsPerCount >= barNumberMinimumPixels)
                    throw std::runtime_error("And label as finely as that spacing allows");

                const auto ticks = rulerTickSpan(pixelsPerCount, labels);
                if (ticks < rulerFinestSpan || ticks > labels || !close(std::fmod(labels, ticks), 0.0))
                    throw std::runtime_error("Ruler marks divide the span between two labels");
                if (ticks < labels && ticks * pixelsPerCount < rulerTickMinimumPixels)
                    throw std::runtime_error("Ruler marks never crowd past their minimum spacing");
                if (ticks > rulerFinestSpan && finerRulerSpan(ticks) * pixelsPerCount >= rulerTickMinimumPixels)
                    throw std::runtime_error("And mark as finely as that spacing allows");
            }
        // Labels a few bars apart are marked at the bars between them before
        // anything finer, so a mark never lands off a bar line it could have
        // been on.
        if (!close(rulerTickSpan(16.0 / 4.0, 4.0 * 4.0), 4.0))
            throw std::runtime_error("Labels four bars apart are marked at each bar");
        // The Info View is written to by the hint that follows the pointer and
        // by the status line that reports what just happened. Which of them
        // wins is the whole of how the panel behaves, and both of its rules
        // are easy to invert by accident.
        {
            juce::Component control, other;
            const juce::String mute {"Mute Audio 2 - silences this track."};
            const juce::String solo {"Solo Audio 2 - silences the rest."};
            if (!infoHintReplaces(nullptr, {}, &control, mute))
                throw std::runtime_error("Reaching a control that explains itself writes the panel");
            if (infoHintReplaces(&control, mute, &control, mute))
                throw std::runtime_error("Resting on the same control does not rewrite it");
            if (!infoHintReplaces(&control, mute, &other, solo))
                throw std::runtime_error("Reaching a different control writes the panel");
            // Play becoming pause under a pointer that has not moved.
            if (!infoHintReplaces(&control, mute, &control, solo))
                throw std::runtime_error("A control that relabels itself is read again");
            // The pointer crossing a lane, a clip or the gap between two
            // buttons must not blank what the panel was last told - which is
            // also what keeps a status message on screen.
            if (infoHintReplaces(&control, mute, nullptr, {}))
                throw std::runtime_error("Nothing under the pointer leaves the panel alone");
            if (infoHintReplaces(nullptr, mute, &other, {}))
                throw std::runtime_error("A control that says nothing leaves the panel alone");
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
}
