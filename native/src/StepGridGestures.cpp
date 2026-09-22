#include "StepGridInternal.h"
#include "Playhead.h"
#include <algorithm>
#include <cmath>
#include <limits>

// Hit testing, pointer gestures, note drag/resize and the wheel.
//
// The pointer obeys the rule in SelectionInput.h, which is the arrangement's
// rule as well: a press on a note takes that note and drops the rest, a press
// on empty space sweeps out a marquee, Ctrl and Shift gather, and a note is
// put down by double-clicking a cell. Painting a run of notes by dragging is
// what draw mode is for, and the right button erases in either mode.

namespace rhino
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

double StepGrid::stepAtX(float x) const
{
    return stepScroll + (x - labelWidth) / cellWidth();
}

float StepGrid::xForStep(double step) const
{
    return labelWidth + static_cast<float>(step - stepScroll) * cellWidth();
}

// Continuous pitch rather than a row index: whole numbers land on the line
// between two lanes, so a lane for pitch p covers [p, p + 1). That is what
// lets a marquee say which lanes it touches without rounding twice.
double StepGrid::pitchAtY(float y) const
{
    return lowestVisiblePitch + visiblePitchRows() - (y - headerHeight) / rowHeight();
}

float StepGrid::yForPitch(double pitch) const
{
    return headerHeight + static_cast<float>(lowestVisiblePitch + visiblePitchRows() - pitch) * rowHeight();
}

bool StepGrid::isOverKeyboard(juce::Point<float> point) const
{
    return point.x >= 0.0f && point.x < labelWidth
        && point.y >= headerHeight && point.y < headerHeight + rowAreaHeight();
}

int StepGrid::cellHit(juce::Point<float> point) const
{
    if (point.x < labelWidth || point.y < headerHeight || point.x >= gridRight() || point.y >= headerHeight + rowAreaHeight())
        return -1;
    const auto steps = session.editorStepCount();
    const auto step = static_cast<int>(stepScroll + (point.x - labelWidth) / cellWidth());
    if (step < 0 || step >= steps)
        return -1;
    const auto rows = visiblePitchRows();
    const auto row = std::clamp(static_cast<int>((point.y - headerHeight) / rowAreaHeight() * rows), 0, rows - 1);
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
    if (isOverKeyboard(position))
    {
        setMouseCursor(juce::MouseCursor::UpDownLeftRightResizeCursor);
        return;
    }
    const auto note = hit(position);
    if (cellHit(position) < 0)
        setMouseCursor(juce::MouseCursor::NormalCursor);
    else if (drawMode)
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
    else if (isMultiSelectModifier(modifiers) && note >= 0)
        setMouseCursor(juce::MouseCursor::NormalCursor);
    else if (!modifiers.isRightButtonDown() && resizeHit(position) >= 0)
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    else if (note >= 0)
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    else
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
}

void StepGrid::mouseMove(const juce::MouseEvent& event)
{
    updatePointer(event.position, event.mods);
}

void StepGrid::mouseDown(const juce::MouseEvent& event)
{
    finishSubdivision();
    finishVelocityAdjustment();
    collapseSelectionOnRelease = false;
    clickedNoteState = {};
    dragTravelled = false;
    if (!event.mods.isRightButtonDown() && isOverKeyboard(event.position))
    {
        grabKeyboardFocus();
        gesture = Gesture::keyboard;
        keyboardDragPosition = event.position;
        keyboardScrollRemainder = 0.0;
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
        // The ruler carries the insert point with the playhead, so a paste
        // after a seek lands where the transport was just put.
        setStepInsertPoint(loopStepAt(event.position.x, event.mods.isAltDown()));
        updatePlayhead();
        loopDragActive = true;
        loopAnchorStep = loopStepAt(event.position.x, event.mods.isAltDown());
        loopPreviewStartStep = loopPreviewEndStep = loopAnchorStep;
        return;
    }
    // A resize handle is the one part of a note that is not the note: it is
    // offered first, and only to the plain pointer. A pencil has no handles,
    // and a gathering press is about the selection rather than the shape.
    if (!drawMode && !event.mods.isRightButtonDown() && !isMultiSelectModifier(event.mods))
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
            dragPosition = event.position;
            noteMoved = false;
            session.beginNoteGesture("Resize note");
            startTimerHz(60);
            return;
        }
    }
    const auto noteIndex = hit(event.position);
    const auto cellIndex = cellHit(event.position);
    if (cellIndex < 0) return;
    grabKeyboardFocus();
    lastHit = cellIndex;
    // A press in the lanes leaves the insert point on the step it landed on,
    // which is where a paste goes when nothing is selected. Clicking a note
    // selects it, and the selection sets the region instead.
    if (noteIndex < 0)
        setStepInsertPoint(std::floor(stepAtX(event.position.x)));

    // The right button erases in either mode, which is the one gesture that
    // never had to be learned twice.
    if (event.mods.isRightButtonDown())
    {
        gesture = Gesture::draw;
        adding = false;
        visited.reset();
        dragPosition = event.position;
        session.beginNoteGesture("Erase notes");
        if (noteIndex >= 0)
            session.removeNotes({visibleNotes[static_cast<size_t>(noteIndex)].state});
        startTimerHz(60);
        return;
    }

    // Draw mode is a pencil: empty space takes a note, a note under the point
    // goes away, and dragging carries on doing whichever of those the press
    // started. Nothing here selects, because a pencil does not select.
    if (drawMode)
    {
        gesture = Gesture::draw;
        adding = noteIndex < 0;
        visited.reset();
        dragPosition = event.position;
        session.beginNoteGesture(adding ? "Draw notes" : "Erase notes");
        if (adding) apply(cellIndex);
        else session.removeNotes({visibleNotes[static_cast<size_t>(noteIndex)].state});
        startTimerHz(60);
        return;
    }

    if (noteIndex >= 0)
    {
        const auto& clicked = visibleNotes[static_cast<size_t>(noteIndex)];
        // Ctrl or Shift gathers: the note joins the selection or leaves it,
        // the rest is untouched, and the press is over. It must not also start
        // carrying the selection, or every attempt to add one note to a group
        // would nudge the whole group.
        if (isMultiSelectModifier(event.mods))
        {
            if (isExtendSelectionModifier(event.mods) && isSelected(clicked.state))
                return;
            toggleSelection(noteIndex);
            return;
        }
        // A plain press takes the note and drops everything else - unless the
        // note is already part of a group, in which case the group is kept so
        // the drag can carry all of it. A press on a group that never travels
        // was a click after all, and collapses onto this note on release.
        clickedNoteState = clicked.state;
        if (!isSelected(clicked.state))
            setSelectedStates({clicked.state});
        else if (selectedNoteStates.size() > 1)
            collapseSelectionOnRelease = true;
        gesture = Gesture::move;
        movingNoteState = clicked.state;
        movingNotes.clear();
        movingGroup = true;
        for (const auto& state : selectedStates())
            if (const auto* note = noteForState(state))
                movingNotes.push_back({state, note->start, note->pitch});
        if (movingNotes.empty())
            movingNotes.push_back({clicked.state, clicked.start, clicked.pitch});
        // Where the group began, and where the pointer grabbed it. Every drag
        // position is measured against these, never against the previous one.
        moveGrabPitch = pitchForIndex(cellHit(event.position));
        dragStartStep = stepAtX(event.position.x);
        dragPosition = event.position;
        verticalAutoScroll = 0.0f;
        noteMoved = false;
        session.beginNoteGesture("Move note");
        startTimerHz(60);
        return;
    }

    // Empty space. Double-clicking it puts a note there - the deliberate way
    // to add one, matching the arrangement, where double-clicking a lane adds
    // a clip. Anything else sweeps out a marquee.
    if (event.getNumberOfClicks() >= 2)
    {
        clearSelection();
        adding = true;
        visited.reset();
        session.beginNoteGesture("Draw note");
        apply(cellIndex);
        session.endNoteGesture();
        gesture = Gesture::none;
        return;
    }
    beginMarquee(event);
}

// The marquee. Its anchor is kept in steps and pitch rather than in pixels,
// because a drag that reaches the edge scrolls the view and a pixel anchor
// would travel with it.
void StepGrid::beginMarquee(const juce::MouseEvent& event)
{
    gesture = Gesture::select;
    selectionAnchorStep = stepAtX(event.position.x);
    selectionAnchorPitch = pitchAtY(event.position.y);
    dragPosition = event.position;
    selectionBox = {};
    verticalAutoScroll = 0.0f;
    // Ctrl or Shift adds to what was already there, exactly as it does on one
    // note. A plain drag starts from nothing, and says so now rather than
    // leaving the old selection flickering under the new box.
    selectionBase = isMultiSelectModifier(event.mods) ? selectedStates() : std::vector<juce::ValueTree>{};
    if (selectionBase.empty())
    {
        selectedNotes.reset();
        selectedNoteStates.clear();
        // A press that never travels is a click, and a click leaves the insert
        // point where it landed - so the region collapses onto this step
        // rather than disappearing. The drag replaces it the moment it moves.
        setStepInsertPoint(std::floor(selectionAnchorStep));
    }
    startTimerHz(60);
    repaint();
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
    return lowestVisiblePitch + visiblePitchRows() - 1 - index / Session::steps;
}

int StepGrid::indexForCell(int step, int pitch) const
{
    const auto row = lowestVisiblePitch + visiblePitchRows() - 1 - pitch;
    if (step < 0 || step >= session.editorStepCount() || row < 0 || row >= visiblePitchRows())
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

// Every drag scrolls, not only a note being carried: a marquee that reaches
// the right edge has to be able to sweep the rest of a clip that is wider than
// the panel, which is the whole point of dragging past the edge.
void StepGrid::autoScrollDrag()
{
    if (gesture == Gesture::none || gesture == Gesture::keyboard || dragPosition.x < 0.0f)
        return;

    const auto maximumStart = std::max(0.0, static_cast<double>(session.editorStepCount()) - visibleStepSpan());
    const auto horizontal = autoScrollPush(dragPosition.x, labelWidth, gridRight());
    const auto nextStepScroll = std::clamp(stepScroll + horizontal * 0.3, 0.0, maximumStart);
    auto changed = nextStepScroll != stepScroll;
    stepScroll = nextStepScroll;
    if (!session.isPatternDrums())
    {
        // Upwards on screen is a higher pitch, so the sign flips here: the
        // lanes move the opposite way to the view along the timeline.
        verticalAutoScroll -= autoScrollPush(dragPosition.y, headerHeight, headerHeight + rowAreaHeight()) * 0.2f;
        const auto pitchSteps = static_cast<int>(verticalAutoScroll);
        if (pitchSteps != 0)
        {
            const auto nextLowest = juce::jlimit(0, 127 - visiblePitchRows() + 1, lowestVisiblePitch + pitchSteps);
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
    dragTravelled = dragTravelled || event.getDistanceFromDragStart() >= 3;
    if (gesture == Gesture::keyboard)
    {
        // Ableton's piano strip: sideways resizes the lanes, up and down drags
        // the keyboard itself along under the pointer.
        const auto delta = event.position - keyboardDragPosition;
        keyboardDragPosition = event.position;
        if (std::abs(delta.x) > 0.0f)
            zoomPitchAt(std::exp(-delta.x * 0.02f), event.position.y);
        keyboardScrollRemainder += delta.y / rowHeight();
        const auto semitones = static_cast<int>(keyboardScrollRemainder);
        keyboardScrollRemainder -= semitones;
        scrollPitchBy(semitones);
        return;
    }
    // Scrolling is the gesture clock's business, not the pointer's: it has to
    // go on happening while the pointer is held still past the edge, so it
    // happens in one place rather than in two that would race.
    if (gesture == Gesture::select)
    {
        // Until the pointer has actually travelled the gesture is still the
        // click that started it, and a click leaves an insert point rather
        // than a one-cell span - the rule the arrangement's region follows.
        if (dragTravelled)
        {
            updateMarqueeSelection();
            repaint();
        }
        return;
    }
    const auto index = cellHit(event.position);
    if (gesture == Gesture::move)
    {
        // Notes travel in fractions of a step, so a press that shakes by a
        // pixel must not count as a move and undo a click's selection.
        if (noteMoved || dragTravelled)
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
    // A press inside a group that never travelled was a click, and a click
    // takes the one thing under it. Deciding it here rather than on the press
    // is what lets the same press also carry the whole group.
    if (gesture == Gesture::move && collapseSelectionOnRelease && !noteMoved && clickedNoteState.isValid())
        setSelectedStates({clickedNoteState});
    if (gesture != Gesture::none && gesture != Gesture::keyboard) session.endNoteGesture();
    gesture = Gesture::none;
    collapseSelectionOnRelease = false;
    clickedNoteState = {};
    dragTravelled = false;
    selectionBox = {};
    selectionBase.clear();
    lastHit = -1;
    movingNoteState = {};
    movingNotes.clear();
    movingGroup = false;
    moveGrabPitch = -1;
    keyboardDragPosition = {-1.0f, -1.0f};
    keyboardScrollRemainder = 0.0;
    dragStartStep = -1.0;
    dragPosition = {-1.0f, -1.0f};
    stopTimer();
    resizingNoteState = {};
    resizingFromLeft = false;
    noteMoved = false;
}

void StepGrid::updateMarqueeSelection()
{
    const auto pointerStep = stepAtX(dragPosition.x);
    const auto pointerPitch = pitchAtY(dragPosition.y);
    const auto firstStep = std::min(selectionAnchorStep, pointerStep);
    const auto lastStep = std::max(selectionAnchorStep, pointerStep);
    const auto lowPitch = std::min(selectionAnchorPitch, pointerPitch);
    const auto highPitch = std::max(selectionAnchorPitch, pointerPitch);
    // The box is only what is drawn; what it catches is decided in steps and
    // pitches. A drag at the edge scrolls the grid, so measuring against
    // pixels would quietly drop every note that had left the panel - which is
    // exactly the material a drag past the edge is reaching for.
    selectionBox = juce::Rectangle<float>({xForStep(selectionAnchorStep), yForPitch(selectionAnchorPitch)},
                                          dragPosition)
                       .getIntersection({labelWidth, headerHeight, gridWidth(), rowAreaHeight()});
    // Asked of the clip rather than of the rows on screen, for the same
    // reason: a note the pitch window has scrolled past is still inside the
    // span the pointer has swept.
    auto caught = selectionBase;
    for (const auto& note : session.editorNotes())
        if (note.startSteps < lastStep && note.startSteps + note.lengthSteps > firstStep
            && note.pitch < highPitch && note.pitch + 1.0 > lowPitch
            && std::find(caught.begin(), caught.end(), note.state) == caught.end())
            caught.push_back(note.state);
    setSelectedStates(std::move(caught));
    // The span says what the region is, not the notes it happened to catch: a
    // span dragged around two notes with a rest between them keeps the rest.
    setStepSelection(std::floor(firstStep), std::ceil(lastStep));
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
        // Keep whatever is below the pointer stable while the visible range
        // changes. Over the keys there is no timeline to zoom, only lanes.
        if (isOverKeyboard(event.position))
            zoomPitchAt(std::exp(wheelDelta * 2.0f), event.position.y);
        else
            zoomAt(std::exp(wheelDelta * 2.0f), event.position.x);
        return;
    }
    const auto semitones = std::max(1, juce::roundToInt(std::abs(wheelDelta) * 8.0f));
    scrollPitchBy(wheelDelta > 0.0f ? semitones : -semitones);
}

}
