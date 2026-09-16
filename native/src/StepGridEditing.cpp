#include "StepGridInternal.h"
#include "Playhead.h"
#include <algorithm>
#include <cmath>
#include <limits>

// Selection commands, clipboard, and the subdivision and velocity tools.

namespace theta
{

void StepGrid::toggleSelection(int index)
{
    if (index < 0 || index >= static_cast<int>(visibleNotes.size()))
    {
        repaint();
        return;
    }
    const auto& note = visibleNotes[static_cast<size_t>(index)];
    auto current = selectedStates();
    if (std::find(current.begin(), current.end(), note.state) != current.end())
        std::erase(current, note.state);
    else
        current.push_back(note.state);
    setSelectedStates(std::move(current));
    const auto bounds = boundsFor(note);
    repaint(bounds.getSmallestIntegerContainer().expanded(3));
}

// Every note in the clip, not every note on screen. The pitch window shows
// sixteen rows by default and a pattern is routinely taller than that, so
// selecting what happens to be visible would quietly select a fragment.
bool StepGrid::selectAllNotes()
{
    const auto allNotes = session.editorNotes();
    std::vector<juce::ValueTree> all;
    all.reserve(allNotes.size());
    for (const auto& note : allNotes) all.push_back(note.state);
    setSelectedStates(std::move(all));
    repaint();
    return !selectedNoteStates.empty();
}

// Session::moveNotes clamps the shift so no note leaves 0-127 and refuses a
// lane that is already taken, so both edges are its business rather than ours.
bool StepGrid::transposeSelection(int semitones)
{
    const auto states = selectedStates();
    if (states.empty() || semitones == 0)
        return false;
    session.beginNoteGesture(std::abs(semitones) == 12 ? "Transpose octave" : "Transpose notes");
    juce::ignoreUnused(session.moveNotes(states, 0.0, semitones));
    session.endNoteGesture();
    // The key is consumed either way: a transpose that lands on an occupied
    // lane is a refusal, not an unhandled keystroke to pass further up.
    return true;
}

void StepGrid::clearSelection()
{
    if (selectedNotes.none() && selectedNoteStates.empty())
        return;
    selectedNotes.reset();
    selectedNoteStates.clear();
    repaint();
}

bool StepGrid::copySelection()
{
    noteClipboard.clear();
    const auto states = selectedStates();
    if (states.empty())
        return false;

    auto minStep = static_cast<double>(Session::steps);
    auto minPitch = 128;
    for (const auto& state : states)
        if (const auto* note = noteForState(state))
        {
            minStep = std::min(minStep, note->start);
            minPitch = std::min(minPitch, note->pitch);
        }
    if (minStep >= session.editorStepCount() || minPitch > 127)
        return false;
    clipboardBasePitch = minPitch;

    for (const auto& state : states)
        if (const auto* note = noteForState(state))
            noteClipboard.push_back({note->start - minStep, note->pitch - minPitch,
                                     std::max(0.001, note->length), note->velocity});
    return !noteClipboard.empty();
}

bool StepGrid::canPasteAt(int step) const
{
    for (const auto& copied : noteClipboard)
    {
        const auto targetStep = step + copied.step;
        const auto targetPitch = clipboardBasePitch + copied.pitch;
        for (const auto& existing : visibleNotes)
            if (existing.pitch == targetPitch
                && targetStep < existing.start + existing.length - 0.0001
                && existing.start < targetStep + copied.length - 0.0001)
                return false;
    }
    return true;
}

bool StepGrid::pasteSelection()
{
    if (noteClipboard.empty())
        return false;

    auto requiredSteps = 0;
    auto minPitchOffset = 0;
    auto maxPitchOffset = 0;
    for (const auto& note : noteClipboard)
    {
        requiredSteps = std::max(requiredSteps, static_cast<int>(std::ceil(note.step + note.length)));
        minPitchOffset = std::min(minPitchOffset, note.pitch);
        maxPitchOffset = std::max(maxPitchOffset, note.pitch);
    }

    const auto steps = session.editorStepCount();
    if (requiredSteps > Session::steps || minPitchOffset < -127 || maxPitchOffset > 127)
        return false;

    auto anchorStep = steps;
    for (int candidate = 0; candidate + requiredSteps <= steps; ++candidate)
        if (canPasteAt(candidate))
        {
            anchorStep = candidate;
            break;
        }
    const auto anchorPitch = juce::jlimit(-minPitchOffset, 127 - maxPitchOffset, clipboardBasePitch);

    session.beginNoteGesture("Paste notes");
    if (!session.ensurePatternLengthSteps(anchorStep + requiredSteps))
    {
        session.endNoteGesture();
        return false;
    }
    std::vector<juce::ValueTree> pasted;
    for (const auto& note : noteClipboard)
    {
        const auto step = anchorStep + note.step;
        const auto pitch = anchorPitch + note.pitch;
        juce::ValueTree state;
        if (session.addNote(step, pitch, note.length, &state, note.velocity).wasOk())
            pasted.push_back(state);
    }
    session.endNoteGesture();
    rebuildVisibleNotes();
    setSelectedStates(std::move(pasted));
    pasteAnchorIndex = indexForCell(std::min(session.editorStepCount() - 1, anchorStep + requiredSteps), anchorPitch);
    repaint();
    return true;
}

bool StepGrid::deleteSelection()
{
    const auto states = selectedStates();
    if (states.empty())
        return false;

    session.beginNoteGesture("Delete notes");
    session.removeNotes(states);
    session.endNoteGesture();
    clearSelection();
    return true;
}

bool StepGrid::beginSubdivision()
{
    const auto states = selectedStates();
    if (states.empty()) return false;
    auto start = std::numeric_limits<double>::max();
    auto end = 0.0;
    auto pitch = -1;
    for (const auto& state : states)
        if (const auto* note = noteForState(state))
        {
            if (pitch >= 0 && pitch != note->pitch) return false;
            pitch = note->pitch;
            start = std::min(start, note->start);
            end = std::max(end, note->start + note->length);
        }
    if (pitch < 0 || end <= start + 0.0001) return false;
    subdivisionCount = juce::jlimit(2, 32, juce::roundToInt(end - start));
    const auto row = lowestVisiblePitch + visiblePitchRows() - 1 - pitch;
    subdivisionSourceBounds = cell(static_cast<int>(std::floor(start)), row);
    subdivisionSourceBounds.translate(static_cast<float>(start - std::floor(start)) * cellWidth(), 0.0f);
    subdivisionSourceBounds.setWidth(static_cast<float>(end - start) * cellWidth());
    session.beginNoteGesture("Divide note");
    subdivisionActive = true;
    std::vector<juce::ValueTree> replacements;
    if (session.redistributeNotes(states, subdivisionCount, replacements).failed())
    {
        finishSubdivision();
        return false;
    }
    rebuildVisibleNotes();
    setSelectedStates(std::move(replacements));
    startTimerHz(30);
    repaint();
    return true;
}

bool StepGrid::adjustSubdivision(int delta)
{
    if (!subdivisionActive || delta == 0) return false;
    const auto next = juce::jlimit(2, 32, subdivisionCount + delta);
    if (next == subdivisionCount) return true;
    std::vector<juce::ValueTree> replacements;
    if (session.redistributeNotes(selectedNoteStates, next, replacements).failed()) return false;
    subdivisionCount = next;
    rebuildVisibleNotes();
    setSelectedStates(std::move(replacements));
    repaint();
    return true;
}

void StepGrid::finishSubdivision()
{
    if (!subdivisionActive) return;
    subdivisionActive = false;
    session.endNoteGesture();
    if (gesture != Gesture::move && !velocityAdjustActive) stopTimer();
    repaint();
}

int StepGrid::selectedVelocityPercent() const
{
    const auto states = selectedStates();
    if (states.empty()) return -2;
    auto midiVelocity = -1;
    for (const auto& state : states)
        if (const auto* note = noteForState(state))
        {
            if (midiVelocity < 0) midiVelocity = note->velocity;
            else if (midiVelocity != note->velocity) return -1;
        }
    return midiVelocity < 0 ? -2 : juce::roundToInt(midiVelocity * 100.0 / 127.0);
}

bool StepGrid::beginVelocityAdjustment()
{
    if (selectedStates().empty()) return false;
    if (!velocityAdjustActive)
    {
        session.beginNoteGesture("Adjust note velocity");
        velocityAdjustActive = true;
        startTimerHz(30);
        repaint(footerBounds().getSmallestIntegerContainer());
    }
    return true;
}

bool StepGrid::adjustVelocity(int delta)
{
    if (delta == 0 || (!velocityAdjustActive && !beginVelocityAdjustment())) return false;
    const auto changed = session.adjustNoteVelocities(selectedStates(), delta);
    repaint(footerBounds().getSmallestIntegerContainer());
    return changed || velocityAdjustActive;
}

void StepGrid::finishVelocityAdjustment()
{
    if (!velocityAdjustActive) return;
    velocityAdjustActive = false;
    session.endNoteGesture();
    if (gesture != Gesture::move && !subdivisionActive) stopTimer();
    repaint(footerBounds().getSmallestIntegerContainer());
}

bool StepGrid::fillSelectionToClipEnd()
{
    const auto states = selectedStates();
    if (states.empty())
        return false;

    session.beginNoteGesture("Fill note to clip end");
    auto changed = false;
    for (const auto& state : states)
        if (session.fillNoteToClipEnd(state).wasOk()) changed = true;
    session.endNoteGesture();
    if (changed)
        repaint();
    return changed;
}

}
