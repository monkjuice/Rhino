#include "StepGridInternal.h"
#include "Playhead.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace theta
{

StepGrid::StepGrid(Session& s) : session(s), vblank(this, [this] { updatePlayhead(); })
{
    setOpaque(true);
    setWantsKeyboardFocus(true);
    setTitle("Pattern notes");
    setDescription("One bar step editor. Drag to draw or erase notes. "
                   "Drag the piano keys sideways to resize the lanes, up and down to scroll them.");
    loopButton.setButtonText(juce::String::charToString(0x27f3));
    loopButton.setTooltip("Loop the entire edited clip (Ctrl+L). Drag on the ruler to draw a loop; right-click to clear it.");
    loopButton.setWantsKeyboardFocus(false);
    loopButton.onClick = [this]
    {
        loopEditedClip();
        grabKeyboardFocus();
    };
    addAndMakeVisible(loopButton);
    horizontalScroll.addListener(this);
    addAndMakeVisible(horizontalScroll);
    session.addChangeListener(this);
    changeListenerCallback(nullptr);
}

StepGrid::~StepGrid()
{
    finishSubdivision();
    finishVelocityAdjustment();
    if (gesture != Gesture::none && gesture != Gesture::keyboard) session.endNoteGesture();
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
        if (const auto* note = noteForState(state))
        {
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
    selectedNoteStates = std::move(states);
    selectedNotes.reset();
    for (const auto& state : selectedNoteStates)
        if (const auto* note = noteForState(state))
        {
            const auto index = note->row * Session::steps + static_cast<int>(std::floor(note->start));
            if (index >= 0) selectedNotes.set(static_cast<size_t>(index));
        }
    if (getHeight() > 0)
        repaint(footerBounds().getSmallestIntegerContainer());
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

void StepGrid::setScaleHighlight(int selection)
{
    if (scaleHighlight != selection)
    {
        scaleHighlight = selection;
        repaint();
    }
}

float StepGrid::rowAreaHeight() const
{
    return std::max(1.0f, getHeight() - headerHeight - footerHeight
                              - (horizontalScroll.isVisible() ? scrollHeight : 0.0f));
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
    scrollDraggedNotes();
    moveDraggedNotesAt(dragPosition);
}

bool StepGrid::keyPressed(const juce::KeyPress& key)
{
    const auto command = isShortcutDown(key.getModifiers());
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
    if (command && key.getKeyCode() == 'E')
        return subdivisionActive || beginSubdivision();
    if (command && key.getKeyCode() == 'V')
        return pasteSelection();
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
    if (key.getKeyCode() == 'F')
        return fillSelectionToClipEnd();
    if (key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey)
        return deleteSelection();
    if (key.getKeyCode() == juce::KeyPress::escapeKey)
    {
        clearSelection();
        return true;
    }
    return false;
}

void StepGrid::focusGained(juce::Component::FocusChangeType)
{
    repaint();
}

void StepGrid::focusLost(juce::Component::FocusChangeType)
{
    finishSubdivision();
    finishVelocityAdjustment();
    repaint();
}

int StepGrid::automaticLowestPitch() const
{
    if (session.isPatternDrums())
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

void StepGrid::changeListenerCallback(juce::ChangeBroadcaster*)
{
    syncHorizontalScroll();
    const auto previousLowestPitch = lowestVisiblePitch;
    if (session.isPatternDrums())
        manualPitchScroll = false;
    if (!manualPitchScroll)
        lowestVisiblePitch = automaticLowestPitch();
    rebuildVisibleNotes();
    const auto& clipRange = session.pattern().getPosition().time;
    const auto& loopRange = session.edit->getTransport().getLoopRange();
    const auto loopsEditedClip = session.hasManualLoopRange()
        && std::abs(loopRange.getStart().inSeconds() - clipRange.getStart().inSeconds()) < 0.0001
        && std::abs(loopRange.getEnd().inSeconds() - clipRange.getEnd().inSeconds()) < 0.0001;
    loopButton.setColour(juce::TextButton::buttonColourId,
                         loopsEditedClip ? juce::Colour(0xff397f94) : juce::Colour(0xff30373e));
    if (previousLowestPitch != lowestVisiblePitch)
        repaint();
}

void StepGrid::rebuildVisibleNotes()
{
    // The scrollbar decides how tall the row area is, and the row area decides
    // how many rows fit, so both settle before any note is placed on one.
    syncHorizontalScroll();
    const auto previousRows = pitchRowCount;
    pitchRowCount = computePitchRowCount();
    const auto rows = pitchRowCount;
    lowestVisiblePitch = juce::jlimit(0, 127 - rows + 1, lowestVisiblePitch);
    std::bitset<Session::steps * maxPitchRows> next;
    std::array<float, Session::steps * maxPitchRows> nextLengths {}, nextStartOffsets {};
    const auto nextDrumLabels = session.isPatternDrums();
    const auto steps = session.editorStepCount();
    std::vector<VisibleNote> nextVisible;
    for (const auto& note : session.editorNotes())
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
    const auto stepCountChanged = steps != visibleStepCount;
    if (stepCountChanged)
        stepZoom = std::max(1.0, static_cast<double>(steps) / static_cast<double>(Session::defaultSteps));
    visibleStepCount = steps;
    syncHorizontalScroll();
    notes = next;
    noteLengths = nextLengths;
    noteStartOffsets = nextStartOffsets;
    visibleNotes = std::move(nextVisible);
    std::erase_if(selectedNoteStates, [this](const auto& state) { return noteForState(state) == nullptr; });
    setSelectedStates(selectedNoteStates);
    if (stepCountChanged || rows != previousRows || lengthsChanged || offsetsChanged
        || showingDrumLabels != nextDrumLabels)
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
    float next = -1.0f;
    auto& transport = session.edit->getTransport();
    next = playheadXForTime(playheadTime(transport));
    movePlayhead(*this, playhead, next,
                 getLocalBounds().withTrimmedTop(static_cast<int>(headerHeight))
                                 .withTrimmedBottom(static_cast<int>(footerHeight)
                                     + (horizontalScroll.isVisible() ? static_cast<int>(scrollHeight) : 0)));
}

float StepGrid::playheadXForTime(double seconds) const
{
    const auto& position = session.pattern().getPosition();
    const auto clipStart = position.time.getStart().inSeconds();
    const auto clipEnd = position.time.getEnd().inSeconds();
    if (seconds < clipStart || seconds >= clipEnd)
        return -1.0f;

    const auto editBeat = session.edit->tempoSequence.toBeats(tracktion::core::TimePosition::fromSeconds(seconds)).inBeats();
    const auto clipStartBeat = session.edit->tempoSequence.toBeats(position.time.getStart()).inBeats();
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
