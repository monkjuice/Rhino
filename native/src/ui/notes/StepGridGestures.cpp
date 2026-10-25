#include "StepGridInternal.h"
#include "ui/shared/Playhead.h"
#include <algorithm>
#include <cmath>
#include <limits>

// Hit testing, pointer gestures, note drag/resize and the wheel.
//
// The pointer obeys the rule in SelectionInput.h, which is the arrangement's
// rule as well: a press on a note takes that note and drops the rest, a press
// on empty space sweeps out a marquee, Ctrl and Shift gather, and a note is
// put down by double-clicking a cell. Painting a run of notes by dragging is
// what draw mode is for, and the right button opens a menu in either mode
// (StepGridMenu.cpp).

namespace rhino
{

juce::Result StepGrid::loopEditedClip()
{
    if (!session.hasPatternClip())
        return juce::Result::fail("Open a MIDI clip to loop it.");
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
    if (!session.hasPatternClip())
        return 0.0;
    const auto range = session.pattern().getPosition().time;
    const auto start = range.getStart().inSeconds();
    const auto end = range.getEnd().inSeconds();
    const auto fraction = std::clamp(step / static_cast<double>(session.editorStepCount()), 0.0, 1.0);
    return juce::jmap(fraction, start, end);
}

float StepGrid::loopXForTimelineTime(double seconds) const
{
    if (!session.hasPatternClip())
        return -1.0f;
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
    // Ctrl+E's chop is open: a drag up or down sets its parts.
    if (subdivisionActive)
    {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        return;
    }
    if (position.y >= 0.0f && position.y < headerHeight && position.x >= labelWidth && position.x < gridRight())
    {
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
        return;
    }
    if (updateLanePointer(position, modifiers))
        return;
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
    updateHover(event.position);
}

// Alt pressed or let go with the pointer still: the note in hand comes off the
// grid or goes back onto it at once, rather than at the next drag.
void StepGrid::modifierKeysChanged(const juce::ModifierKeys& modifiers)
{
    if (gesture == Gesture::move)
    {
        moveFreely = modifiers.isAltDown();
        if (noteMoved || dragTravelled)
            moveDraggedNotesAt(dragPosition);
        return;
    }
    if (gesture != Gesture::resize)
    {
        juce::Component::modifierKeysChanged(modifiers);
        return;
    }
    resizeFreely = modifiers.isAltDown();
    if (dragTravelled)
        resizeCurrentNoteTo(dragPosition, resizeFreely);
}

void StepGrid::mouseDown(const juce::MouseEvent& event)
{
    // A press while another is still held - a second finger, or a pen beside
    // the mouse - ends the first gesture rather than overwriting it, so an
    // undo bracket it opened cannot be left open.
    if (gesture != Gesture::none)
        mouseUp(event);
    // Ctrl still down after Ctrl+E: the left button drags the chop's parts up
    // and down, and the press itself moves and selects nothing. Any other
    // press ends the chop.
    if (subdivisionActive && event.mods.isLeftButtonDown() && isShortcutDown(event.mods))
    {
        gesture = Gesture::chop;
        chopMouseY = event.position.y;
        return;
    }
    finishSubdivision();
    finishVelocityAdjustment();
    collapseSelectionOnRelease = false;
    clickedNoteState = {};
    dragTravelled = false;
    // The middle button pans, and answers first, as it does in the
    // arrangement: it is about the view, so nothing under the pointer - a
    // note, the keys, the ruler or the velocity lane - gets a say.
    if (event.mods.isMiddleButtonDown())
    {
        grabKeyboardFocus();
        gesture = Gesture::pan;
        panAnchor = event.position;
        panStartStep = stepScroll;
        panStartPitch = lowestVisiblePitch;
        setHoveredNote({});
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        return;
    }
    if (!event.mods.isRightButtonDown() && isOverKeyboard(event.position))
    {
        grabKeyboardFocus();
        gesture = Gesture::keyboard;
        keyboardDragPosition = event.position;
        keyboardScrollRemainder = 0.0;
        // A key sounds while it is held, and a click on it - a press that
        // never travels - selects its notes when the button comes up.
        keyboardPressPitch = pitchNamedAt(event.position);
        session.startNoteAudition(keyboardPressPitch, 100);
        return;
    }
    if (event.position.y >= 0.0f && event.position.y < headerHeight
        && event.position.x >= labelWidth && event.position.x < gridRight())
    {
        grabKeyboardFocus();
        // The loop's options rather than its removal: a right-click asks.
        if (event.mods.isRightButtonDown())
        {
            presentContextMenu(rulerMenu());
            return;
        }
        if (!event.mods.isLeftButtonDown() || !session.hasPatternClip())
            return;
        const auto localStep = std::clamp(stepScroll + (event.position.x - labelWidth) / cellWidth(),
                                          0.0, static_cast<double>(session.editorStepCount()));
        const auto localBeat = localStep * 4.0 / static_cast<double>(session.editorStepResolution());
        const auto& position = session.pattern().getPosition();
        const auto clipStartBeat = session.secondsToBeats(position.time.getStart().inSeconds());
        const auto offsetBeat = position.offset.inSeconds() * session.tempo() / 60.0;
        const auto requestedTime = session.beatsToSeconds(clipStartBeat - offsetBeat + localBeat);
        const auto clippedTime = std::clamp(requestedTime, position.time.getStart().inSeconds(), position.time.getEnd().inSeconds());
        // Through the model, which also makes it the playback start: a seek
        // in the editor is a seek, and stop returns to it as it does to one
        // made on the ruler.
        session.locate(clippedTime);
        // The ruler carries the insert point with the playhead, so a paste
        // after a seek lands where the transport was just put.
        setStepInsertPoint(loopStepAt(event.position.x, event.mods.isAltDown()));
        updatePlayhead();
        loopDragActive = true;
        loopAnchorStep = loopStepAt(event.position.x, event.mods.isAltDown());
        loopPreviewStartStep = loopPreviewEndStep = loopAnchorStep;
        return;
    }
    // The right button offers what can be done to what it landed on - notes,
    // the grid, the lane - and edits nothing by itself. It used to erase, in
    // either mode, so every right-click meant as a question cost a note.
    if (event.mods.isRightButtonDown())
    {
        openContextMenu(event.position);
        return;
    }
    if (velocityLaneMouseDown(event))
        return;
    // A resize handle is the one part of a note that is not the note: it is
    // offered first, and only to the plain pointer. A pencil has no handles,
    // and a gathering press is about the selection rather than the shape.
    if (!drawMode && !isMultiSelectModifier(event.mods))
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
            resizeFreely = event.mods.isAltDown();
            hasResizeTarget = false;
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
        if (adding)
        {
            apply(cellIndex);
            session.startNoteAudition(pitchForIndex(cellIndex), 100);
        }
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
        moveFreely = event.mods.isAltDown();
        verticalAutoScroll = 0.0f;
        noteMoved = false;
        session.beginNoteGesture("Move note");
        session.startNoteAudition(clicked.pitch, clicked.velocity);
        startTimerHz(60);
        return;
    }

    // Empty space. Double-clicking it puts a note there - the deliberate way
    // to add one, matching the arrangement, where double-clicking a lane adds
    // a clip. With Alt it goes where the pointer is rather than on the step it
    // is in. Anything else sweeps out a marquee.
    if (event.getNumberOfClicks() >= 2)
    {
        clearSelection();
        adding = true;
        visited.reset();
        const auto pitch = pitchForIndex(cellIndex);
        session.beginNoteGesture("Draw note");
        if (event.mods.isAltDown())
            addNoteOffTheGrid(stepAtX(event.position.x), pitch);
        else
            apply(cellIndex);
        session.endNoteGesture();
        session.startNoteAudition(pitch, 100);
        gesture = Gesture::none;
        return;
    }
    beginMarquee(event);
}

// A sixteenth of a step is as fine as a note is placed by hand. It is a step
// long, or as long as the gap before the next note on its row allows, and
// refused, with the model's reason, inside a note.
void StepGrid::addNoteOffTheGrid(double step, int pitch)
{
    step = std::max(0.0, std::round(step * 16.0) / 16.0);
    auto length = 1.0;
    for (const auto& note : session.editorNotes())
        if (note.pitch == pitch && note.startSteps > step + 0.0001)
            length = std::min(length, note.startSteps - step);
    if (const auto result = session.addNote(step, pitch, length); result.failed())
        report(result.getErrorMessage());
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
    // A note carried below the rows - into the velocity lane, or past it - sits
    // on the lowest row while the view scrolls pitch down after it, rather
    // than freezing where the pointer left the rows.
    const auto rowsBottom = headerHeight + rowAreaHeight();
    const auto index = cellHit(position.y >= rowsBottom ? juce::Point<float>(position.x, rowsBottom - 0.5f) : position);
    if (index < 0 || dragStartStep < 0.0 || moveGrabPitch < 0 || movingNotes.empty())
        return;
    // The grabbed note is the anchor; the rest of the group keeps its distance.
    const auto anchorIt = std::find_if(movingNotes.begin(), movingNotes.end(),
                                       [this](const auto& note) { return note.state == movingNoteState; });
    const auto& anchor = anchorIt != movingNotes.end() ? *anchorIt : movingNotes.front();
    const auto* placed = noteForState(anchor.state);
    if (placed == nullptr)
        return;
    const auto pointerStep = stepScroll + (position.x - labelWidth) / cellWidth();
    const auto travelled = pointerStep - dragStartStep;
    // Snapped, the grabbed note lands on a step's line, but only once the
    // pointer has gone half a step: a drag meant to change the pitch does not
    // also pull a note that sits between two lines onto one of them. With Alt
    // the note follows the pointer at a sixteenth of a step, never between
    // two tones.
    const auto target = moveFreely ? anchor.step + std::round(travelled * 16.0) / 16.0
                      : std::abs(travelled) < 0.5 ? anchor.step
                                                  : std::round(anchor.step + travelled);
    // Asking for the distance from where the group actually sits to where the
    // pointer wants it keeps the two together even when the session clamps a
    // move short against a neighbouring note or the end of the clip.
    moveCurrentNotesBy(target - placed->start,
                       anchor.pitch + pitchForIndex(index) - moveGrabPitch - placed->pitch);
    // The preview follows the note to each new pitch.
    if (const auto* moved = noteForState(anchor.state); moved != nullptr && moved->pitch != session.auditionedPitch()
        && session.noteAuditionEnabled())
        session.startNoteAudition(moved->pitch, moved->velocity);
}

// Every drag scrolls, not only a note being carried: a marquee that reaches
// the right edge has to be able to sweep the rest of a clip that is wider than
// the panel, which is the whole point of dragging past the edge.
void StepGrid::autoScrollDrag()
{
    if (gesture == Gesture::none || gesture == Gesture::keyboard || gesture == Gesture::velocityDrag
        || gesture == Gesture::laneResize || dragPosition.x < 0.0f)
        return;

    const auto maximumStart = std::max(0.0, static_cast<double>(session.editorStepCount()) - visibleStepSpan());
    const auto horizontal = autoScrollPush(dragPosition.x, labelWidth, gridRight());
    const auto nextStepScroll = std::clamp(stepScroll + horizontal * 0.3, 0.0, maximumStart);
    auto changed = nextStepScroll != stepScroll;
    stepScroll = nextStepScroll;
    // A stroke or a marquee in the velocity lane is below the rows the whole
    // time, and would otherwise scroll pitch at the gesture clock's rate.
    if (!session.isPatternDrums() && !isVelocityLaneGesture(gesture))
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

// The view follows the pointer: the steps move the pixels the mouse did, and
// the rows move by the whole rows it did, so the note that was under the
// cursor stays under it. Drum rows are the Drum Rack's bank and stay put, as
// they do for the wheel and the keys.
void StepGrid::panView(juce::Point<float> pointer)
{
    const auto maximumStart = std::max(0.0, static_cast<double>(session.editorStepCount()) - visibleStepSpan());
    stepScroll = std::clamp(panStartStep - (pointer.x - panAnchor.x) / cellWidth(), 0.0, maximumStart);
    horizontalScroll.setCurrentRange(stepScroll, visibleStepSpan(), juce::dontSendNotification);
    if (!session.isPatternDrums())
    {
        // Dragging down carries the notes down and brings the pitches above
        // them into view, so the lowest row on show climbs by the rows travelled.
        const auto next = juce::jlimit(0, 127 - visiblePitchRows() + 1,
                                       panStartPitch + juce::roundToInt((pointer.y - panAnchor.y) / rowHeight()));
        if (next != lowestVisiblePitch)
        {
            lowestVisiblePitch = next;
            manualPitchScroll = true;
            rebuildVisibleNotes();
        }
    }
    updatePlayhead();
    repaint();
}

juce::Result StepGrid::resizeCurrentNoteTo(int index)
{
    if (!resizingNoteState.isValid())
        return juce::Result::ok();
    if (index < 0)
        return juce::Result::ok();
    const auto targetStep = index % Session::steps;
    const auto length = std::max(0.0625, targetStep - resizingStartStep + 1.0);
    if (hasResizeTarget && std::abs(resizeTarget - length) < 0.0001)
        return juce::Result::ok();
    const auto result = session.resizeNote(resizingNoteState, length);
    if (result.wasOk())
    {
        resizeTarget = length;
        hasResizeTarget = true;
        noteMoved = true;
    }
    return result;
}

juce::Result StepGrid::resizeCurrentNoteTo(juce::Point<float> position, bool freeLength)
{
    if (!resizingNoteState.isValid())
        return juce::Result::ok();
    if (resizingFromLeft)
    {
        auto newStart = static_cast<double>(stepScroll + (position.x - labelWidth) / cellWidth());
        // Snapped, the edge lands on a step's line, always short of the note's
        // end; with Alt it follows the pointer.
        if (!freeLength)
            if (const auto* note = laneNoteFor(resizingNoteState))
                newStart = std::min(std::round(newStart), std::ceil(note->start + note->length - 0.0001) - 1.0);
        if (hasResizeTarget && std::abs(resizeTarget - newStart) < 0.0001)
            return juce::Result::ok();
        const auto result = session.resizeNoteFromLeft(resizingNoteState, newStart);
        if (result.wasOk())
        {
            resizeTarget = newStart;
            hasResizeTarget = true;
            noteMoved = true;
        }
        return result;
    }
    auto end = stepScroll + (position.x - labelWidth) / cellWidth();
    // Alt/Option follows the pointer continuously. Otherwise the edge lands on
    // a step's line, the first one past the note's start at the least; until
    // 2026-10-25 the first step went in sixteenths without Alt.
    if (!freeLength)
        end = std::max(std::round(end), std::floor(resizingStartStep + 0.0001) + 1.0);
    const auto length = std::max(0.0625, end - resizingStartStep);
    if (hasResizeTarget && std::abs(resizeTarget - length) < 0.0001)
        return juce::Result::ok();
    const auto result = session.resizeNote(resizingNoteState, length);
    if (result.wasOk())
    {
        resizeTarget = length;
        hasResizeTarget = true;
        noteMoved = true;
    }
    return result;
}

void StepGrid::mouseDrag(const juce::MouseEvent& event)
{
    dragGesture(event);
    // Whatever the drag did - carried a note to another row, scrolled the
    // keys under a still pointer - the keyboard names what it is on now.
    if (gesture != Gesture::none)
        setHoveredPitch(pitchNamedAt(event.position));
}

void StepGrid::dragGesture(const juce::MouseEvent& event)
{
    if (loopDragActive)
    {
        const auto edge = loopStepAt(event.position.x, event.mods.isAltDown());
        loopPreviewStartStep = std::min(loopAnchorStep, edge);
        loopPreviewEndStep = std::max(loopAnchorStep, edge);
        repaint();
        return;
    }
    // Before dragPosition is recorded: a pan follows the pointer itself, and
    // must never be mistaken for a drag the gesture clock should autoscroll.
    if (gesture == Gesture::pan)
    {
        panView(event.position);
        return;
    }
    if (gesture == Gesture::none) return;
    // Before dragPosition too: a chop's drag counts parts, and nothing about
    // it may autoscroll.
    if (gesture == Gesture::chop)
    {
        chopByMouse(event.position.y);
        return;
    }
    dragPosition = event.position;
    dragTravelled = dragTravelled || event.getDistanceFromDragStart() >= 3;
    if (isVelocityLaneGesture(gesture))
    {
        velocityLaneMouseDrag(event);
        return;
    }
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
        // A press that shakes by a pixel must not count as a move and undo a
        // click's selection.
        moveFreely = event.mods.isAltDown();
        if (noteMoved || dragTravelled)
            moveDraggedNotesAt(event.position);
        return;
    }
    if (gesture == Gesture::resize)
    {
        resizeFreely = event.mods.isAltDown();
        resizeCurrentNoteTo(event.position, resizeFreely);
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
    // A pencil sounds each row it paints on, once.
    if (adding && index >= 0 && session.noteAuditionEnabled() && pitchForIndex(index) != session.auditionedPitch())
        session.startNoteAudition(pitchForIndex(index), 100);
}

void StepGrid::mouseUp(const juce::MouseEvent& event)
{
    if (loopDragActive)
    {
        dragGesture(event);
        loopDragActive = false;
        if (event.getDistanceFromDragStart() >= 3 && loopPreviewEndStep > loopPreviewStartStep)
            session.setLoopRange(timelineTimeForLoopStep(loopPreviewStartStep),
                                 timelineTimeForLoopStep(loopPreviewEndStep));
        repaint();
        return;
    }
    // A pan opened no note gesture, so it has none to close.
    if (gesture == Gesture::pan)
    {
        panView(event.position);
        gesture = Gesture::none;
        updatePointer(event.position, event.mods);
        if (isMouseOver(true))
            updateHover(event.position);
        return;
    }
    // A press inside a group that never travelled was a click, and a click
    // takes the one thing under it. Deciding it here rather than on the press
    // is what lets the same press also carry the whole group.
    if (gesture == Gesture::move && collapseSelectionOnRelease && !noteMoved && clickedNoteState.isValid())
        setSelectedStates({clickedNoteState});
    // A key clicked rather than dragged: its notes, as Live selects them.
    if (gesture == Gesture::keyboard && !dragTravelled && keyboardPressPitch >= 0)
        selectPitch(keyboardPressPitch, event.mods);
    // Whatever was sounding under the pointer stops when it lets go.
    session.stopNoteAudition();
    const auto laneGesture = isVelocityLaneGesture(gesture);
    if (laneGesture)
        finishVelocityLaneGesture();
    // A chop's drag is inside the chop's own undo step, which stays open
    // until Ctrl comes up.
    else if (gesture != Gesture::none && gesture != Gesture::keyboard && gesture != Gesture::chop)
        session.endNoteGesture();
    chopMouseY = -1.0f;
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
    keyboardPressPitch = -1;
    moveFreely = false;
    dragStartStep = -1.0;
    dragPosition = {-1.0f, -1.0f};
    // V held, or a subdivision, outlives a press made during it, and the
    // clock is what notices V being let go.
    if (!velocityAdjustActive && !subdivisionActive)
        stopTimer();
    resizingNoteState = {};
    resizingFromLeft = false;
    hasResizeTarget = false;
    resizeFreely = false;
    noteMoved = false;
    // A marker may have been left away from the pointer, so the one that
    // lights is the one a press would now take.
    if (isMouseOver(true))
        updateHover(event.position);
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
    // A paste goes to the start of a span swept for it, as in Live.
    setInsertStep(std::floor(firstStep));
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
    // Ctrl and the wheel zoom, as they do nearly everywhere else. They used to
    // chop the selected notes, for anyone reaching for the zoom; the wheel
    // ends a chop now rather than driving it, which a drag does.
    finishSubdivision();
    if (event.mods.isShiftDown() || isShortcutDown(event.mods))
    {
        // Keep whatever is below the pointer stable while the visible range
        // changes. Over the keys there is no timeline to zoom, only lanes.
        if (isOverKeyboard(event.position))
            zoomPitchAt(std::exp(wheelDelta * 2.0f), event.position.y);
        else
            zoomAt(std::exp(wheelDelta * 2.0f), event.position.x);
    }
    // A wheel turned sideways, or a trackpad swiped sideways, travels along
    // the clip, as it does in the arrangement; it used to scroll the pitch.
    else if (std::abs(wheel.deltaX) > std::abs(wheel.deltaY))
    {
        const auto maximumStart = std::max(0.0, static_cast<double>(session.editorStepCount()) - visibleStepSpan());
        stepScroll = std::clamp(stepScroll - wheel.deltaX * visibleStepSpan() * 0.3, 0.0, maximumStart);
        syncHorizontalScroll();
        updatePlayhead();
        repaint();
    }
    else
    {
        const auto semitones = std::max(1, juce::roundToInt(std::abs(wheelDelta) * 8.0f));
        scrollPitchBy(wheelDelta > 0.0f ? semitones : -semitones);
    }
    // The rows moved under a pointer that did not.
    setHoveredPitch(pitchNamedAt(event.position));
}

}
