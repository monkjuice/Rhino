#include "StepGridInternal.h"
#include "Playhead.h"
#include <algorithm>
#include <cmath>
#include <limits>

// Hit testing, pointer gestures, note drag/resize and the wheel.

namespace theta
{

juce::Result StepGrid::loopEditedClip()
{
    const auto range = session.pattern().getPosition().time;
    return session.setLoopRange(range.getStart().inSeconds(), range.getEnd().inSeconds());
}

double StepGrid::loopStepAt(float x, bool free) const
{
    auto step = std::clamp(stepScroll + (x - labelWidth) / cellWidth(),
                           0.0, static_cast<double>(session.editorStepCount()));
    return free ? std::round(step * 16.0) / 16.0 : std::round(step);
}

double StepGrid::timelineTimeForLoopStep(double step) const
{
    const auto range = session.pattern().getPosition().time;
    const auto start = range.getStart().inSeconds();
    const auto end = range.getEnd().inSeconds();
    const auto fraction = std::clamp(step / static_cast<double>(session.editorStepCount()), 0.0, 1.0);
    return juce::jmap(fraction, start, end);
}

float StepGrid::loopXForTimelineTime(double seconds) const
{
    const auto range = session.pattern().getPosition().time;
    const auto start = range.getStart().inSeconds();
    const auto duration = range.getEnd().inSeconds() - start;
    if (duration <= 0.0)
        return -1.0f;
    const auto step = (seconds - start) / duration * session.editorStepCount();
    return labelWidth + static_cast<float>(step - stepScroll) * cellWidth();
}

int StepGrid::cellHit(juce::Point<float> point) const
{
    if (point.x < labelWidth || point.y < headerHeight || point.x >= gridRight() || point.y >= headerHeight + rowAreaHeight())
        return -1;
    const auto steps = session.editorStepCount();
    const auto step = static_cast<int>(stepScroll + (point.x - labelWidth) / cellWidth());
    if (step < 0 || step >= steps)
        return -1;
    const auto row = static_cast<int>((point.y - headerHeight) / rowAreaHeight() * Session::pitches);
    return row * Session::steps + step;
}

int StepGrid::hit(juce::Point<float> point) const
{
    if (cellHit(point) < 0)
        return -1;
    // Prefer the shortest containing note. This makes tightly packed
    // retriggers easy to pick even next to a long sustained note.
    auto best = -1;
    auto bestWidth = std::numeric_limits<float>::max();
    for (int candidate = 0; candidate < static_cast<int>(visibleNotes.size()); ++candidate)
        if (const auto bounds = boundsFor(visibleNotes[static_cast<size_t>(candidate)]); bounds.contains(point)
            && bounds.getWidth() < bestWidth)
        {
            best = candidate;
            bestWidth = bounds.getWidth();
        }
    return best;
}

int StepGrid::resizeHit(juce::Point<float> point) const
{
    if (point.y < headerHeight || point.y >= headerHeight + rowAreaHeight())
        return -1;
    for (int index = 0; index < static_cast<int>(visibleNotes.size()); ++index)
    {
        auto bounds = boundsFor(visibleNotes[static_cast<size_t>(index)]);
        if (bounds.getRight() < labelWidth || bounds.getX() > gridRight())
            continue;
        const auto handleWidth = std::min(5.0f, std::max(3.0f, bounds.getWidth() * 0.25f));
        const auto rightHandle = bounds.withX(bounds.getRight() - handleWidth).withWidth(handleWidth);
        const auto leftHandle = bounds.withWidth(handleWidth);
        if (rightHandle.contains(point) || leftHandle.contains(point))
            return index;
    }
    return -1;
}

void StepGrid::updatePointer(juce::Point<float> position, const juce::ModifierKeys& modifiers)
{
    if (position.y >= 0.0f && position.y < headerHeight && position.x >= labelWidth && position.x < gridRight())
    {
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
        return;
    }
    const auto note = hit(position);
    if (isShortcutDown(modifiers) && note >= 0)
        setMouseCursor(juce::MouseCursor::NormalCursor);
    else if (!modifiers.isRightButtonDown() && resizeHit(position) >= 0)
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    else if (cellHit(position) >= 0)
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
    else
        setMouseCursor(juce::MouseCursor::NormalCursor);
}

void StepGrid::mouseMove(const juce::MouseEvent& event)
{
    updatePointer(event.position, event.mods);
}

void StepGrid::mouseDown(const juce::MouseEvent& event)
{
    finishSubdivision();
    finishVelocityAdjustment();
    if (!event.mods.isRightButtonDown() && juce::KeyPress::isKeyCurrentlyDown('S')
        && event.position.x >= labelWidth && event.position.y >= headerHeight)
    {
        grabKeyboardFocus();
        gesture = Gesture::select;
        selectionAnchor = event.position;
        selectionBox = {};
        selectedNotes.reset();
        selectedNoteStates.clear();
        repaint();
        return;
    }
    if (event.position.y >= 0.0f && event.position.y < headerHeight
        && event.position.x >= labelWidth && event.position.x < gridRight())
    {
        grabKeyboardFocus();
        if (event.mods.isRightButtonDown())
        {
            session.clearManualLoopRange();
            repaint();
            return;
        }
        if (!event.mods.isLeftButtonDown())
            return;
        const auto localStep = std::clamp(stepScroll + (event.position.x - labelWidth) / cellWidth(),
                                          0.0, static_cast<double>(session.editorStepCount()));
        const auto localBeat = localStep * 4.0 / static_cast<double>(session.editorStepResolution());
        const auto& position = session.pattern().getPosition();
        const auto clipStartBeat = session.edit->tempoSequence.toBeats(position.time.getStart()).inBeats();
        const auto offsetBeat = position.offset.inSeconds() * session.tempo() / 60.0;
        const auto requestedTime = session.edit->tempoSequence.toTime(
            tracktion::core::BeatPosition::fromBeats(clipStartBeat - offsetBeat + localBeat)).inSeconds();
        const auto clippedTime = std::clamp(requestedTime, position.time.getStart().inSeconds(), position.time.getEnd().inSeconds());
        session.edit->getTransport().setPosition(tracktion::core::TimePosition::fromSeconds(clippedTime));
        updatePlayhead();
        loopDragActive = true;
        loopAnchorStep = loopStepAt(event.position.x, event.mods.isAltDown());
        loopPreviewStartStep = loopPreviewEndStep = loopAnchorStep;
        return;
    }
    if (!event.mods.isRightButtonDown() && !isShortcutDown(event.mods))
    {
        const auto resizeIndex = resizeHit(event.position);
        if (resizeIndex >= 0)
        {
            const auto& note = visibleNotes[static_cast<size_t>(resizeIndex)];
            grabKeyboardFocus();
            gesture = Gesture::resize;
            resizingNoteState = note.state;
            const auto bounds = boundsFor(note);
            resizingFromLeft = event.position.x < bounds.getCentreX();
            resizingStartStep = note.start;
            noteMoved = false;
            session.beginNoteGesture("Resize note");
            return;
        }
    }
    const auto noteIndex = hit(event.position);
    const auto cellIndex = cellHit(event.position);
    if (cellIndex < 0) return;
    grabKeyboardFocus();
    lastHit = cellIndex;
    pasteAnchorIndex = cellIndex;
    if (isShortcutDown(event.mods) && noteIndex >= 0)
    {
        toggleSelection(noteIndex);
        return;
    }
    if (!event.mods.isRightButtonDown() && noteIndex >= 0)
    {
        const auto& clicked = visibleNotes[static_cast<size_t>(noteIndex)];
        gesture = Gesture::move;
        movingNoteState = clicked.state;
        movingNotes.clear();
        movingGroup = isSelected(clicked.state);
        if (movingGroup)
            for (const auto& state : selectedStates())
                if (const auto* note = noteForState(state))
                    movingNotes.push_back({state, note->start, note->pitch});
        if (movingNotes.empty())
            movingNotes.push_back({clicked.state, clicked.start, clicked.pitch});
        // Where the group began, and where the pointer grabbed it. Every drag
        // position is measured against these, never against the previous one.
        moveGrabPitch = pitchForIndex(cellHit(event.position));
        dragStartStep = stepScroll + (event.position.x - labelWidth) / cellWidth();
        dragPosition = event.position;
        verticalAutoScroll = 0.0f;
        noteMoved = false;
        session.beginNoteGesture("Move note");
        startTimerHz(60);
        return;
    }

    gesture = Gesture::draw;
    adding = !event.mods.isRightButtonDown() && noteIndex < 0;
    visited.reset();
    session.beginNoteGesture(adding ? "Draw notes" : "Erase notes");
    if (!adding && noteIndex >= 0)
        session.removeNotes({visibleNotes[static_cast<size_t>(noteIndex)].state});
    else
        apply(cellIndex);
}

void StepGrid::apply(int index)
{
    if (index < 0 || visited.test(static_cast<size_t>(index))) return;
    visited.set(static_cast<size_t>(index));
    session.setNote(index % Session::steps,
                    pitchForIndex(index), adding);
}

int StepGrid::pitchForIndex(int index) const
{
    return lowestVisiblePitch + Session::pitches - 1 - index / Session::steps;
}

int StepGrid::indexForCell(int step, int pitch) const
{
    const auto row = lowestVisiblePitch + Session::pitches - 1 - pitch;
    if (step < 0 || step >= session.editorStepCount() || row < 0 || row >= Session::pitches)
        return -1;
    return row * Session::steps + step;
}

juce::Result StepGrid::moveCurrentNotesBy(double stepDelta, int pitchDelta)
{
    if (movingNotes.empty() || (std::abs(stepDelta) < 0.0001 && pitchDelta == 0))
        return juce::Result::ok();
    std::vector<juce::ValueTree> sources;
    sources.reserve(movingNotes.size());
    for (const auto& note : movingNotes)
        sources.push_back(note.state);
    const auto result = session.moveNotes(sources, stepDelta, pitchDelta);
    if (result.wasOk())
    {
        if (movingGroup) setSelectedStates(std::move(sources));
        else clearSelection();
        noteMoved = true;
    }
    return result;
}

void StepGrid::moveDraggedNotesAt(juce::Point<float> position)
{
    const auto index = cellHit(position);
    if (index < 0 || dragStartStep < 0.0 || moveGrabPitch < 0 || movingNotes.empty())
        return;
    const auto& anchor = movingNotes.front();
    const auto* placed = noteForState(anchor.state);
    if (placed == nullptr)
        return;
    // Notes follow the pointer freely along the timeline, at the same 1/16 of a
    // step that a free resize uses: a note is only ever between two columns,
    // never between two tones, so only the row snaps.
    const auto pointerStep = stepScroll + (position.x - labelWidth) / cellWidth();
    const auto travelled = std::round((pointerStep - dragStartStep) * 16.0) / 16.0;
    // Asking for the distance from where the group actually sits to where the
    // pointer wants it keeps the two together even when the session clamps a
    // move short against a neighbouring note or the end of the clip.
    moveCurrentNotesBy(anchor.step + travelled - placed->start,
                       anchor.pitch + pitchForIndex(index) - moveGrabPitch - placed->pitch);
}

void StepGrid::scrollDraggedNotes()
{
    if (gesture != Gesture::move || dragPosition.x < 0.0f)
        return;

    constexpr auto edge = 28.0f;
    const auto maximumStart = std::max(0.0, static_cast<double>(session.editorStepCount()) - visibleStepSpan());
    const auto horizontalDirection = dragPosition.x < labelWidth + edge ? -1.0
        : dragPosition.x > gridRight() - edge ? 1.0 : 0.0;
    const auto nextStepScroll = std::clamp(stepScroll + horizontalDirection * 0.28, 0.0, maximumStart);
    auto changed = nextStepScroll != stepScroll;
    stepScroll = nextStepScroll;
    if (!session.isPatternDrums())
    {
        const auto verticalDirection = dragPosition.y < headerHeight + edge ? 1.0f
            : dragPosition.y > headerHeight + rowAreaHeight() - edge ? -1.0f : 0.0f;
        verticalAutoScroll += verticalDirection * 0.18f;
        const auto pitchSteps = static_cast<int>(verticalAutoScroll);
        if (pitchSteps != 0)
        {
            const auto nextLowest = juce::jlimit(0, 127 - Session::pitches + 1, lowestVisiblePitch + pitchSteps);
            changed = changed || nextLowest != lowestVisiblePitch;
            lowestVisiblePitch = nextLowest;
            verticalAutoScroll -= static_cast<float>(pitchSteps);
            manualPitchScroll = true;
            rebuildVisibleNotes();
        }
    }
    if (changed)
    {
        horizontalScroll.setCurrentRange(stepScroll, visibleStepSpan(), juce::dontSendNotification);
        updatePlayhead();
        repaint();
    }
}

juce::Result StepGrid::resizeCurrentNoteTo(int index)
{
    if (!resizingNoteState.isValid())
        return juce::Result::ok();
    if (index < 0)
        return juce::Result::ok();
    const auto targetStep = index % Session::steps;
    const auto length = std::max(0.0625, targetStep - resizingStartStep + 1.0);
    const auto result = session.resizeNote(resizingNoteState, length);
    if (result.wasOk())
        noteMoved = true;
    return result;
}

juce::Result StepGrid::resizeCurrentNoteTo(juce::Point<float> position, bool freeLength)
{
    if (!resizingNoteState.isValid())
        return juce::Result::ok();
    if (resizingFromLeft)
    {
        auto newStart = static_cast<double>(stepScroll + (position.x - labelWidth) / cellWidth());
        if (!freeLength && newStart < std::floor(resizingStartStep))
            newStart = std::round(newStart);
        else
            newStart = std::round(newStart * 16.0) / 16.0;
        const auto result = session.resizeNoteFromLeft(resizingNoteState, newStart);
        if (result.wasOk())
        {
            resizingStartStep = newStart;
            noteMoved = true;
        }
        return result;
    }
    auto length = stepScroll + (position.x - labelWidth) / cellWidth() - resizingStartStep;
    length = std::max(0.0625, length);
    // The first grid space may be freely adjusted. Once past it, resize snaps
    // to grid boundaries unless Alt/Option is held.
    if (!freeLength && length > 1.0)
        length = std::round(length);
    else
        length = std::round(length * 16.0) / 16.0;
    const auto result = session.resizeNote(resizingNoteState, length);
    if (result.wasOk()) noteMoved = true;
    return result;
}

void StepGrid::mouseDrag(const juce::MouseEvent& event)
{
    if (loopDragActive)
    {
        const auto edge = loopStepAt(event.position.x, event.mods.isAltDown());
        loopPreviewStartStep = std::min(loopAnchorStep, edge);
        loopPreviewEndStep = std::max(loopAnchorStep, edge);
        repaint();
        return;
    }
    if (gesture == Gesture::none) return;
    dragPosition = event.position;
    if (gesture == Gesture::select)
    {
        selectionBox = juce::Rectangle<float>(selectionAnchor, event.position).getIntersection(
            {labelWidth, headerHeight, gridWidth(), rowAreaHeight()});
        updateMarqueeSelection();
        repaint();
        return;
    }
    const auto index = cellHit(event.position);
    if (gesture == Gesture::move)
    {
        scrollDraggedNotes();
        // Notes now travel in fractions of a step, so a click that shakes by a
        // pixel must not count as a move and cost the click its erase.
        if (noteMoved || event.getDistanceFromDragStart() >= 3)
            moveDraggedNotesAt(event.position);
        return;
    }
    if (gesture == Gesture::resize)
    {
        resizeCurrentNoteTo(event.position, event.mods.isAltDown());
        return;
    }

    if (!adding)
    {
        if (const auto noteIndex = hit(event.position); noteIndex >= 0)
            session.removeNotes({visibleNotes[static_cast<size_t>(noteIndex)].state});
    }

    // Fill skipped cells for fast horizontal strokes, without toggling a cell
    // twice when the pointer retraces its path.
    if (index >= 0 && lastHit >= 0 && index / Session::steps == lastHit / Session::steps)
        for (int i = std::min(index, lastHit); i <= std::max(index, lastHit); ++i) apply(i);
    else apply(index);
    lastHit = index;
}

void StepGrid::mouseUp(const juce::MouseEvent& event)
{
    if (loopDragActive)
    {
        mouseDrag(event);
        loopDragActive = false;
        if (event.getDistanceFromDragStart() >= 3 && loopPreviewEndStep > loopPreviewStartStep)
            session.setLoopRange(timelineTimeForLoopStep(loopPreviewStartStep),
                                 timelineTimeForLoopStep(loopPreviewEndStep));
        repaint();
        return;
    }
    if (gesture == Gesture::move && !movingGroup && !noteMoved && movingNoteState.isValid())
        session.removeNotes({movingNoteState});
    if (gesture != Gesture::none) session.endNoteGesture();
    gesture = Gesture::none;
    selectionAnchor = {-1.0f, -1.0f};
    selectionBox = {};
    lastHit = -1;
    movingNoteState = {};
    movingNotes.clear();
    movingGroup = false;
    moveGrabPitch = -1;
    dragStartStep = -1.0;
    dragPosition = {-1.0f, -1.0f};
    stopTimer();
    resizingNoteState = {};
    resizingFromLeft = false;
    noteMoved = false;
}

void StepGrid::updateMarqueeSelection()
{
    selectedNotes.reset();
    selectedNoteStates.clear();
    if (selectionBox.isEmpty()) return;
    for (const auto& note : visibleNotes)
        if (boundsFor(note).intersects(selectionBox)) selectedNoteStates.push_back(note.state);
    setSelectedStates(selectedNoteStates);
}

void StepGrid::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (gesture != Gesture::none)
        return;
    const auto wheelDelta = std::abs(wheel.deltaY) >= std::abs(wheel.deltaX) ? wheel.deltaY : -wheel.deltaX;
    if (std::abs(wheelDelta) < 0.0001f)
        return;
    if (velocityAdjustActive || juce::KeyPress::isKeyCurrentlyDown('V'))
    {
        if (!velocityAdjustActive && !beginVelocityAdjustment()) return;
        adjustVelocity(wheelDelta > 0.0f ? 1 : -1);
        return;
    }
    if (isShortcutDown(event.mods))
    {
        if (!subdivisionActive)
        {
            beginSubdivision();
            return;
        }
        adjustSubdivision(wheelDelta > 0.0f ? 1 : -1);
        return;
    }
    finishSubdivision();
    if (event.mods.isShiftDown())
    {
        // Keep the step below the pointer stable while the visible range changes.
        zoomAt(std::exp(wheelDelta * 2.0f), event.position.x);
        return;
    }
    if (session.isPatternDrums())
        return;
    const auto semitones = std::max(1, juce::roundToInt(std::abs(wheelDelta) * 8.0f));
    lowestVisiblePitch = juce::jlimit(0, 127 - Session::pitches + 1,
                                      lowestVisiblePitch + (wheelDelta > 0.0f ? semitones : -semitones));
    manualPitchScroll = true;
    rebuildVisibleNotes();
}

}
