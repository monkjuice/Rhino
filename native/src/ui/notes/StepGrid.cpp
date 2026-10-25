#include "StepGridInternal.h"
#include "ui/shared/Playhead.h"
#include "ui/shared/UiVisibility.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace rhino
{

StepGrid::StepGrid(Session& s) : session(s), vblank(this, [this] { updatePlayhead(); })
{
    setOpaque(true);
    setWantsKeyboardFocus(true);
    setTitle("Pattern notes");
    setDescription("Step editor. Drag to select, double-click a cell for a note, right-click for options, "
                   "B for draw mode. Drag the piano keys sideways to resize the lanes, up and down to scroll them. "
                   "The velocity lane under the notes: drag a marker to change its note's velocity.");
    drawButton.setButtonText("Draw");
    drawButton.setTooltip("Draw mode (B). Drag to paint notes, and press a note to erase it; with it off, drag "
                          "selects and double-clicking a cell adds one note. Right-click for options in either.");
    drawButton.setClickingTogglesState(true);
    drawButton.setWantsKeyboardFocus(false);
    drawButton.setColour(juce::TextButton::buttonColourId, palette::noteEditorButton);
    drawButton.setColour(juce::TextButton::buttonOnColourId, palette::noteEditorButtonOn);
    drawButton.onClick = [this]
    {
        setDrawMode(drawButton.getToggleState());
        grabKeyboardFocus();
    };
    addAndMakeVisible(drawButton);
    loopButton.setButtonText(juce::String::charToString(0x27f3));
    loopButton.setTooltip("Loop the entire edited clip (Ctrl+L). Drag on the ruler to draw a loop; right-click to clear it.");
    loopButton.setWantsKeyboardFocus(false);
    loopButton.onClick = [this]
    {
        loopEditedClip();
        grabKeyboardFocus();
    };
    addAndMakeVisible(loopButton);
    setUpVelocityLane();
    horizontalScroll.addListener(this);
    addAndMakeVisible(horizontalScroll);
    session.addChangeListener(this);
    changeListenerCallback(nullptr);
}

StepGrid::~StepGrid()
{
    finishSubdivision();
    finishVelocityAdjustment();
    if (holdsVelocityGesture(gesture)) session.endNoteVelocityGesture();
    else if (gesture != Gesture::none && gesture != Gesture::keyboard && gesture != Gesture::pan
             && !isVelocityLaneGesture(gesture))
        session.endNoteGesture();
    horizontalScroll.removeListener(this);
    session.removeChangeListener(this);
}

juce::Rectangle<float> StepGrid::cell(int step, int row) const
{
    const auto width = cellWidth();
    const auto height = rowHeight();
    return {labelWidth + static_cast<float>(step - stepScroll) * width, headerHeight + row * height, width, height};
}

juce::Rectangle<float> StepGrid::boundsFor(const VisibleNote& note) const
{
    auto bounds = cell(static_cast<int>(std::floor(note.start)), note.row);
    bounds.translate(static_cast<float>(note.start - std::floor(note.start)) * cellWidth(), 0.0f);
    bounds.setWidth(std::max(3.0f, static_cast<float>(note.length) * cellWidth()));
    bounds.setRight(std::min(bounds.getRight(), gridRight()));
    return bounds;
}

const StepGrid::VisibleNote* StepGrid::noteForState(const juce::ValueTree& state) const
{
    const auto found = std::find_if(visibleNotes.begin(), visibleNotes.end(), [&state](const auto& note)
    {
        return note.state == state;
    });
    return found == visibleNotes.end() ? nullptr : &*found;
}

bool StepGrid::isSelected(const juce::ValueTree& state) const
{
    return std::find(selectedNoteStates.begin(), selectedNoteStates.end(), state) != selectedNoteStates.end();
}

std::vector<juce::ValueTree> StepGrid::selectedStates() const
{
    std::vector<juce::ValueTree> result;
    std::bitset<Session::steps * maxPitchRows> representedCells;
    for (const auto& state : selectedNoteStates)
    {
        const auto* note = noteForState(state);
        if (note == nullptr)
        {
            // Selected, but scrolled out of the pitch window. The cell bitset is
            // addressed by visible row and so has nowhere to record it; the
            // explicit list is the only account of it, and it is the true one.
            result.push_back(state);
            continue;
        }
        const auto index = note->row * Session::steps + static_cast<int>(std::floor(note->start));
        if (index >= 0 && selectedNotes.test(static_cast<size_t>(index)))
        {
            result.push_back(state);
            representedCells.set(static_cast<size_t>(index));
        }
    }

    // Keep cell-addressed selection commands meaningful. If a selected cell
    // has no explicit instance selection, it represents every retrigger that
    // begins in that cell.
    for (const auto& note : visibleNotes)
    {
        const auto index = note.row * Session::steps + static_cast<int>(std::floor(note.start));
        if (index >= 0 && selectedNotes.test(static_cast<size_t>(index))
            && !representedCells.test(static_cast<size_t>(index)))
            result.push_back(note.state);
    }
    return result;
}

void StepGrid::setSelectedStates(std::vector<juce::ValueTree> states)
{
    // What changed colour has to be repainted here: a click on a note in a
    // grid that already has the focus repaints nothing else, and the lane
    // shows the selection too. Past a few hundred notes the diff costs more
    // than painting the panel.
    const auto changed = states != selectedNoteStates;
    const auto few = states.size() + selectedNoteStates.size() <= 128;
    std::vector<juce::ValueTree> toggled;
    if (changed && few)
    {
        for (const auto& state : states)
            if (std::find(selectedNoteStates.begin(), selectedNoteStates.end(), state) == selectedNoteStates.end())
                toggled.push_back(state);
        for (const auto& state : selectedNoteStates)
            if (std::find(states.begin(), states.end(), state) == states.end())
                toggled.push_back(state);
    }
    selectedNoteStates = std::move(states);
    selectedNotes.reset();
    for (const auto& state : selectedNoteStates)
        if (const auto* note = noteForState(state))
        {
            const auto index = note->row * Session::steps + static_cast<int>(std::floor(note->start));
            if (index >= 0) selectedNotes.set(static_cast<size_t>(index));
        }
    // The region the clipboard commands act on follows the selection, so this
    // is the one place it has to be kept in step.
    setStepRegionFromSelection();
    if (getHeight() <= 0)
        return;
    repaint(footerBounds().getSmallestIntegerContainer());
    if (changed && !few)
    {
        repaint();
        return;
    }
    if (!changed)
        return;
    repaintVelocityLane();
    for (const auto& state : toggled)
        if (const auto* note = noteForState(state))
            repaint(boundsFor(*note).getSmallestIntegerContainer().expanded(3));
}

void StepGrid::zoomIn()
{
    zoomAt(1.5, labelWidth + gridWidth() * 0.5f);
}

void StepGrid::zoomOut()
{
    zoomAt(1.0 / 1.5, labelWidth + gridWidth() * 0.5f);
}

void StepGrid::zoomAt(double factor, float pointerX)
{
    const auto anchor = std::clamp((pointerX - labelWidth) / gridWidth(), 0.0f, 1.0f);
    const auto anchorStep = stepScroll + static_cast<double>(anchor) * visibleStepSpan();
    stepZoom = std::clamp(stepZoom * factor, 1.0, 8.0);
    stepScroll = anchorStep - static_cast<double>(anchor) * visibleStepSpan();
    syncHorizontalScroll();
    // The scrollbar coming or going can fold the velocity lane or bring it
    // back, which moves the rows by more than the scrollbar's own height.
    if (computePitchRowCount() != pitchRowCount)
        changeListenerCallback(nullptr);
    updatePlayhead();
    repaint();
}

void StepGrid::zoomPitchAt(double factor, float pointerY)
{
    const auto anchor = std::clamp((pointerY - headerHeight) / rowAreaHeight(), 0.0f, 1.0f);
    const auto before = visiblePitchRows();
    // The pitch under the pointer, as a continuous position in the row area.
    // Holding it still is what makes the lanes grow out from the pointer
    // rather than from the top of the grid.
    const auto held = lowestVisiblePitch + before * (1.0 - anchor) - 1.0;
    pitchZoom = std::clamp(pitchZoom * factor,
                           static_cast<double>(minPitchRows) / Session::pitches,
                           static_cast<double>(maxPitchRows) / Session::pitches);
    const auto after = computePitchRowCount();
    if (after == before)
        return;
    pitchRowCount = after;
    lowestVisiblePitch = juce::jlimit(0, 127 - after + 1,
                                      juce::roundToInt(held - after * (1.0 - anchor) + 1.0));
    manualPitchScroll = !session.isPatternDrums();
    rebuildVisibleNotes();
    repaint();
}

void StepGrid::scrollPitchBy(int semitones)
{
    if (semitones == 0 || session.isPatternDrums())
        return;
    const auto next = juce::jlimit(0, 127 - visiblePitchRows() + 1, lowestVisiblePitch + semitones);
    if (next == lowestVisiblePitch)
        return;
    lowestVisiblePitch = next;
    manualPitchScroll = true;
    rebuildVisibleNotes();
}

void StepGrid::setDrawMode(bool shouldDraw)
{
    if (drawMode == shouldDraw)
        return;
    drawMode = shouldDraw;
    drawButton.setToggleState(drawMode, juce::dontSendNotification);
    setDescription(drawMode
        ? "Step editor, draw mode. Drag to paint notes, or velocities in the velocity lane; "
          "press a note to erase it, B to go back to selecting."
        : "Step editor. Drag to select, double-click a cell for a note, right-click for options, "
          "B for draw mode. Drag the piano keys sideways to resize the lanes, up and down to scroll them. "
          "The velocity lane under the notes: drag a marker to change its note's velocity.");
    repaint(footerBounds().getSmallestIntegerContainer());
}

void StepGrid::toggleDrawMode()
{
    setDrawMode(!drawMode);
}

void StepGrid::setScaleHighlight(int selection)
{
    if (scaleHighlight != selection)
    {
        scaleHighlight = selection;
        repaint();
    }
}

float StepGrid::timeAreaHeight() const
{
    return std::max(1.0f, getHeight() - headerHeight - footerHeight
                              - (horizontalScroll.isVisible() ? scrollHeight : 0.0f));
}

float StepGrid::timeAreaBottom() const
{
    return headerHeight + timeAreaHeight();
}

// What the velocity lane leaves of the time area. Every hit test and every
// row is bounded by this, so none of them can reach into the lane.
float StepGrid::rowAreaHeight() const
{
    return std::max(1.0f, timeAreaHeight() - velocityLaneBlockHeight());
}

float StepGrid::rowHeight() const
{
    return rowAreaHeight() / static_cast<float>(visiblePitchRows());
}

int StepGrid::computePitchRowCount() const
{
    // Zoom asks for a row count; the panel's height caps how thin a row is
    // allowed to get. The default range always fits, however short the panel,
    // so a small editor shows the same sixteen lanes it always did.
    const auto ceiling = std::max(Session::pitches,
                                  std::clamp(static_cast<int>(rowAreaHeight() / minimumRowHeight),
                                             minPitchRows, maxPitchRows));
    return std::clamp(juce::roundToInt(Session::pitches * pitchZoom), minPitchRows, ceiling);
}

juce::Rectangle<float> StepGrid::footerBounds() const
{
    return {0.0f, static_cast<float>(getHeight()) - footerHeight,
            static_cast<float>(getWidth()), footerHeight};
}

float StepGrid::cellWidth() const
{
    return gridWidth() / static_cast<float>(visibleStepSpan());
}

float StepGrid::gridRight() const
{
    return static_cast<float>(getWidth());
}

float StepGrid::gridWidth() const
{
    return std::max(1.0f, gridRight() - labelWidth);
}

double StepGrid::visibleStepSpan() const
{
    return std::clamp(static_cast<double>(session.editorStepCount()) / stepZoom,
                      1.0, static_cast<double>(session.editorStepCount()));
}

void StepGrid::timerCallback()
{
    if (velocityAdjustActive && !juce::KeyPress::isKeyCurrentlyDown('V'))
        finishVelocityAdjustment();
    if (subdivisionActive)
    {
        if (!isShortcutDown(juce::ModifierKeys::getCurrentModifiersRealtime())) finishSubdivision();
        return;
    }
    // A pointer held still past the edge of the grid sends no more drags, so
    // the gesture is carried on from here: the view scrolls, and whatever is
    // being dragged is re-read against the music that has come into view.
    if (!dragTravelled || dragPosition.x < 0.0f)
        return;
    autoScrollDrag();
    switch (gesture)
    {
        case Gesture::move:   moveDraggedNotesAt(dragPosition); break;
        case Gesture::select: updateMarqueeSelection(); repaint(); break;
        case Gesture::resize: resizeCurrentNoteTo(dragPosition, resizeFreely); break;
        case Gesture::draw:   apply(cellHit(dragPosition)); break;
        // A stroke held still at the edge paints the columns that scroll in
        // under it, as a stroke of notes does.
        case Gesture::velocityDraw:
            paintVelocityStrokeTo(dragPosition, juce::ModifierKeys::getCurrentModifiersRealtime().isAltDown());
            break;
        case Gesture::velocitySelect: updateVelocityMarquee(); repaint(); break;
        default: break;
    }
    // Pitch may have scrolled under a pointer held still at the edge.
    setHoveredPitch(pitchNamedAt(dragPosition));
}

bool StepGrid::keyPressed(const juce::KeyPress& key)
{
    const auto command = isShortcutDown(key.getModifiers());
    // Inside a marker drag or a stroke the open undo transaction is the
    // gesture's own. Any edit from the keyboard would split it, or take a note
    // out from under it, so the editor's own keys wait for the release; keys
    // it does not handle, the transport's among them, still pass.
    if (holdsVelocityGesture(gesture))
    {
        const auto code = key.getKeyCode();
        const int plainKeys[] {juce::KeyPress::upKey, juce::KeyPress::downKey, juce::KeyPress::leftKey,
                               juce::KeyPress::rightKey, juce::KeyPress::deleteKey,
                               juce::KeyPress::backspaceKey, juce::KeyPress::escapeKey, 'V', 'B', 'F'};
        const int commandKeys[] {'Z', 'Y', 'L', 'A', 'C', 'X', 'E', 'V', 'D'};
        const auto among = [code](const auto& keys) { return std::find(std::begin(keys), std::end(keys), code) != std::end(keys); };
        if (among(plainKeys) || (command && among(commandKeys)))
            return true;
    }
    if (velocityAdjustActive
        && (key.getKeyCode() == juce::KeyPress::upKey || key.getKeyCode() == juce::KeyPress::downKey))
        return adjustVelocity(key.getKeyCode() == juce::KeyPress::upKey ? 1 : -1);
    if (!command && (key.getKeyCode() == 'V' || key.getTextCharacter() == 'v' || key.getTextCharacter() == 'V'))
        return beginVelocityAdjustment();
    if (velocityAdjustActive)
        finishVelocityAdjustment();
    if (subdivisionActive && command
        && (key.getKeyCode() == juce::KeyPress::upKey || key.getKeyCode() == juce::KeyPress::rightKey
            || key.getKeyCode() == juce::KeyPress::downKey || key.getKeyCode() == juce::KeyPress::leftKey))
        return adjustSubdivision(key.getKeyCode() == juce::KeyPress::upKey
                              || key.getKeyCode() == juce::KeyPress::rightKey ? 1 : -1);
    if (subdivisionActive && !(command && key.getKeyCode() == 'E'))
        finishSubdivision();
    const auto up = key.getKeyCode() == juce::KeyPress::upKey;
    const auto down = key.getKeyCode() == juce::KeyPress::downKey;
    // Live's keys: the selection's velocity by 10, or by 1 with Shift.
    if (command && (up || down))
        return nudgeSelectedVelocities((up ? 1 : -1) * (key.getModifiers().isShiftDown() ? 1 : 10));
    if (command && key.getKeyCode() == 'Z')
    {
        if (key.getModifiers().isShiftDown()) session.redo();
        else session.undo();
        return true;
    }
    if (command && key.getKeyCode() == 'Y')
    {
        session.redo();
        return true;
    }
    if (command && key.getKeyCode() == 'L')
    {
        loopEditedClip();
        return true;
    }
    if (command && key.getKeyCode() == 'A')
        return selectAllNotes();
    if (command && key.getKeyCode() == 'C')
        return copySelection();
    if (command && key.getKeyCode() == 'X')
        return cutSelection();
    // Always the editor's own: here it divides notes, and a refusal must not
    // fall through to the shell, where Ctrl+E splits the arrangement's clip.
    if (command && key.getKeyCode() == 'E')
    {
        if (!subdivisionActive && !beginSubdivision())
            report("Select a note, or notes on one pitch, to divide");
        return true;
    }
    if (command && key.getKeyCode() == 'V')
        return pasteSelection();
    if (command && key.getKeyCode() == 'D')
        return duplicateSelection();
    if (key.getModifiers().isShiftDown()
        && (key.getKeyCode() == juce::KeyPress::leftKey || key.getKeyCode() == juce::KeyPress::rightKey))
    {
        const auto states = selectedStates();
        if (states.empty()) return false;
        const auto delta = key.getKeyCode() == juce::KeyPress::rightKey ? 1.0 : -1.0;
        const auto amount = key.getModifiers().isAltDown() ? delta / 16.0 : delta;
        session.beginNoteGesture("Resize notes");
        for (const auto& state : states)
            if (const auto* note = noteForState(state))
                session.resizeNote(state, std::max(0.0625, note->length + amount));
        session.endNoteGesture();
        return true;
    }
    // Live's keys: Left and Right carry the selected notes a step along the
    // clip, and with Alt a sixteenth of one. Shift is their length, above.
    if (!command && (key.getKeyCode() == juce::KeyPress::leftKey || key.getKeyCode() == juce::KeyPress::rightKey))
    {
        const auto step = key.getModifiers().isAltDown() ? 1.0 / 16.0 : 1.0;
        return nudgeSelection(key.getKeyCode() == juce::KeyPress::rightKey ? step : -step);
    }
    if (!command
        && (key.getKeyCode() == juce::KeyPress::upKey || key.getKeyCode() == juce::KeyPress::downKey))
    {
        const auto direction = key.getKeyCode() == juce::KeyPress::upKey ? 1 : -1;
        return transposeSelection(direction * (key.getModifiers().isShiftDown() ? 12 : 1));
    }
    // Live's key for the same mode, so the hand that already knows one knows
    // the other.
    if (!command && key.getKeyCode() == 'B')
    {
        toggleDrawMode();
        return true;
    }
    // Plain F only: Ctrl+F is the browser's search, and it has to get past
    // the editor holding the focus.
    if (!command && key.getKeyCode() == 'F')
        return fillSelectionToClipEnd();
    if (key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey)
        return deleteSelection();
    if (key.getKeyCode() == juce::KeyPress::escapeKey)
    {
        clearSelection();
        clearStepSelection();
        setInsertStep(-1.0);
        repaint();
        return true;
    }
    return false;
}

// Nothing is drawn for the focus: the cyan frame that was drawn round the
// whole editor for as long as it held the keys read as a selection of its
// own, and the arrangement draws none either.
void StepGrid::focusLost(juce::Component::FocusChangeType)
{
    finishSubdivision();
    finishVelocityAdjustment();
}

int StepGrid::automaticLowestPitch() const
{
    // A Drum Rack's rows are the bank of pads its face shows.
    if (session.isPatternDrums())
        return juce::jlimit(0, 127 - visiblePitchRows() + 1, session.patternDrumLowestNote());
    if (!session.hasPatternClip())
        return Session::lowestNote;

    int minPitch = 128, maxPitch = 0;
    for (auto* note : session.pattern().getSequence().getNotes())
    {
        minPitch = std::min(minPitch, note->getNoteNumber());
        maxPitch = std::max(maxPitch, note->getNoteNumber());
    }

    if (minPitch > maxPitch)
        return Session::lowestNote;

    const auto rows = visiblePitchRows();
    auto base = std::min(Session::lowestNote, minPitch);
    if (maxPitch >= base + rows)
        base = maxPitch - rows + 1;
    return juce::jlimit(0, 127 - rows + 1, base);
}

void StepGrid::visibilityChanged()
{
    if (staleWhileHidden && !isHiddenInShell(*this))
    {
        staleWhileHidden = false;
        changeListenerCallback(nullptr);
    }
}

void StepGrid::changeListenerCallback(juce::ChangeBroadcaster*)
{
    if (isHiddenInShell(*this))
    {
        staleWhileHidden = true;
        return;
    }
    // The insert line marks a place in one clip, and in the next it would
    // mark nothing anyone chose.
    if (const auto clip = session.hasPatternClip() ? session.pattern().itemID : te::EditItemID(); clip != shownClip)
    {
        shownClip = clip;
        insertStep = -1.0;
        repaint();
    }
    syncHorizontalScroll();
    const auto previousLowestPitch = lowestVisiblePitch;
    if (session.isPatternDrums())
        manualPitchScroll = false;
    if (!manualPitchScroll)
        lowestVisiblePitch = automaticLowestPitch();
    rebuildVisibleNotes();
    if (!session.hasPatternClip())
    {
        if (previousLowestPitch != lowestVisiblePitch)
            repaint();
        return;
    }
    const auto& clipRange = session.pattern().getPosition().time;
    const auto loopRange = session.loopRange();
    const auto loopsEditedClip = session.hasManualLoopRange()
        && std::abs(loopRange.getStart().inSeconds() - clipRange.getStart().inSeconds()) < 0.0001
        && std::abs(loopRange.getEnd().inSeconds() - clipRange.getEnd().inSeconds()) < 0.0001;
    loopButton.setColour(juce::TextButton::buttonColourId,
                         loopsEditedClip ? palette::noteEditorButtonOn : palette::noteEditorButton);
    if (previousLowestPitch != lowestVisiblePitch)
        repaint();
}

void StepGrid::rebuildVisibleNotes()
{
    // The scrollbar decides how tall the row area is, and the row area decides
    // how many rows fit, so both settle before any note is placed on one -
    // the clip's length first, because it decides the zoom and so the
    // scrollbar, and the scrollbar can fold the velocity lane.
    const auto steps = session.editorStepCount();
    const auto stepCountChanged = steps != visibleStepCount;
    if (stepCountChanged)
        stepZoom = std::max(1.0, static_cast<double>(steps) / static_cast<double>(Session::defaultSteps));
    visibleStepCount = steps;
    syncHorizontalScroll();
    const auto previousRows = pitchRowCount;
    pitchRowCount = computePitchRowCount();
    const auto rows = pitchRowCount;
    lowestVisiblePitch = juce::jlimit(0, 127 - rows + 1, lowestVisiblePitch);
    std::bitset<Session::steps * maxPitchRows> next;
    std::array<float, Session::steps * maxPitchRows> nextLengths {}, nextStartOffsets {};
    const auto nextDrumLabels = session.isPatternDrums();
    std::vector<VisibleNote> nextVisible;
    const auto allNotes = session.editorNotes();
    for (const auto& note : allNotes)
    {
        const auto row = lowestVisiblePitch + rows - 1 - note.pitch;
        const auto step = static_cast<int>(std::floor(note.startSteps));
        if (row >= 0 && row < rows && step >= 0 && step < steps)
        {
            nextVisible.push_back({note.state, note.startSteps, note.lengthSteps, note.pitch, row, note.velocity});
            next.set(static_cast<size_t>(row * Session::steps + step));
            nextLengths[static_cast<size_t>(row * Session::steps + step)] =
                static_cast<float>(std::max(0.0625, note.lengthSteps));
            nextStartOffsets[static_cast<size_t>(row * Session::steps + step)] =
                static_cast<float>(note.startSteps - step);
        }
    }
    const auto changed = next ^ notes;
    const auto lengthsChanged = nextLengths != noteLengths;
    const auto offsetsChanged = nextStartOffsets != noteStartOffsets;
    notes = next;
    noteLengths = nextLengths;
    noteStartOffsets = nextStartOffsets;
    visibleNotes = std::move(nextVisible);
    // Before the selection is reconciled below, which repaints the lane.
    rebuildLaneNotes(allNotes, rows);
    // Drop notes that have left the clip, never notes that have merely left the
    // pitch window. Selection outlives scrolling, so a pattern taller than the
    // window can still be selected and transposed whole.
    std::erase_if(selectedNoteStates, [&allNotes](const auto& state)
    {
        return std::none_of(allNotes.begin(), allNotes.end(),
                            [&state](const auto& note) { return note.state == state; });
    });
    setSelectedStates(selectedNoteStates);
    juce::StringArray nextRowNames;
    if (nextDrumLabels)
        for (int row = 0; row < rows; ++row)
            nextRowNames.add(session.patternNoteName(lowestVisiblePitch + rows - 1 - row).value_or(juce::String()));
    const auto rowNamesChanged = nextRowNames != drumRowNames;
    drumRowNames = std::move(nextRowNames);
    if (stepCountChanged || rows != previousRows || lengthsChanged || offsetsChanged
        || showingDrumLabels != nextDrumLabels || rowNamesChanged)
    {
        showingDrumLabels = nextDrumLabels;
        repaint();
        return;
    }
    if (manualPitchScroll)
    {
        repaint();
        return;
    }
    for (int row = 0; row < rows; ++row)
    for (int step = 0; step < steps; ++step)
    {
        const auto index = row * Session::steps + step;
        if (changed.test(static_cast<size_t>(index))) repaint(cell(step, row).getSmallestIntegerContainer());
    }
}

void StepGrid::updatePlayhead()
{
    // Every display refresh asks, shown or not; a hidden editor has no
    // playhead anyone can see.
    if (isHiddenInShell(*this))
        return;
    float next = -1.0f;
    next = playheadXForTime(session.playheadSeconds());
    movePlayhead(*this, playhead, next,
                 getLocalBounds().withTrimmedTop(static_cast<int>(headerHeight))
                                 .withTrimmedBottom(static_cast<int>(footerHeight)
                                     + (horizontalScroll.isVisible() ? static_cast<int>(scrollHeight) : 0)));
}

float StepGrid::playheadXForTime(double seconds) const
{
    if (!session.hasPatternClip())
        return -1.0f;
    const auto& position = session.pattern().getPosition();
    const auto clipStart = position.time.getStart().inSeconds();
    const auto clipEnd = position.time.getEnd().inSeconds();
    if (seconds < clipStart || seconds >= clipEnd)
        return -1.0f;

    const auto editBeat = session.secondsToBeats(seconds);
    const auto clipStartBeat = session.secondsToBeats(position.time.getStart().inSeconds());
    const auto offsetBeat = position.offset.inSeconds() * session.tempo() / 60.0;
    auto localBeat = std::fmod(editBeat - clipStartBeat + offsetBeat, session.patternLengthBeats());
    if (localBeat < 0.0)
        localBeat += session.patternLengthBeats();
    const auto step = localBeat / 4.0 * session.editorStepResolution();
    if (step < stepScroll || step > stepScroll + visibleStepSpan())
        return -1.0f;
    return static_cast<float>(labelWidth + (step - stepScroll) * cellWidth());
}

void StepGrid::syncHorizontalScroll()
{
    const auto steps = session.editorStepCount();
    const auto visible = visibleStepSpan();
    const auto maximumStart = std::max(0.0, static_cast<double>(steps) - visible);
    stepScroll = std::clamp(stepScroll, 0.0, maximumStart);
    horizontalScroll.setVisible(maximumStart > 0.001);
    horizontalScroll.setRangeLimits(0.0, static_cast<double>(steps), juce::dontSendNotification);
    horizontalScroll.setCurrentRange(stepScroll, visible, juce::dontSendNotification);
    // A scrollbar that comes or goes can fold the lane or bring it back.
    updateVelocityButton();
}

void StepGrid::scrollBarMoved(juce::ScrollBar* bar, double start)
{
    if (bar != &horizontalScroll)
        return;
    const auto maximumStart = std::max(0.0, static_cast<double>(session.editorStepCount()) - visibleStepSpan());
    stepScroll = std::clamp(start, 0.0, maximumStart);
    updatePlayhead();
    repaint();
}

void StepGrid::resized()
{
    loopButton.setBounds(4, 2, static_cast<int>(labelWidth) - 8, static_cast<int>(headerHeight) - 4);
    // In the footer beside the velocity readout: it is a mode the pointer is
    // in, so it belongs where the editor's own state is reported rather than
    // in the shell's toolbar above it.
    drawButton.setBounds(6, getHeight() - static_cast<int>(footerHeight) + 3,
                         56, static_cast<int>(footerHeight) - 6);
    velocityButton.setBounds(66, getHeight() - static_cast<int>(footerHeight) + 3,
                             60, static_cast<int>(footerHeight) - 6);
    syncHorizontalScroll();
    horizontalScroll.setBounds(static_cast<int>(labelWidth),
                               getHeight() - static_cast<int>(footerHeight + scrollHeight),
                               std::max(1, static_cast<int>(gridRight() - labelWidth)), static_cast<int>(scrollHeight));
    // A new height can lift or lower the cap on how thin a row may be, which
    // changes which pitches are on screen and so where every note sits.
    if (computePitchRowCount() != pitchRowCount)
        changeListenerCallback(nullptr);
    updatePlayhead();
    repaint();
}
}
