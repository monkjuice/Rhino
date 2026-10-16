#include "StepGridInternal.h"
#include <algorithm>
#include <cmath>

// The velocity lane's pointer: the cursor, hover, a marker drag, a stroke in
// draw mode, the lane's marquee and the divider. StepGridVelocity.cpp says
// what the lane is.

namespace rhino
{

bool StepGrid::updateLanePointer(juce::Point<float> point, const juce::ModifierKeys& modifiers)
{
    const auto overDivider = isOverLaneDivider(point);
    if (overDivider != dividerHot)
    {
        dividerHot = overDivider;
        repaint(laneDividerBounds().getSmallestIntegerContainer());
    }
    if (!isOverLaneBlock(point))
        return false;
    if (overDivider)
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    else if (point.x < labelWidth)
        setMouseCursor(juce::MouseCursor::NormalCursor);
    else if (drawMode)
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
    else if (velocityMarkerAt(point) >= 0)
        // Up and down, as an automation node in the arrangement.
        setMouseCursor(isMultiSelectModifier(modifiers) ? juce::MouseCursor::NormalCursor
                                                        : juce::MouseCursor::UpDownResizeCursor);
    else
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
    return true;
}

// The marker a press would take is the one that lights, and a note under the
// pointer in the grid lights its own marker, so a marker stacked with others
// can be found by pointing at its note - Live's locator.
void StepGrid::updateHover(juce::Point<float> point)
{
    juce::ValueTree next;
    if (isOverLaneBlock(point))
    {
        if (const auto index = velocityMarkerAt(point); index >= 0)
            next = laneNotes[static_cast<size_t>(index)].state;
    }
    else if (velocityLaneHeight() > 0.0f)
    {
        if (const auto index = hit(point); index >= 0)
            next = visibleNotes[static_cast<size_t>(index)].state;
    }
    setHoveredNote(next);
}

void StepGrid::setHoveredNote(const juce::ValueTree& state)
{
    if (state == hoveredNoteState)
        return;
    if (const auto* before = hoveredNoteState.isValid() ? laneNoteFor(hoveredNoteState) : nullptr)
        repaint(markerBounds(*before));
    hoveredNoteState = state;
    if (const auto* after = state.isValid() ? laneNoteFor(state) : nullptr)
        repaint(markerBounds(*after));
    // The gutter reads the hovered note's value out.
    const auto lane = velocityLaneBounds();
    if (!lane.isEmpty())
        repaint(lane.withWidth(labelWidth).getSmallestIntegerContainer());
}

void StepGrid::mouseExit(const juce::MouseEvent&)
{
    if (gesture == Gesture::none)
        setHoveredNote({});
    if (dividerHot)
    {
        dividerHot = false;
        repaint(laneDividerBounds().getSmallestIntegerContainer());
    }
}

bool StepGrid::velocityLaneMouseDown(const juce::MouseEvent& event)
{
    const auto point = event.position;
    if (!isOverLaneBlock(point))
        return false;
    grabKeyboardFocus();
    // The right button erases in the grid. Here a marker can stand for a note
    // scrolled out of sight, and an eraser would delete what nobody can see.
    if (!event.mods.isLeftButtonDown())
        return true;
    if (isOverLaneDivider(point))
    {
        if (event.getNumberOfClicks() >= 2)
        {
            velocityLaneRequested = velocitylane::defaultHeight;
            velocityLaneShown = true;
            updateVelocityButton();
            resized();
            return true;
        }
        gesture = Gesture::laneResize;
        laneResizeStartY = point.y;
        // The laid-out height rather than the one asked for, so nothing jumps
        // when a pane too short for the request is dragged.
        laneResizeStartHeight = velocityLaneHeight();
        laneResizeStartRequest = velocityLaneRequested;
        repaint(laneDividerBounds().getSmallestIntegerContainer());
        return true;
    }
    if (point.x < labelWidth || !session.hasPatternClip())
        return true;

    // A pencil sets what it crosses and selects nothing. With notes selected
    // it paints only them, which is how one lane of a chord gets a ramp.
    if (drawMode)
    {
        gesture = Gesture::velocityDraw;
        const auto selection = selectedStates();
        velocityDrawTargets.assign(laneNotes.size(), {});
        for (size_t index = 0; index < laneNotes.size(); ++index)
            if (selection.empty()
                || std::find(selection.begin(), selection.end(), laneNotes[index].state) != selection.end())
                velocityDrawTargets[index] = laneNotes[index].state;
        strokeFresh = true;
        strokeLastStep = stepAtX(point.x);
        strokeLastValue = velocityValueAtY(point.y);
        noteMoved = false;
        dragPosition = point;
        session.beginNoteVelocityGesture("Draw velocities");
        paintVelocityStrokeTo(point, event.mods.isAltDown());
        startTimerHz(60);
        return true;
    }

    const auto index = velocityMarkerAt(point);
    if (index < 0)
    {
        beginVelocityMarquee(event);
        return true;
    }
    const auto state = laneNotes[static_cast<size_t>(index)].state;
    // Gathering only: the marker's note joins the selection or leaves it, and
    // nothing is carried, as with a gathering press on a note.
    if (isMultiSelectModifier(event.mods))
    {
        if (!(isExtendSelectionModifier(event.mods) && isSelected(state)))
            toggleSelectionOf(state);
        return true;
    }
    // The grid's own rule: a press inside a group keeps the group for the
    // drag, and collapses onto this one note if it turns out to be a click.
    clickedNoteState = state;
    if (!isSelected(state))
        setSelectedStates({state});
    else if (selectedNoteStates.size() > 1)
        collapseSelectionOnRelease = true;
    velocityOrigins.clear();
    for (const auto& selected : selectedStates())
        if (const auto* note = laneNoteFor(selected))
            velocityOrigins.push_back({selected, note->velocity});
    gesture = Gesture::velocityDrag;
    velocityDragArmed = false;
    velocityDragDelta = 0.0;
    noteMoved = false;
    setHoveredNote(state);
    session.beginNoteVelocityGesture("Change velocity");
    return true;
}

void StepGrid::velocityLaneMouseDrag(const juce::MouseEvent& event)
{
    switch (gesture)
    {
        case Gesture::velocityDrag:
        {
            // A press that shakes by a pixel is a click and changes nothing.
            if (!velocityDragArmed)
            {
                if (!dragTravelled)
                    return;
                velocityDragArmed = true;
                velocityDragLastY = event.position.y;
                return;
            }
            const auto plot = velocityPlotBounds();
            if (plot.getHeight() <= 0.0f)
                return;
            // Accumulated rather than read off the pointer's height, so Shift
            // can slow it down in the middle of a drag without a jump.
            const auto travelled = static_cast<double>(velocityDragLastY - event.position.y) * 126.0
                                 / plot.getHeight() * (event.mods.isShiftDown() ? 0.25 : 1.0);
            velocityDragLastY = event.position.y;
            std::vector<int> origins;
            origins.reserve(velocityOrigins.size());
            for (const auto& origin : velocityOrigins) origins.push_back(origin.velocity);
            velocityDragDelta = velocitylane::clampGroupDelta(origins, velocityDragDelta + travelled);
            std::vector<Session::NoteVelocity> targets;
            for (const auto& origin : velocityOrigins)
                if (const auto* note = laneNoteFor(origin.state))
                    if (const auto value = velocitylane::draggedVelocity(origin.velocity, velocityDragDelta);
                        value != note->velocity)
                        targets.push_back({origin.state, value});
            if (applyNoteVelocities(targets))
                noteMoved = true;
            return;
        }
        case Gesture::velocityDraw:
            paintVelocityStrokeTo(event.position, event.mods.isAltDown());
            return;
        case Gesture::velocitySelect:
            if (dragTravelled)
            {
                updateVelocityMarquee();
                repaint();
            }
            return;
        case Gesture::laneResize:
        {
            const auto raw = laneResizeStartHeight + (laneResizeStartY - event.position.y);
            const auto room = timeAreaHeight() - velocitylane::dividerHeight - Session::pitches * octaveLabelRowHeight;
            // Dragged most of the way down, the lane closes, live; dragged back
            // up in the same drag, it opens again.
            const auto show = raw >= velocitylane::minimumHeight * 0.5f;
            const auto request = std::clamp(raw, velocitylane::minimumHeight,
                                            std::max(velocitylane::minimumHeight,
                                                     std::min(velocitylane::maximumHeight, room)));
            if (show != velocityLaneShown || (show && request != velocityLaneRequested))
            {
                velocityLaneShown = show;
                if (show) velocityLaneRequested = request;
                updateVelocityButton();
                resized();
            }
            return;
        }
        default:
            return;
    }
}

void StepGrid::finishVelocityLaneGesture()
{
    // A click is a press that never travelled. A drag that changed nothing,
    // a group already on the rail it was pushed at, is still a drag.
    if (gesture == Gesture::velocityDrag && collapseSelectionOnRelease && !velocityDragArmed && clickedNoteState.isValid())
        setSelectedStates({clickedNoteState});
    if (holdsVelocityGesture(gesture))
        session.endNoteVelocityGesture();
    if (gesture == Gesture::laneResize && !velocityLaneShown)
    {
        // Released shut: the toggle brings back the size it had at the press.
        velocityLaneRequested = laneResizeStartRequest;
        updateVelocityButton();
    }
    if (gesture == Gesture::velocitySelect)
        repaint();
    velocityOrigins.clear();
    velocityDrawTargets.clear();
    velocityDragArmed = false;
    velocityDragDelta = 0.0;
    // Drops the drag's readout from the gutter, and the divider's highlight.
    repaintVelocityLane();
}

// The marquee's anchor is kept in steps and velocity, as the grid's is kept
// in steps and pitch, so a drag at the edge that scrolls the view does not
// slide the box off the markers it started on.
void StepGrid::beginVelocityMarquee(const juce::MouseEvent& event)
{
    gesture = Gesture::velocitySelect;
    velocityAnchorStep = stepAtX(event.position.x);
    velocityAnchorValue = velocityValueAtY(event.position.y);
    dragPosition = event.position;
    selectionBox = {};
    selectionBase = isMultiSelectModifier(event.mods) ? selectedStates() : std::vector<juce::ValueTree>{};
    if (selectionBase.empty())
    {
        selectedNotes.reset();
        selectedNoteStates.clear();
        setStepInsertPoint(std::floor(velocityAnchorStep));
    }
    startTimerHz(60);
    repaint();
}

// A marker is caught by its dot: the note's start inside the span and its
// velocity inside the band. Notes out of the pitch window are caught too.
void StepGrid::updateVelocityMarquee()
{
    const auto pointerStep = stepAtX(dragPosition.x);
    const auto pointerValue = velocityValueAtY(dragPosition.y);
    const auto firstStep = std::min(velocityAnchorStep, pointerStep);
    const auto lastStep = std::max(velocityAnchorStep, pointerStep);
    const auto low = std::min(velocityAnchorValue, pointerValue);
    const auto high = std::max(velocityAnchorValue, pointerValue);
    const auto lane = velocityLaneBounds();
    selectionBox = juce::Rectangle<float>({xForStep(velocityAnchorStep), yForVelocity(velocityAnchorValue)}, dragPosition)
                       .getIntersection({labelWidth, lane.getY(), gridWidth(), lane.getHeight()});
    auto caught = selectionBase;
    for (const auto& note : laneNotes)
        if (note.start >= firstStep && note.start <= lastStep
            && static_cast<float>(note.velocity) >= low && static_cast<float>(note.velocity) <= high
            && std::find(caught.begin(), caught.end(), note.state) == caught.end())
            caught.push_back(note.state);
    setSelectedStates(std::move(caught));
    setStepSelection(std::floor(firstStep), std::ceil(lastStep));
}

// Plain: every target starting in a step column the stroke crossed since the
// last call takes the stroke's value there - Live's notes within the grid
// division under the pointer. Alt: one note at a time, each taking the value
// at its own start, for a crescendo. Only columns in view are painted.
void StepGrid::paintVelocityStrokeTo(juce::Point<float> point, bool perNote)
{
    if (gesture != Gesture::velocityDraw)
        return;
    const auto span = visibleStepSpan();
    // Held to the view: a pointer parked past the edge paints the columns
    // that scroll in under it, as if the pencil rode the edge.
    const auto toStep = std::clamp(stepAtX(point.x), stepScroll, stepScroll + span - 1.0e-6);
    const auto toValue = velocityValueAtY(point.y);
    // The gesture clock asks again sixty times a second; a pencil that has
    // not moved and a view that has not scrolled have nothing new to paint.
    if (!strokeFresh && toStep == strokeLastStep && toValue == strokeLastValue)
        return;
    strokeFresh = false;
    // The targets are kept beside the lane's notes, index for index, so a
    // stroke over a long clip looks nothing up. Something that adds or takes
    // away notes mid-stroke - a take, the Push - breaks the alignment, and
    // the targets are found again by state.
    auto aligned = velocityDrawTargets.size() == laneNotes.size();
    for (size_t index = 0; aligned && index < laneNotes.size(); ++index)
        aligned = !velocityDrawTargets[index].isValid() || velocityDrawTargets[index] == laneNotes[index].state;
    if (!aligned)
    {
        std::vector<juce::ValueTree> realigned(laneNotes.size());
        for (size_t index = 0; index < laneNotes.size(); ++index)
            if (std::find(velocityDrawTargets.begin(), velocityDrawTargets.end(), laneNotes[index].state)
                != velocityDrawTargets.end())
                realigned[index] = laneNotes[index].state;
        velocityDrawTargets = std::move(realigned);
    }
    const auto steps = session.editorStepCount();
    const auto firstVisible = std::max(0, static_cast<int>(std::floor(stepScroll)));
    const auto lastVisible = std::min(steps - 1, static_cast<int>(std::ceil(stepScroll + span)) - 1);
    const auto columns = velocitylane::strokeColumns(strokeLastStep, toStep, firstVisible, lastVisible);
    const auto reach = 4.0 / static_cast<double>(cellWidth());
    const auto low = std::min(strokeLastStep, toStep) - reach;
    const auto high = std::max(strokeLastStep, toStep) + reach;
    std::vector<Session::NoteVelocity> targets;
    for (size_t index = 0; index < laneNotes.size(); ++index)
    {
        if (!velocityDrawTargets[index].isValid())
            continue;
        const auto* note = &laneNotes[index];
        const auto& state = note->state;
        auto value = -1;
        if (perNote)
        {
            if (note->start >= low && note->start <= high && note->start >= stepScroll && note->start < stepScroll + span)
                value = velocitylane::clampVelocity(
                    velocitylane::strokeValueAt(strokeLastStep, strokeLastValue, toStep, toValue, note->start));
        }
        else if (const auto column = static_cast<int>(std::floor(note->start));
                 !columns.empty() && column >= columns.first && column <= columns.last)
            value = velocitylane::clampVelocity(
                velocitylane::columnValue(strokeLastStep, strokeLastValue, toStep, toValue, column));
        if (value > 0 && value != note->velocity)
            targets.push_back({state, value});
    }
    if (applyNoteVelocities(targets))
        noteMoved = true;
    strokeLastStep = toStep;
    strokeLastValue = toValue;
    // The gutter reads out the value under the pencil.
    if (const auto lane = velocityLaneBounds(); !lane.isEmpty())
        repaint(lane.withWidth(labelWidth).getSmallestIntegerContainer());
}

}
