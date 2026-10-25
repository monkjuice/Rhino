#include "StepGridInternal.h"
#include "ui/shared/Playhead.h"
#include <algorithm>
#include <cmath>
#include <limits>

// Selection commands, clipboard, and the subdivision and velocity tools.
//
// Copy, cut and duplicate follow the arrangement's rule, in steps instead of
// seconds: they act on a region, which is either the span dragged out in the
// grid or the span of the selected notes, and duplicate pastes at the
// region's own end and carries the region along with it. A paste lands on the
// insert line and replaces what it covers. Where it parts from the
// arrangement is where that line is after a copy: at the end of what was
// copied, not at its start, so that copying notes and pasting puts a second
// copy beside them rather than writing the copy over its own source.

namespace rhino
{
namespace
{
juce::String countOfNotes(size_t count)
{
    return juce::String(static_cast<int>(count)) + (count == 1 ? " note" : " notes");
}
}

void StepGrid::toggleSelection(int index)
{
    if (index < 0 || index >= static_cast<int>(visibleNotes.size()))
    {
        repaint();
        return;
    }
    toggleSelectionOf(visibleNotes[static_cast<size_t>(index)].state);
}

// By state rather than by row, because a marker in the velocity lane can
// stand for a note the pitch window has scrolled past. setSelectedStates
// repaints what changed colour.
void StepGrid::toggleSelectionOf(const juce::ValueTree& state)
{
    auto current = selectedStates();
    if (std::find(current.begin(), current.end(), state) != current.end())
        std::erase(current, state);
    else
        current.push_back(state);
    setSelectedStates(std::move(current));
}

// Every note in the clip, not every note on screen. The pitch window shows
// what rows the panel has room for and a pattern is routinely taller than
// that, so selecting what happens to be visible would quietly select a
// fragment.
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

// Every note of the pitch in the clip, not only the ones on screen. Plain, they
// become the selection; Shift adds them, and Ctrl adds them or, when every one
// is selected already, takes them back out - the grid's own modifiers, which
// for Live's Shift-click on the piano ruler are Ctrl here.
void StepGrid::selectPitch(int pitch, const juce::ModifierKeys& mods)
{
    std::vector<juce::ValueTree> row;
    for (const auto& note : session.editorNotes())
        if (note.pitch == pitch)
            row.push_back(note.state);
    if (!isMultiSelectModifier(mods))
    {
        setSelectedStates(std::move(row));
        return;
    }
    auto current = selectedStates();
    const auto selected = [&current](const juce::ValueTree& state)
    {
        return std::find(current.begin(), current.end(), state) != current.end();
    };
    if (isToggleSelectionModifier(mods) && !row.empty() && std::all_of(row.begin(), row.end(), selected))
        std::erase_if(current, [&row](const juce::ValueTree& state)
        {
            return std::find(row.begin(), row.end(), state) != row.end();
        });
    else
        for (const auto& state : row)
            if (!selected(state))
                current.push_back(state);
    setSelectedStates(std::move(current));
}

// Session::moveNotes clamps the shift so no note leaves 0-127 and refuses a
// lane that is already taken, so both edges are its business rather than ours.
bool StepGrid::transposeSelection(int amount)
{
    const auto states = selectedStates();
    if (states.empty() || amount == 0)
        return false;
    session.beginNoteGesture(std::abs(amount) == 12 ? "Transpose octave" : "Transpose notes");
    juce::ignoreUnused(session.moveNotes(states, 0.0, amount, shownPitches));
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
}

// A click: the region collapses onto the step, and the insert line goes there.
void StepGrid::setStepInsertPoint(double step)
{
    setStepSelection(step, step);
    setInsertStep(step);
}

void StepGrid::clearStepSelection()
{
    stepSelection = {};
}

void StepGrid::setInsertStep(double step)
{
    if (step >= 0.0)
        step = std::clamp(step, 0.0, static_cast<double>(session.editorStepCount()));
    if (step == insertStep)
        return;
    // Both columns the line was and is in, in the width paintInsertLine fills.
    if (getHeight() > 0)
        for (const auto at : {insertStep, step})
            if (at >= 0.0)
                repaint(juce::Rectangle<float>(xForStep(at) - 2.0f, headerHeight, 6.0f, timeAreaBottom() - headerHeight)
                            .getSmallestIntegerContainer());
    insertStep = step;
}

double StepGrid::pasteStep() const
{
    if (insertStep >= 0.0)
        return insertStep;
    if (!selectedStates().empty())
        if (const auto region = effectiveStepRegion(); region.active)
            return region.end;
    return 0.0;
}

void StepGrid::report(const juce::String& message) const
{
    if (status)
        status(message);
}

// The arrangement's selection, carried into the steps of the open clip.
void StepGrid::showTimeSpan(double startSeconds, double endSeconds)
{
    if (!session.hasPatternClip())
        return;
    // The clip may have changed under an editor the shell is hiding, and its
    // length decides the zoom's range: caught up first, or the catch-up when
    // the editor is shown would put back the zoom a new length opens at. The
    // line set below is in this clip, so the catch-up must not clear it as a
    // line left over from the last one.
    if (session.editorStepCount() != visibleStepCount)
        rebuildVisibleNotes();
    shownClip = session.pattern().itemID;
    const auto clipStartBeat = session.secondsToBeats(session.pattern().getPosition().time.getStart().inSeconds());
    const auto stepAt = [this, clipStartBeat](double seconds)
    {
        return (session.secondsToBeats(seconds) - clipStartBeat) / 4.0 * session.editorStepResolution();
    };
    // Held to the clip itself, which can end short of the grid's last step.
    const auto steps = static_cast<double>(session.editorStepCount());
    const auto clipEnd = std::min(steps, session.patternLengthBeats() / 4.0 * session.editorStepResolution());
    const auto first = std::clamp(stepAt(startSeconds), 0.0, clipEnd);
    const auto last = std::clamp(stepAt(endSeconds), 0.0, clipEnd);
    if (last <= first)
        return;
    // Exactly the span when the zoom reaches it, and centred when it is
    // narrower than the deepest view. A long clip opens deeper than the
    // zoom's own limit, and may be framed as deep.
    stepZoom = std::clamp(steps / (last - first), 1.0, std::max(8.0, steps / Session::defaultSteps));
    stepScroll = first - (visibleStepSpan() - (last - first)) * 0.5;
    syncHorizontalScroll();
    // Last, because a rebuild reads the region off the notes again.
    std::vector<juce::ValueTree> caught;
    for (const auto& note : session.editorNotes())
        if (note.startSteps < last && note.startSteps + note.lengthSteps > first)
            caught.push_back(note.state);
    setSelectedStates(std::move(caught));
    setStepSelection(first, last);
    setInsertStep(first);
    updatePlayhead();
    repaint();
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
    // actually take. Read off the lane's notes, which are every note in the
    // clip, because a note the velocity lane selected may be scrolled out of
    // the pitch window.
    for (const auto& state : selectedStates())
        if (const auto* note = laneNoteFor(state))
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
    const auto noteCount = session.hasPatternClip()
        ? static_cast<size_t>(session.pattern().getSequence().getNumNotes()) : size_t {0};
    if (noteCount > 0 && selectedStates().size() == noteCount)
    {
        setStepSelection(0.0, static_cast<double>(session.editorStepCount()));
        return;
    }
    clearStepSelection();
    const auto region = effectiveStepRegion();
    if (region.active)
        setStepSelection(region.start, region.end);
}

// Only ever the insert line. The grid already has two ways of showing a
// selection - the marquee while it is being dragged, and the notes it caught
// afterwards - and a band behind either of them reads as a third. What
// neither of them says is where a paste would land, which is the one mark
// worth keeping.
void StepGrid::paintInsertLine(juce::Graphics& g)
{
    if (insertStep < 0.0)
        return;
    const auto left = labelWidth + static_cast<float>(insertStep - stepScroll) * cellWidth();
    const auto top = headerHeight;
    // Through the velocity lane too: a paste lands on every note's marker.
    const auto bottom = timeAreaBottom();
    juce::Graphics::ScopedSaveState scope(g);
    g.reduceClipRegion(juce::Rectangle<float>(labelWidth, 0.0f, gridWidth(), bottom).getSmallestIntegerContainer());
    g.setColour(juce::Colour(0xffc6d58c));
    // Left edge on the step, the same two pixels the playhead fills, so that a
    // parked playhead sits exactly on the line it was parked by.
    g.fillRect(left, top, 2.0f, bottom - top);
}

// What is copied is the region, not the notes' own bounding box: a rest at
// either end of the selection has to survive the round trip, or pasting would
// shorten the phrase.
bool StepGrid::captureRegion(const StepSelection& region, NoteClipboard& into) const
{
    const auto states = selectedStates();
    if (states.empty() || !region.isRange())
        return false;

    // Every selected note, scrolled into the pitch window or not; delete takes
    // them all, so a cut that copied only the visible ones would lose the rest.
    auto minPitch = 128;
    for (const auto& state : states)
        if (const auto* note = laneNoteFor(state))
            minPitch = std::min(minPitch, note->pitch);
    if (minPitch > 127)
        return false;

    NoteClipboard taken;
    taken.basePitch = minPitch;
    taken.spanSteps = region.length();
    for (const auto& state : states)
        if (const auto* note = laneNoteFor(state))
            taken.notes.push_back({note->start - region.start, note->pitch - minPitch,
                                   std::max(0.001, note->length), note->velocity});
    if (taken.notes.empty())
        return false;
    into = std::move(taken);
    return true;
}

bool StepGrid::copySelection()
{
    const auto region = effectiveStepRegion();
    if (!captureRegion(region, noteClipboard))
    {
        report("Select notes to copy");
        return false;
    }
    // The next paste goes beside what was copied. Pasted at its start, it
    // replaced its own source with itself and showed nothing, and a copy
    // dragged away from there left a hole where the source had been.
    setInsertStep(region.end);
    report("Copied " + countOfNotes(noteClipboard.notes.size()));
    return true;
}

bool StepGrid::cutSelection()
{
    const auto region = effectiveStepRegion();
    NoteClipboard taken;
    if (!captureRegion(region, taken))
    {
        report("Select notes to cut");
        return false;
    }
    noteClipboard = std::move(taken);
    deleteSelection();
    setStepInsertPoint(region.start);
    repaint();
    report("Cut " + countOfNotes(noteClipboard.notes.size()));
    return true;
}

bool StepGrid::pasteSelection()
{
    if (noteClipboard.notes.empty())
    {
        report("Copy or cut notes first");
        return false;
    }
    if (!pasteNotes(noteClipboard, pasteStep(), "Paste notes"))
        return false;
    report("Pasted " + countOfNotes(noteClipboard.notes.size()));
    return true;
}

bool StepGrid::pasteNotes(const NoteClipboard& clipboard, double atStep, const juce::String& actionName)
{
    auto minPitchOffset = 0;
    auto maxPitchOffset = 0;
    auto contentEnd = 0.0;
    for (const auto& note : clipboard.notes)
    {
        minPitchOffset = std::min(minPitchOffset, note.pitch);
        maxPitchOffset = std::max(maxPitchOffset, note.pitch);
        contentEnd = std::max(contentEnd, note.step + note.length);
    }
    if (clipboard.notes.empty() || minPitchOffset < -127 || maxPitchOffset > 127)
        return false;

    const auto span = std::max(clipboard.spanSteps, contentEnd);
    const auto anchorStep = std::max(0.0, atStep);
    const auto requiredSteps = static_cast<int>(std::ceil(anchorStep + span - 0.0001));
    if (requiredSteps > Session::steps)
    {
        report("No room: a clip holds at most " + juce::String(Session::steps) + " steps in the note editor");
        return false;
    }
    const auto anchorPitch = juce::jlimit(-minPitchOffset, 127 - maxPitchOffset, clipboard.basePitch);

    session.beginNoteGesture(actionName);
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
    for (const auto& incoming : clipboard.notes)
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
    for (const auto& note : clipboard.notes)
    {
        juce::ValueTree state;
        if (session.addNote(anchorStep + note.step, anchorPitch + note.pitch, note.length, &state, note.velocity).wasOk())
            pasted.push_back(state);
    }
    session.endNoteGesture();
    rebuildVisibleNotes();
    setSelectedStates(std::move(pasted));
    // The region moves onto what was just pasted, so Ctrl+D carries on from
    // there, and the line moves past it, so a second paste lies beside the
    // first rather than writing over it.
    setStepSelection(anchorStep, anchorStep + span);
    setInsertStep(anchorStep + span);
    repaint();
    return true;
}

// Live's rule: the copy lands flush against the end of the selection and the
// selection moves onto it, so holding Ctrl+D turns one bar into four. The
// clipboard is left alone, as the arrangement leaves its own: a Ctrl+D used
// to replace whatever had been copied before it.
bool StepGrid::duplicateSelection()
{
    const auto region = effectiveStepRegion();
    NoteClipboard copy;
    if (!captureRegion(region, copy))
    {
        report("Select notes to duplicate");
        return false;
    }
    if (!pasteNotes(copy, region.end, "Duplicate notes"))
        return false;
    report("Duplicated " + countOfNotes(copy.notes.size()));
    return true;
}

// Session::moveNotes holds the group inside the clip and stops it against a
// note that is staying put, so the edges are its business, as they are for a
// transpose; the key is consumed either way.
bool StepGrid::nudgeSelection(double steps)
{
    const auto states = selectedStates();
    if (states.empty())
        return false;
    session.beginNoteGesture("Nudge notes");
    juce::ignoreUnused(session.moveNotes(states, steps, 0));
    session.endNoteGesture();
    return true;
}

// The menu's chop, the parts chosen outright: on the grid, or so many a note.
bool StepGrid::chopSelection(int parts)
{
    std::vector<juce::ValueTree> originals;
    auto chops = chopsOfSelection(originals);
    if (chops.empty())
    {
        report("Select notes to chop");
        return false;
    }
    for (auto& chop : chops)
        chop.parts = parts;
    std::vector<juce::ValueTree> pieces;
    session.beginNoteGesture("Chop notes");
    const auto result = session.chopNotes(originals, chops, pieces);
    session.endNoteGesture();
    if (result.failed())
    {
        report(result.getErrorMessage());
        return false;
    }
    rebuildVisibleNotes();
    setSelectedStates(std::move(pieces));
    return true;
}

// The selected notes as a chop takes them: where the editor shows each, with
// its pitch, velocity and colour, beside the states it replaces.
std::vector<Session::NoteChop> StepGrid::chopsOfSelection(std::vector<juce::ValueTree>& originals) const
{
    std::vector<Session::NoteChop> chops;
    const auto all = session.editorNotes();
    for (const auto& state : selectedStates())
        for (const auto& note : all)
            if (note.state == state)
            {
                chops.push_back({note.startSteps, note.lengthSteps, note.pitch, note.velocity, note.colour, 0});
                originals.push_back(state);
                break;
            }
    return chops;
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

// Ctrl+E, Live's Chop Note(s) on Grid: every selected note, on any pitch, cut
// at the step lines inside it, as one undo step that stays open while Ctrl is
// held. Until 2026-10-25 it spread one pitch's span into equal retriggers, and
// Ctrl with the wheel opened it too, which chopped the selection for anyone
// reaching for the zoom; the wheel no longer touches it.
bool StepGrid::beginSubdivision()
{
    std::vector<juce::ValueTree> originals;
    chopSources = chopsOfSelection(originals);
    if (chopSources.empty())
        return false;
    // The badge stands over the first note.
    const auto& first = chopSources.front();
    const auto row = rowOfPitch(first.pitch);
    subdivisionSourceBounds = {xForStep(first.startSteps), rowsTop() + static_cast<float>(row) * rowHeight(),
                               static_cast<float>(first.lengthSteps) * cellWidth(), rowHeight()};
    session.beginNoteGesture("Chop notes");
    subdivisionActive = true;
    chopDelta = 0;
    std::vector<juce::ValueTree> pieces;
    if (session.chopNotes(originals, chopSources, pieces).failed())
    {
        finishSubdivision();
        return false;
    }
    chopPieces = pieces;
    subdivisionCount = Session::gridChopParts(first.startSteps, first.lengthSteps);
    rebuildVisibleNotes();
    setSelectedStates(std::move(pieces));
    startTimerHz(30);
    // The pointer turns up-and-down: a drag is what sets the parts now.
    if (isMouseOver(true))
        updatePointer(getMouseXYRelative().toFloat(), juce::ModifierKeys::getCurrentModifiers());
    repaint();
    return true;
}

// Every note gains or loses parts together, none going below one or past 32,
// chopped afresh from the notes as they were. At a count of the grid's own the
// cut is at the step lines; any other count cuts each note into equal parts.
// A change no note can follow is not taken, so the way back down is never
// longer than the way up.
bool StepGrid::adjustSubdivision(int delta)
{
    if (!subdivisionActive || delta == 0) return false;
    const auto partsAt = [](const Session::NoteChop& chop, int change)
    {
        return change == 0 ? 0 : std::clamp(Session::gridChopParts(chop.startSteps, chop.lengthSteps) + change, 1, 32);
    };
    const auto countOf = [](const Session::NoteChop& chop, int parts)
    {
        return parts == 0 ? Session::gridChopParts(chop.startSteps, chop.lengthSteps) : parts;
    };
    const auto next = chopDelta + delta;
    auto chops = chopSources;
    auto changes = false;
    for (auto& chop : chops)
    {
        chop.parts = partsAt(chop, next);
        changes = changes || countOf(chop, chop.parts) != countOf(chop, partsAt(chop, chopDelta));
    }
    if (!changes)
        return true;
    std::vector<juce::ValueTree> pieces;
    if (session.chopNotes(chopPieces, chops, pieces).failed())
        return false;
    chopDelta = next;
    chopPieces = pieces;
    subdivisionCount = countOf(chops.front(), chops.front().parts);
    rebuildVisibleNotes();
    setSelectedStates(std::move(pieces));
    repaint();
    return true;
}

// Ctrl still down after Ctrl+E, a press and a drag up adds a part to every
// chopped note and a drag down takes one away, a part every chopPixelsPerPart,
// as Live chops by dragging. The press sets where the drag counts from; the
// mouse moving with no button down changes nothing.
void StepGrid::chopByMouse(float y)
{
    if (!subdivisionActive || chopMouseY < 0.0f)
        return;
    const auto parts = static_cast<int>((chopMouseY - y) / chopPixelsPerPart);
    if (parts == 0)
        return;
    chopMouseY -= static_cast<float>(parts) * chopPixelsPerPart;
    adjustSubdivision(parts);
}

void StepGrid::finishSubdivision()
{
    if (!subdivisionActive) return;
    subdivisionActive = false;
    chopSources.clear();
    chopPieces.clear();
    session.endNoteGesture();
    if (isMouseOver(true))
        updatePointer(getMouseXYRelative().toFloat(), juce::ModifierKeys::getCurrentModifiers());
    // Only with nothing else on the clock: a stroke or a marquee carries on
    // autoscrolling after a modal tool ends.
    if (gesture == Gesture::none && !velocityAdjustActive) stopTimer();
    repaint();
}

// Read off the lane's notes rather than the visible ones, so a selected note
// the pitch window has scrolled past still counts.
int StepGrid::selectedVelocity() const
{
    const auto states = selectedStates();
    if (states.empty()) return -2;
    auto velocity = -1;
    for (const auto& state : states)
        if (const auto* note = laneNoteFor(state))
        {
            if (velocity < 0) velocity = note->velocity;
            else if (velocity != note->velocity) return -1;
        }
    return velocity < 0 ? -2 : velocity;
}

// Refused while the pointer holds a gesture, so two tools never share one
// undo transaction.
bool StepGrid::beginVelocityAdjustment()
{
    if (gesture != Gesture::none || selectedStates().empty()) return false;
    if (!velocityAdjustActive)
    {
        session.beginNoteVelocityGesture("Adjust note velocity");
        velocityAdjustActive = true;
        startTimerHz(30);
        repaint(footerBounds().getSmallestIntegerContainer());
    }
    return true;
}

// One MIDI step per key press or wheel notch, so every value is reachable.
bool StepGrid::adjustVelocity(int delta)
{
    if (delta == 0 || (!velocityAdjustActive && !beginVelocityAdjustment())) return false;
    std::vector<Session::NoteVelocity> targets;
    for (const auto& state : selectedStates())
        if (const auto* note = laneNoteFor(state))
            targets.push_back({state, velocitylane::clampVelocity(note->velocity + delta)});
    const auto changed = applyNoteVelocities(targets);
    return changed || velocityAdjustActive;
}

void StepGrid::finishVelocityAdjustment()
{
    if (!velocityAdjustActive) return;
    velocityAdjustActive = false;
    session.endNoteVelocityGesture();
    if (gesture == Gesture::none && !subdivisionActive) stopTimer();
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
