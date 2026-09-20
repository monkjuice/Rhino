#include "StepGridInternal.h"
#include "Playhead.h"
#include <algorithm>
#include <cmath>
#include <limits>

// Selection commands, clipboard, and the subdivision and velocity tools.
//
// Copy, cut, paste and duplicate follow the arrangement's rule, in steps
// instead of seconds: everything acts on a region, the region is either the
// span dragged out in the grid or the span of the selected notes, paste lands
// at the insert point and replaces what it covers, and duplicate pastes at the
// region's own end and carries the region along with it.

namespace rhino
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

void StepGrid::setStepSelection(double start, double end)
{
    const auto limit = static_cast<double>(session.editorStepCount());
    stepSelection.start = std::clamp(std::min(start, end), 0.0, limit);
    stepSelection.end = std::clamp(std::max(start, end), 0.0, limit);
    stepSelection.active = true;
    // Set here and re-set by setStepRegionFromSelection, so only a region
    // actually read off the notes is marked as theirs.
    regionFromNotes = false;
}

void StepGrid::setStepInsertPoint(double step)
{
    setStepSelection(step, step);
}

void StepGrid::clearStepSelection()
{
    stepSelection = {};
}

StepGrid::StepSelection StepGrid::effectiveStepRegion() const
{
    if (stepSelection.active && stepSelection.isRange())
        return stepSelection;
    // Rounded out to whole steps: a sixteenth note occupies the step it sits
    // in, so duplicating it puts the copy in the next step rather than a
    // sixteenth of a step later.
    StepSelection fromNotes;
    // selectedStates rather than the raw list: a selected cell can stand for
    // every retrigger inside it, and the region has to cover what copy would
    // actually take.
    for (const auto& state : selectedStates())
        if (const auto* note = noteForState(state))
        {
            const auto start = std::floor(note->start);
            const auto end = std::ceil(note->start + note->length);
            if (!fromNotes.active)
            {
                fromNotes = {start, end, true};
                continue;
            }
            fromNotes.start = std::min(fromNotes.start, start);
            fromNotes.end = std::max(fromNotes.end, end);
        }
    return fromNotes.active ? fromNotes : stepSelection;
}

// The region follows whatever the selection has become, so the marquee, a
// click, Ctrl+A and a paste all leave it saying the same thing.
//
// Selecting every note is the one case not read off the notes: after Ctrl+A
// the region is the whole clip, because a bar whose last note stops at step
// twelve is still a bar, and duplicating it has to land on step sixteen.
void StepGrid::setStepRegionFromSelection()
{
    // Counted rather than listed: this runs on every selection change, a note
    // drag included, and editorNotes builds a vector each time it is asked.
    const auto noteCount = static_cast<size_t>(session.pattern().getSequence().getNumNotes());
    if (noteCount > 0 && selectedStates().size() == noteCount)
    {
        setStepSelection(0.0, static_cast<double>(session.editorStepCount()));
        regionFromNotes = true;
        return;
    }
    clearStepSelection();
    const auto region = effectiveStepRegion();
    if (region.active)
        setStepSelection(region.start, region.end);
    regionFromNotes = true;
}

void StepGrid::paintStepSelection(juce::Graphics& g)
{
    // Nothing to draw over selected notes: they are already drawn as selected,
    // and a band over every pitch row would read as a marquee mid-drag.
    if (!stepSelection.active || regionFromNotes)
        return;
    const auto left = labelWidth + static_cast<float>(stepSelection.start - stepScroll) * cellWidth();
    const auto right = labelWidth + static_cast<float>(stepSelection.end - stepScroll) * cellWidth();
    const auto top = headerHeight;
    const auto bottom = headerHeight + rowAreaHeight();
    juce::Graphics::ScopedSaveState scope(g);
    g.reduceClipRegion(juce::Rectangle<float>(labelWidth, 0.0f, gridWidth(), bottom).getSmallestIntegerContainer());
    if (stepSelection.isRange())
    {
        // A tab on the ruler and a wash under the lanes, with no outline: the
        // span has to be legible without competing with the notes inside it.
        g.setColour(juce::Colour(0x18c6d58c));
        g.fillRect(juce::Rectangle<float>(left, top, right - left, bottom - top));
        g.setColour(juce::Colour(0xffc6d58c));
        g.fillRect(left, headerHeight - 3.0f, right - left, 3.0f);
    }
    // The insert point: a region with no width is still where a paste lands.
    g.setColour(juce::Colour(0xffc6d58c));
    g.fillRect(left - 1.0f, top, 2.0f, bottom - top);
    g.fillRect(left - 4.0f, top, 9.0f, 3.0f);
    g.fillRect(left - 4.0f, bottom - 3.0f, 9.0f, 3.0f);
}

// What is copied is the region, not the notes' own bounding box: a rest at
// either end of the selection has to survive the round trip, or pasting would
// shorten the phrase.
bool StepGrid::copySelection()
{
    const auto states = selectedStates();
    const auto region = effectiveStepRegion();
    if (states.empty() || !region.isRange())
        return false;

    auto minPitch = 128;
    for (const auto& state : states)
        if (const auto* note = noteForState(state))
            minPitch = std::min(minPitch, note->pitch);
    if (minPitch > 127)
        return false;

    noteClipboard.clear();
    clipboardBasePitch = minPitch;
    clipboardSpanSteps = region.length();
    for (const auto& state : states)
        if (const auto* note = noteForState(state))
            noteClipboard.push_back({note->start - region.start, note->pitch - minPitch,
                                     std::max(0.001, note->length), note->velocity});
    return !noteClipboard.empty();
}

bool StepGrid::cutSelection()
{
    const auto region = effectiveStepRegion();
    if (!copySelection())
        return false;
    deleteSelection();
    setStepInsertPoint(region.start);
    repaint();
    return true;
}

bool StepGrid::pasteSelection()
{
    if (noteClipboard.empty())
        return false;

    auto minPitchOffset = 0;
    auto maxPitchOffset = 0;
    auto contentEnd = 0.0;
    for (const auto& note : noteClipboard)
    {
        minPitchOffset = std::min(minPitchOffset, note.pitch);
        maxPitchOffset = std::max(maxPitchOffset, note.pitch);
        contentEnd = std::max(contentEnd, note.step + note.length);
    }
    if (minPitchOffset < -127 || maxPitchOffset > 127)
        return false;

    const auto span = std::max(clipboardSpanSteps, contentEnd);
    const auto region = effectiveStepRegion();
    const auto anchorStep = std::max(0.0, stepSelection.active ? stepSelection.start
                                                               : region.active ? region.start : 0.0);
    const auto requiredSteps = static_cast<int>(std::ceil(anchorStep + span - 0.0001));
    if (requiredSteps > Session::steps)
        return false;
    const auto anchorPitch = juce::jlimit(-minPitchOffset, 127 - maxPitchOffset, clipboardBasePitch);

    session.beginNoteGesture("Paste notes");
    if (!session.ensurePatternLengthSteps(requiredSteps))
    {
        session.endNoteGesture();
        return false;
    }
    // Pasting replaces what it lands on, as it does in Live, so a paste is
    // never refused and never slides off to somewhere that was not asked for.
    // Only the lanes the paste writes to are cleared, and only where it writes.
    constexpr double tolerance = 0.0001;
    std::vector<juce::ValueTree> displaced;
    const auto existing = session.editorNotes();
    for (const auto& incoming : noteClipboard)
    {
        const auto step = anchorStep + incoming.step;
        const auto pitch = anchorPitch + incoming.pitch;
        for (const auto& note : existing)
            if (note.pitch == pitch
                && step < note.startSteps + note.lengthSteps - tolerance
                && note.startSteps < step + incoming.length - tolerance
                && std::find(displaced.begin(), displaced.end(), note.state) == displaced.end())
                displaced.push_back(note.state);
    }
    if (!displaced.empty())
        session.removeNotes(displaced);

    std::vector<juce::ValueTree> pasted;
    for (const auto& note : noteClipboard)
    {
        juce::ValueTree state;
        if (session.addNote(anchorStep + note.step, anchorPitch + note.pitch, note.length, &state, note.velocity).wasOk())
            pasted.push_back(state);
    }
    session.endNoteGesture();
    rebuildVisibleNotes();
    setSelectedStates(std::move(pasted));
    // The region moves onto what was just pasted, so a second paste replaces
    // it rather than stacking on it, and Ctrl+D carries on from there.
    setStepSelection(anchorStep, anchorStep + span);
    repaint();
    return true;
}

// Live's rule: the copy lands flush against the end of the selection and the
// selection moves onto it, so holding Ctrl+D turns one bar into four.
bool StepGrid::duplicateSelection()
{
    const auto region = effectiveStepRegion();
    if (!region.isRange() || !copySelection())
        return false;
    setStepInsertPoint(region.end);
    return pasteSelection();
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
