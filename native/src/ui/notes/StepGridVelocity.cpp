#include "StepGridInternal.h"
#include <algorithm>
#include <cmath>

// The velocity lane: Live's velocity editor, under the pitch rows and on the
// same time axis. Every note of the clip has a marker - a dot at its start, at
// the height of its velocity, and a bar along its length - including a note
// the pitch window has scrolled past, which still sounds.
//
// The lane shares the grid's selection and its pointer rules (SelectionInput.h):
// a press on a marker takes its note, Ctrl or Shift gathers, a press on empty
// lane sweeps a marquee. Dragging a marker moves every selected velocity by the
// same amount; in draw mode a stroke sets the velocity of the notes it crosses.
// A drag, a stroke and a held V are each one note-velocity gesture in the
// session: one undo step, announced once when it ends, so while it runs the
// grid reads the new values back from the model itself.
//
// This unit holds the lane's layout, its caches and its keys; the pointer is
// StepGridVelocityGestures.cpp and the painting StepGridVelocityPainter.cpp.

namespace rhino
{

void StepGrid::setUpVelocityLane()
{
    velocityButton.setButtonText("Velocity");
    velocityButton.setClickingTogglesState(true);
    velocityButton.setWantsKeyboardFocus(false);
    velocityButton.setColour(juce::TextButton::buttonColourId, palette::noteEditorButton);
    velocityButton.setColour(juce::TextButton::buttonOnColourId, palette::noteEditorButtonOn);
    velocityButton.onClick = [this]
    {
        setVelocityLaneShown(velocityButton.getToggleState());
        grabKeyboardFocus();
    };
    addAndMakeVisible(velocityButton);
    updateVelocityButton();
}

void StepGrid::setVelocityLaneShown(bool shouldShow)
{
    if (velocityLaneShown == shouldShow)
    {
        updateVelocityButton();
        return;
    }
    velocityLaneShown = shouldShow;
    updateVelocityButton();
    resized();
}

// On while the lane is wanted, dimmed while the pane is too short to show
// it, so a lane that folded away does not look as if it was switched off.
void StepGrid::updateVelocityButton()
{
    const auto folded = velocityLaneShown && velocityLaneHeight() <= 0.0f;
    if (velocityButton.getToggleState() != velocityLaneShown)
        velocityButton.setToggleState(velocityLaneShown, juce::dontSendNotification);
    const auto alpha = folded ? 0.5f : 1.0f;
    if (velocityButton.getAlpha() != alpha)
        velocityButton.setAlpha(alpha);
    const juce::String tip = !velocityLaneShown ? "Show the velocity lane under the notes."
                           : folded ? "No room for the velocity lane: drag the editor taller to show it."
                                    : "Hide the velocity lane. Drag the lane's top edge to resize it.";
    if (velocityButton.getTooltip() != tip)
        velocityButton.setTooltip(tip);
}

float StepGrid::velocityLaneHeight() const
{
    return velocitylane::laidOutHeight(velocityLaneShown, velocityLaneRequested, timeAreaHeight(),
                                       Session::pitches * octaveLabelRowHeight);
}

float StepGrid::velocityLaneBlockHeight() const
{
    const auto height = velocityLaneHeight();
    return height > 0.0f ? height + velocitylane::dividerHeight : 0.0f;
}

juce::Rectangle<float> StepGrid::laneDividerBounds() const
{
    if (velocityLaneHeight() <= 0.0f)
        return {};
    return {0.0f, headerHeight + rowAreaHeight(), static_cast<float>(getWidth()), velocitylane::dividerHeight};
}

juce::Rectangle<float> StepGrid::velocityLaneBounds() const
{
    const auto height = velocityLaneHeight();
    if (height <= 0.0f)
        return {};
    return {0.0f, headerHeight + rowAreaHeight() + velocitylane::dividerHeight, static_cast<float>(getWidth()), height};
}

// Where 127 and 1 are drawn: inset from the lane so a dot on either rail,
// hovered ring and all, stays inside it.
juce::Rectangle<float> StepGrid::velocityPlotBounds() const
{
    const auto lane = velocityLaneBounds();
    if (lane.isEmpty())
        return {};
    return {labelWidth, lane.getY() + velocitylane::valueInset, gridWidth(),
            lane.getHeight() - 2.0f * velocitylane::valueInset};
}

bool StepGrid::isOverLaneBlock(juce::Point<float> point) const
{
    const auto lane = velocityLaneBounds();
    return !lane.isEmpty() && point.x >= 0.0f && point.x < static_cast<float>(getWidth())
        && point.y >= laneDividerBounds().getY() && point.y < lane.getBottom();
}

bool StepGrid::isOverLaneDivider(juce::Point<float> point) const
{
    return laneDividerBounds().contains(point);
}

float StepGrid::yForVelocity(float velocity) const
{
    const auto plot = velocityPlotBounds();
    return velocitylane::yForVelocity(velocity, plot.getY(), plot.getHeight());
}

float StepGrid::velocityValueAtY(float y) const
{
    const auto plot = velocityPlotBounds();
    if (plot.getHeight() <= 0.0f)
        return 64.0f;
    return velocitylane::valueAtY(y, plot.getY(), plot.getHeight());
}

const StepGrid::LaneNote* StepGrid::laneNoteFor(const juce::ValueTree& state) const
{
    const auto found = std::find_if(laneNotes.begin(), laneNotes.end(),
                                    [&state](const auto& note) { return note.state == state; });
    return found == laneNotes.end() ? nullptr : &*found;
}

// Everything a marker can paint, hovered ring and thick bar included, with a
// margin for a fractional display scale.
juce::Rectangle<int> StepGrid::markerBounds(const LaneNote& note) const
{
    const auto stepWidth = cellWidth();
    const auto x0 = labelWidth + static_cast<float>(note.start - stepScroll) * stepWidth;
    const auto x1 = std::min(gridRight(), x0 + static_cast<float>(note.length) * stepWidth);
    const auto cx = static_cast<int>(std::floor(x0));
    const auto cy = static_cast<int>(std::floor(yForVelocity(static_cast<float>(note.velocity))));
    const auto right = std::max(cx + 6, static_cast<int>(std::ceil(x1)) + 1);
    return juce::Rectangle<int>(cx - 5, cy - 5, right - (cx - 5), 11).expanded(2);
}

// An index into laneNotes. Only markers near the point are ranked, so the
// selection is searched for a handful of notes rather than for every one.
int StepGrid::velocityMarkerAt(juce::Point<float> point) const
{
    const auto lane = velocityLaneBounds();
    if (lane.isEmpty() || !lane.contains(point) || point.x < labelWidth)
        return -1;
    const auto plot = velocityPlotBounds();
    const auto stepWidth = cellWidth();
    const auto right = gridRight();
    std::vector<velocitylane::Marker> markers;
    std::vector<int> indices;
    for (int index = 0; index < static_cast<int>(laneNotes.size()); ++index)
    {
        const auto& note = laneNotes[static_cast<size_t>(index)];
        const auto x0 = labelWidth + static_cast<float>(note.start - stepScroll) * stepWidth;
        if (x0 >= right)
            continue;
        const auto x1 = std::min(right, x0 + static_cast<float>(note.length) * stepWidth);
        const auto y = velocitylane::yForVelocity(static_cast<float>(note.velocity), plot.getY(), plot.getHeight());
        if (point.x < x0 - velocitylane::headReach || point.x > std::max(x0 + velocitylane::headReach, x1)
            || std::abs(point.y - y) > velocitylane::headReach)
            continue;
        markers.push_back({x0, x1, y, isSelected(note.state), note.inWindow});
        indices.push_back(index);
    }
    const auto picked = velocitylane::pickMarker(markers, point.x, point.y, labelWidth);
    return picked < 0 ? -1 : indices[static_cast<size_t>(picked)];
}

std::pair<juce::String, juce::String> StepGrid::velocityGutterText() const
{
    if (gesture == Gesture::velocityDraw)
        return {juce::String(velocitylane::clampVelocity(strokeLastValue)), {}};
    const auto& shown = gesture == Gesture::velocityDrag ? clickedNoteState : hoveredNoteState;
    const auto* note = shown.isValid() ? laneNoteFor(shown) : nullptr;
    if (note == nullptr)
        return {};
    const auto name = session.isPatternDrums() ? session.patternNoteName(note->pitch).value_or(pitchName(note->pitch))
                                               : pitchName(note->pitch);
    return {juce::String(note->velocity), name};
}

juce::String StepGrid::getTooltip()
{
    return tooltipAt(getMouseXYRelative().toFloat());
}

// The grid itself stays as silent as it always was; only the lane, whose
// gestures cannot be guessed from looking at it, explains itself.
juce::String StepGrid::tooltipAt(juce::Point<float> point) const
{
    if (!isOverLaneBlock(point))
        return {};
    if (isOverLaneDivider(point))
        return "Velocity lane edge. Drag to resize the lane, or all the way down to close it; "
               "double-click for its default height.";
    if (point.x < labelWidth)
        return "Velocity scale: 127 at the top, 1 at the bottom. Hover a marker to read its note's velocity here.";
    if (drawMode)
        return "Velocity lane, draw mode. Drag to paint the velocity of the notes under the pointer, step by step; "
               "hold Alt to paint one note at a time. With notes selected, only they are painted. "
               "B to go back to selecting.";
    return "Velocity lane. Drag a marker up or down to set its note's velocity, Shift for fine steps; "
           "Ctrl- or Shift-click to gather markers; drag empty space to select. "
           "Ctrl+Up/Down changes the selection by 10, Ctrl+Shift+Up/Down by 1.";
}

void StepGrid::rebuildLaneNotes(const std::vector<Session::EditorNote>& all, int rows)
{
    const auto steps = session.editorStepCount();
    std::vector<LaneNote> next;
    next.reserve(all.size());
    for (const auto& note : all)
    {
        const auto step = static_cast<int>(std::floor(note.startSteps));
        if (step < 0 || step >= steps)
            continue;
        const auto row = lowestVisiblePitch + rows - 1 - note.pitch;
        next.push_back({note.state, note.startSteps, note.lengthSteps, note.pitch, note.velocity, row >= 0 && row < rows});
    }
    if (next == laneNotes)
        return;
    laneNotes = std::move(next);
    if (hoveredNoteState.isValid() && laneNoteFor(hoveredNoteState) == nullptr)
        hoveredNoteState = {};
    repaintVelocityLane();
    // A marker moved out from under a still pointer - Ctrl+Up, an undo - no
    // longer lights; the one now under it does.
    if (gesture == Gesture::none && isMouseOver(true))
        updateHover(getMouseXYRelative().toFloat());
}

// The model is the one account of a velocity. Nothing announces inside a
// velocity gesture, so the grid asks it again after every write rather than
// keeping a second copy of its own up to date by hand.
void StepGrid::refreshNoteVelocities()
{
    const auto all = session.editorNotes();
    rebuildLaneNotes(all, visiblePitchRows());
    // The visible notes are the in-window part of the same list, in the same
    // order; if that ever stops being so, rebuild them from scratch.
    size_t from = 0;
    for (auto& visible : visibleNotes)
    {
        while (from < all.size() && all[from].state != visible.state)
            ++from;
        if (from == all.size())
        {
            rebuildVisibleNotes();
            break;
        }
        visible.velocity = all[from++].velocity;
    }
    if (getHeight() > 0)
        repaint(footerBounds().getSmallestIntegerContainer());
}

bool StepGrid::applyNoteVelocities(const std::vector<Session::NoteVelocity>& targets)
{
    if (targets.empty() || !session.setNoteVelocities(targets))
        return false;
    refreshNoteVelocities();
    return true;
}

// The whole strip, divider included. Any part of it nobody repaints shows an
// older frame under Direct2D.
void StepGrid::repaintVelocityLane()
{
    if (getHeight() <= 0 || velocityLaneHeight() <= 0.0f)
        return;
    repaint(laneDividerBounds().getUnion(velocityLaneBounds()).getSmallestIntegerContainer());
}

// Ctrl+Up/Down. Outside any gesture, so it is an ordinary note edit: one undo
// step, announced at once.
bool StepGrid::nudgeSelectedVelocities(int delta)
{
    if (gesture != Gesture::none)
        return true;
    const auto states = selectedStates();
    if (states.empty())
        return false;
    std::vector<Session::NoteVelocity> targets;
    for (const auto& state : states)
        if (const auto* note = laneNoteFor(state))
            targets.push_back({state, velocitylane::clampVelocity(note->velocity + delta)});
    session.beginNoteGesture("Change velocity");
    session.setNoteVelocities(targets);
    session.endNoteGesture();
    // Consumed even at a rail, as a transpose that cannot move is.
    return true;
}

}
