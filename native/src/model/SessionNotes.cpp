#include "SessionInternal.h"
#include <algorithm>
#include <limits>
#include <set>
#include <utility>

// Note editing and pattern geometry. Serves StepGrid.
//
// Every edit here reads the clip the note editor has open, and a document with
// no MIDI track has none, so each one asks first and refuses rather than
// reaching for a clip that is not there.

namespace rhino
{
namespace
{
juce::Result noOpenClip() { return juce::Result::fail("Open a MIDI clip to edit its notes."); }

// Where the editor shows a note to start, in its own steps. A clip whose window
// opens part-way through a note - cut, trimmed or pasted a hair after the note
// began - still plays it: Tracktion moves the note-on of a note that began
// before the clip to the clip's start (createDefaultPlaybackMidiSequence). So
// that note is shown, sized and divided from step 0, where it sounds. A note
// wholly before the window plays nothing and keeps its own start, off the grid.
double shownStartStep(double startStep, double lengthSteps)
{
    return startStep < 0.0 && startStep + lengthSteps > 0.0 ? 0.0 : startStep;
}
}

bool Session::hasNote(int step, int pitch) const
{
    if (patternClip == nullptr)
        return false;
    const auto gridSteps = editorStepCount();
    if (step < 0 || step >= gridSteps || pitch < 0 || pitch > 127)
        return false;
    const auto beat = beatForStep(step);
    for (auto* note : pattern().getSequence().getNotes())
        if (note->getNoteNumber() == pitch
            && std::abs(note->getStartBeat().inBeats() - beat) < 0.0001)
            return true;
    return false;
}

std::vector<Session::EditorNote> Session::editorNotes() const
{
    std::vector<EditorNote> result;
    if (patternClip == nullptr)
        return result;
    const auto stepBeats = stepDurationBeats(editorStepResolution());
    result.reserve(static_cast<size_t>(pattern().getSequence().getNumNotes()));
    for (auto* note : pattern().getSequence().getNotes())
    {
        const auto start = stepForBeat(note->getStartBeat().inBeats());
        const auto length = note->getLengthBeats().inBeats() / stepBeats;
        const auto shown = shownStartStep(start, length);
        result.push_back({note->state, shown, length - (shown - start),
                          note->getNoteNumber(), note->getVelocity(), note->getColour()});
    }
    return result;
}

// The track the note editor's clip sits on, or -1: a note edit leaves only
// the decks playing that track behind their bounce.
int Session::patternTrackIndex() const
{
    if (patternClip == nullptr) return -1;
    auto* track = trackHoldingClip(*edit, *patternClip);
    return track != nullptr ? audioTracks().indexOf(track) : -1;
}

juce::Result Session::addNote(double startSteps, int pitch, double lengthSteps,
                              juce::ValueTree* addedState, int velocity)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (patternClip == nullptr)
        return noOpenClip();
    const auto gridSteps = editorStepCount();
    if (startSteps < 0.0 || startSteps >= gridSteps || pitch < 0 || pitch > 127)
        return juce::Result::fail("Add notes inside the visible grid.");
    lengthSteps = std::clamp(lengthSteps, 0.001, static_cast<double>(gridSteps) - startSteps);
    constexpr double tolerance = 0.0001;
    for (const auto& existing : editorNotes())
        if (existing.pitch == pitch
            && startSteps < existing.startSteps + existing.lengthSteps - tolerance
            && existing.startSteps < startSteps + lengthSteps - tolerance)
            return juce::Result::fail("Notes on the same lane cannot overlap.");

    auto& sequence = pattern().getSequence();
    const auto stepBeats = stepDurationBeats(editorStepResolution());
    auto* added = sequence.addNote(pitch, tracktion::core::BeatPosition::fromBeats(beatForStep(startSteps)),
                                   tracktion::core::BeatDuration::fromBeats(lengthSteps * stepBeats),
                                   juce::jlimit(0, 127, velocity), 0, &edit->getUndoManager());
    if (added == nullptr)
        return juce::Result::fail("The note could not be added.");
    if (addedState != nullptr)
        *addedState = added->state;
    pattern().state.removeProperty(starterPlaceholderID, &edit->getUndoManager());
    markModified(patternTrackIndex(), ModelChange::notes);
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

bool Session::removeNotes(const std::vector<juce::ValueTree>& states)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (patternClip == nullptr)
        return false;
    auto& sequence = pattern().getSequence();
    auto& transport = edit->getTransport();
    const auto playing = transport.isPlaying();
    // Where the clip is in its sequence now, its loop wrapped.
    const auto playheadStep = playing ? patternStepAt(transport.getPosition().inSeconds()) : -1.0;
    const auto playheadBeat = beatForStep(playheadStep);
    auto removedActiveNote = false;
    auto changed = false;
    for (const auto& state : states)
        if (auto* note = sequence.getNoteFor(state))
        {
            removedActiveNote = removedActiveNote || (playheadStep >= 0.0
                && playheadBeat >= note->getStartBeat().inBeats()
                && playheadBeat < note->getEndBeat().inBeats());
            sequence.removeNote(*note, &edit->getUndoManager());
            changed = true;
        }
    if (changed)
    {
        markModified(patternTrackIndex(), ModelChange::notes);
        if (playing)
        {
            // Rebuild so the next pass uses the edited sequence. A MIDI panic
            // is only needed when the removed note was already sounding;
            // panicking for every edit audibly cuts unrelated notes and FX.
            if (removedActiveNote)
                panicMidiOnTrack(trackHoldingClip(*edit, pattern()));
            edit->restartPlayback();
        }
        sendSynchronousChangeMessage();
    }
    return changed;
}

// Absolute values rather than a step up or down: a group dragged into a rail
// has to come back to its own spacing, which only the values it started from
// can give, and a stroke in the lane sets each note to something different.
//
// No restartPlayback here, unlike removeNotes: the engine's Edit::TreeWatcher
// already restarts playback, deferred and coalesced, when any NOTE property
// other than its colour changes, and a velocity moves no note in time, so
// nothing sounding needs cutting off.
bool Session::setNoteVelocities(const std::vector<NoteVelocity>& values)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (patternClip == nullptr || values.empty())
        return false;
    auto& sequence = pattern().getSequence();
    auto* undoManager = &edit->getUndoManager();
    auto changed = false;
    for (const auto& value : values)
        if (auto* note = sequence.getNoteFor(value.state))
            if (const auto clamped = juce::jlimit(1, 127, value.velocity); clamped != note->getVelocity())
            {
                note->setVelocity(clamped, undoManager);
                changed = true;
            }
    if (!changed)
        return false;
    // Marked at every step, so a deck playing the track knows its cell changed;
    // only the announcement waits for a gesture's end.
    markModified(patternTrackIndex(), ModelChange::notes);
    if (noteVelocityGestureDepth > 0)
    {
        noteVelocitiesChangedInGesture = true;
        return true;
    }
    sendSynchronousChangeMessage();
    return true;
}

void Session::beginNoteGesture(juce::String actionName) { edit->getUndoManager().beginNewTransaction(actionName); }
void Session::endNoteGesture() { edit->getUndoManager().beginNewTransaction(); }

void Session::beginNoteVelocityGesture(const juce::String& actionName)
{
    if (noteVelocityGestureDepth++ == 0)
    {
        noteVelocitiesChangedInGesture = false;
        edit->getUndoManager().beginNewTransaction(actionName);
    }
}

void Session::endNoteVelocityGesture()
{
    if (noteVelocityGestureDepth <= 0)
        return;
    if (--noteVelocityGestureDepth > 0)
        return;
    edit->getUndoManager().beginNewTransaction();
    if (std::exchange(noteVelocitiesChangedInGesture, false))
        sendSynchronousChangeMessage();
}

int Session::editorStepCount() const
{
    const auto resolution = editorStepResolution();
    return juce::jlimit(defaultSteps, steps, static_cast<int>(std::ceil(patternLengthBeats() * resolution / 4.0)));
}

int Session::editorStepResolution() const
{
    if (patternClip == nullptr)
        return defaultSteps;
    return juce::jlimit(defaultSteps, 64,
                        static_cast<int>(patternClip->state.getProperty(editorStepsID, defaultSteps)));
}

double Session::patternLengthBeats() const
{
    if (patternClip == nullptr)
        return beatsPerBar();
    // A looped clip repeats its loop for as long as it runs: the loop is the
    // pattern, however far the clip has been dragged out.
    if (patternClip->isLooping())
        return std::max(0.0001, patternClip->getLoopLengthBeats().inBeats());
    const auto position = patternClip->getPosition();
    const auto startBeat = edit->tempoSequence.toBeats(position.time.getStart()).inBeats();
    const auto endBeat = edit->tempoSequence.toBeats(position.time.getEnd()).inBeats();
    return std::max(0.0001, endBeat - startBeat);
}

// A clip offset is how far into its own source the clip starts, and a MIDI
// clip source is its whole sequence: splitting a clip, or copying a region
// that begins part-way through one, leaves every note where it was and moves
// this instead. Step 0 of the grid is therefore the offset, not beat zero.
double Session::clipOffsetBeats(const te::Clip& clip) const
{
    return clip.getPosition().offset.inSeconds() * tempo() / 60.0;
}

// On a looped clip the offset counts into the loop rather than into the
// sequence, and the editor shows the loop from its start.
double Session::patternOffsetBeats() const
{
    if (patternClip == nullptr)
        return 0.0;
    return patternClip->isLooping() ? patternClip->getLoopStartBeats().inBeats() : clipOffsetBeats(*patternClip);
}

double Session::beatForStep(double step) const
{
    return patternOffsetBeats() + step * stepDurationBeats(editorStepResolution());
}

double Session::stepForBeat(double beat) const
{
    return (beat - patternOffsetBeats()) / stepDurationBeats(editorStepResolution());
}

void Session::setEditorStepCount(int newSteps)
{
    const auto clamped = juce::jlimit(defaultSteps, 64, newSteps);
    if (patternClip == nullptr || editorStepResolution() == clamped)
        return;
    patternClip->state.setProperty(editorStepsID, clamped, &edit->getUndoManager());
    markModified(patternTrackIndex(), ModelChange::notes);
    sendSynchronousChangeMessage();
}

void Session::setNote(int step, int pitch, bool enabled)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (patternClip == nullptr)
        return;
    const auto gridSteps = editorStepCount();
    const auto stepBeats = stepDurationBeats(editorStepResolution());
    if (step < 0 || step >= gridSteps || pitch < 0 || pitch > 127)
        return;
    const auto beat = beatForStep(step);
    auto& sequence = pattern().getSequence();
    auto* undoManager = &edit->getUndoManager();
    const auto restartLivePlayback = [this]
    {
        if (!edit->getTransport().isPlaying())
            return;
        panicMidiOnTrack(trackHoldingClip(*edit, pattern()));
        edit->restartPlayback();
    };
    for (auto* note : sequence.getNotes())
        if (note->getNoteNumber() == pitch
            && std::abs(note->getStartBeat().inBeats() - beat) < 0.0001)
        {
            if (!enabled)
            {
                sequence.removeNote(*note, undoManager);
                markModified(patternTrackIndex(), ModelChange::notes);
                restartLivePlayback();
            }
            sendSynchronousChangeMessage();
            return;
        }
    if (enabled)
    {
        for (auto* note : sequence.getNotes())
        {
            if (note->getNoteNumber() != pitch)
                continue;

            const auto noteStartStep = juce::roundToInt(stepForBeat(note->getStartBeat().inBeats()));
            const auto noteLengthSteps = std::max(1, static_cast<int>(std::ceil(note->getLengthBeats().inBeats()
                                                                                / stepBeats)));
            if (noteStartStep < step && step < noteStartStep + noteLengthSteps)
            {
                const auto start = tracktion::core::BeatPosition::fromBeats(beatForStep(noteStartStep));
                const auto length = tracktion::core::BeatDuration::fromBeats((step - noteStartStep) * stepBeats);
                note->setStartAndLength(start, length, undoManager);
                markModified(patternTrackIndex(), ModelChange::notes);
                break;
            }
        }

        pattern().state.removeProperty(starterPlaceholderID, undoManager);
        const auto duration = std::max(0.02, stepBeats);
        sequence.addNote(pitch, tracktion::core::BeatPosition::fromBeats(beat),
                         tracktion::core::BeatDuration::fromBeats(duration), 100, 0, undoManager);
        markModified(patternTrackIndex(), ModelChange::notes);
        restartLivePlayback();
    }
    sendSynchronousChangeMessage();
}

int Session::noteLengthSteps(int step, int pitch) const
{
    if (patternClip == nullptr)
        return 0;
    const auto gridSteps = editorStepCount();
    const auto stepBeats = stepDurationBeats(editorStepResolution());
    if (step < 0 || step >= gridSteps || pitch < 0 || pitch > 127)
        return 0;
    const auto beat = beatForStep(step);
    for (auto* note : pattern().getSequence().getNotes())
        if (note->getNoteNumber() == pitch
            && std::abs(note->getStartBeat().inBeats() - beat) < 0.0001)
            return std::max(1, static_cast<int>(std::ceil(note->getLengthBeats().inBeats() / stepBeats)));
    return 0;
}

juce::Result Session::resizeNote(int step, int pitch, int lengthSteps)
{
    return resizeNote(step, pitch, static_cast<double>(lengthSteps));
}

juce::Result Session::resizeNote(int step, int pitch, double lengthSteps)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (patternClip == nullptr)
        return noOpenClip();
    const auto gridSteps = editorStepCount();
    const auto stepBeats = stepDurationBeats(editorStepResolution());
    if (step < 0 || step >= gridSteps || pitch < 0 || pitch > 127)
        return juce::Result::fail("Resize notes inside the visible grid.");
    lengthSteps = std::clamp(lengthSteps, 0.0625, static_cast<double>(gridSteps - step));
    const auto beat = beatForStep(step);
    auto& sequence = pattern().getSequence();
    auto* undoManager = &edit->getUndoManager();
    auto nextNoteStep = static_cast<double>(gridSteps);
    for (auto* note : sequence.getNotes())
        if (note->getNoteNumber() == pitch && note->getStartBeat().inBeats() > beat + 0.0001)
            nextNoteStep = std::min(nextNoteStep, stepForBeat(note->getStartBeat().inBeats()));
    lengthSteps = std::min(lengthSteps, std::max(0.0625, nextNoteStep - step));

    for (auto* note : sequence.getNotes())
        if (note->getNoteNumber() == pitch
            && std::abs(note->getStartBeat().inBeats() - beat) < 0.0001)
        {
            if (std::abs(note->getLengthBeats().inBeats() / stepBeats - lengthSteps) < 0.0001)
                return juce::Result::ok();
            const auto start = tracktion::core::BeatPosition::fromBeats(beat);
            const auto length = tracktion::core::BeatDuration::fromBeats(lengthSteps * stepBeats);
            note->setStartAndLength(start, length, undoManager);
            markModified(patternTrackIndex(), ModelChange::notes);
            sendSynchronousChangeMessage();
            return juce::Result::ok();
        }
    return juce::Result::fail("Select a note to resize.");
}

juce::Result Session::resizeNote(const juce::ValueTree& state, double lengthSteps)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (patternClip == nullptr)
        return noOpenClip();
    auto& sequence = pattern().getSequence();
    auto* note = sequence.getNoteFor(state);
    if (note == nullptr)
        return juce::Result::fail("Select a note to resize.");
    const auto stepBeats = stepDurationBeats(editorStepResolution());
    // The length asked for is the one the editor shows, measured from where
    // it shows the note to start; the part before the window is kept.
    const auto noteStart = stepForBeat(note->getStartBeat().inBeats());
    const auto noteLength = note->getLengthBeats().inBeats() / stepBeats;
    const auto startStep = shownStartStep(noteStart, noteLength);
    const auto hidden = startStep - noteStart;
    lengthSteps = std::clamp(lengthSteps, 0.001, static_cast<double>(editorStepCount()) - startStep);
    if (std::abs(noteLength - hidden - lengthSteps) < 0.0001)
        return juce::Result::ok();
    auto nextStart = static_cast<double>(editorStepCount());
    for (auto* other : sequence.getNotes())
        if (other != note && other->getNoteNumber() == note->getNoteNumber()
            && other->getStartBeat() > note->getStartBeat())
            nextStart = std::min(nextStart, stepForBeat(other->getStartBeat().inBeats()));
    lengthSteps = std::min(lengthSteps, std::max(0.001, nextStart - startStep));
    if (std::abs(noteLength - hidden - lengthSteps) < 0.0001)
        return juce::Result::ok();
    note->setStartAndLength(note->getStartBeat(),
                            tracktion::core::BeatDuration::fromBeats((hidden + lengthSteps) * stepBeats),
                            &edit->getUndoManager());
    markModified(patternTrackIndex(), ModelChange::notes);
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::resizeNoteFromLeft(double startStep, int pitch, double newStartStep)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (patternClip == nullptr)
        return noOpenClip();
    const auto stepBeats = stepDurationBeats(editorStepResolution());
    const auto startBeat = beatForStep(startStep);
    auto& sequence = pattern().getSequence();
    auto* undoManager = &edit->getUndoManager();
    for (auto* note : sequence.getNotes())
        if (note->getNoteNumber() == pitch && std::abs(note->getStartBeat().inBeats() - startBeat) < 0.0001)
        {
            const auto endStep = startStep + note->getLengthBeats().inBeats() / stepBeats;
            auto previousEnd = 0.0;
            for (auto* other : sequence.getNotes())
                if (other->getNoteNumber() == pitch && other != note && other->getStartBeat().inBeats() < startBeat)
                    previousEnd = std::max(previousEnd, stepForBeat(other->getEndBeat().inBeats()));
            newStartStep = std::clamp(newStartStep, previousEnd, endStep - 0.0625);
            if (std::abs(stepForBeat(note->getStartBeat().inBeats()) - newStartStep) < 0.0001)
                return juce::Result::ok();
            note->setStartAndLength(tracktion::core::BeatPosition::fromBeats(beatForStep(newStartStep)),
                                    tracktion::core::BeatDuration::fromBeats((endStep - newStartStep) * stepBeats), undoManager);
            markModified(patternTrackIndex(), ModelChange::notes);
            sendSynchronousChangeMessage();
            return juce::Result::ok();
        }
    return juce::Result::fail("Select a note to resize.");
}

juce::Result Session::resizeNoteFromLeft(const juce::ValueTree& state, double newStartStep)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (patternClip == nullptr)
        return noOpenClip();
    auto& sequence = pattern().getSequence();
    auto* note = sequence.getNoteFor(state);
    if (note == nullptr)
        return juce::Result::fail("Select a note to resize.");
    const auto stepBeats = stepDurationBeats(editorStepResolution());
    const auto endStep = stepForBeat(note->getEndBeat().inBeats());
    auto previousEnd = 0.0;
    for (auto* other : sequence.getNotes())
        if (other != note && other->getNoteNumber() == note->getNoteNumber()
            && other->getStartBeat() < note->getStartBeat())
            previousEnd = std::max(previousEnd, stepForBeat(other->getEndBeat().inBeats()));
    newStartStep = std::clamp(newStartStep, previousEnd, endStep - 0.001);
    if (std::abs(stepForBeat(note->getStartBeat().inBeats()) - newStartStep) < 0.0001)
        return juce::Result::ok();
    note->setStartAndLength(tracktion::core::BeatPosition::fromBeats(beatForStep(newStartStep)),
                            tracktion::core::BeatDuration::fromBeats((endStep - newStartStep) * stepBeats),
                            &edit->getUndoManager());
    markModified(patternTrackIndex(), ModelChange::notes);
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

bool Session::ensurePatternLengthSteps(int requiredSteps)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (requiredSteps <= editorStepCount())
        return true;
    if (requiredSteps > steps || patternClip == nullptr)
        return false;

    const auto position = pattern().getPosition();
    const auto startBeat = edit->tempoSequence.toBeats(position.time.getStart());
    const auto requiredBeats = tracktion::core::BeatDuration::fromBeats(requiredSteps * stepDurationBeats(editorStepResolution()));
    const auto end = edit->tempoSequence.toTime(startBeat + requiredBeats);
    // A looped clip's pattern is its loop, so the loop is what grows; the
    // clip grows only as far as one pass of the longer loop needs.
    if (pattern().isLooping())
    {
        const auto loopStart = pattern().getLoopStartBeats();
        pattern().setLoopRangeBeats({loopStart, loopStart + requiredBeats});
    }
    if (!pattern().isLooping() || end > position.time.getEnd())
        pattern().setLength(end - position.time.getStart(), true);
    // A clip grown to hold pasted notes wins the ground it grew over, as every
    // other clip edit does, rather than lying on top of its neighbour.
    makeRoomForClip(pattern());
    markModified(patternTrackIndex(), ModelChange::notes);
    refreshLoop();
    sendSynchronousChangeMessage();
    return true;
}

juce::Result Session::fillNoteToClipEnd(int step, int pitch)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (patternClip == nullptr)
        return noOpenClip();
    const auto gridSteps = editorStepCount();
    if (step < 0 || step >= gridSteps || pitch < 0 || pitch > 127)
        return juce::Result::fail("Select a note inside the visible grid.");

    const auto beat = beatForStep(step);
    const auto endBeat = patternOffsetBeats() + patternLengthBeats();
    if (endBeat <= beat + 0.0001)
        return juce::Result::fail("The selected note already starts at the clip end.");

    auto& sequence = pattern().getSequence();
    auto* undoManager = &edit->getUndoManager();
    auto nextNoteBeat = endBeat;
    for (auto* note : sequence.getNotes())
        if (note->getNoteNumber() == pitch && note->getStartBeat().inBeats() > beat + 0.0001)
            nextNoteBeat = std::min(nextNoteBeat, note->getStartBeat().inBeats());

    for (auto* note : sequence.getNotes())
        if (note->getNoteNumber() == pitch
            && std::abs(note->getStartBeat().inBeats() - beat) < 0.0001)
        {
            const auto start = tracktion::core::BeatPosition::fromBeats(beat);
            const auto length = tracktion::core::BeatDuration::fromBeats(std::max(0.0001, nextNoteBeat - beat));
            note->setStartAndLength(start, length, undoManager);
            markModified(patternTrackIndex(), ModelChange::notes);
            sendSynchronousChangeMessage();
            return juce::Result::ok();
        }
    return juce::Result::fail("Select a note to fill.");
}

juce::Result Session::fillNoteToClipEnd(const juce::ValueTree& state)
{
    if (patternClip == nullptr)
        return noOpenClip();
    auto& sequence = pattern().getSequence();
    auto* note = sequence.getNoteFor(state);
    if (note == nullptr)
        return juce::Result::fail("Select a note to fill.");
    const auto stepBeats = stepDurationBeats(editorStepResolution());
    const auto startStep = shownStartStep(stepForBeat(note->getStartBeat().inBeats()),
                                          note->getLengthBeats().inBeats() / stepBeats);
    return resizeNote(state, patternLengthBeats() / stepBeats - startStep);
}

juce::Result Session::moveNote(int sourceStep, int sourcePitch, int targetStep, int targetPitch)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (patternClip == nullptr)
        return noOpenClip();
    const auto gridSteps = editorStepCount();
    if (sourceStep < 0 || sourceStep >= gridSteps || targetStep < 0 || targetStep >= gridSteps
        || sourcePitch < 0 || sourcePitch > 127
        || targetPitch < 0 || targetPitch > 127)
        return juce::Result::fail("Move notes inside the visible pitch grid.");
    if (sourceStep == targetStep && sourcePitch == targetPitch)
        return juce::Result::ok();

    auto& sequence = pattern().getSequence();
    auto* undoManager = &edit->getUndoManager();
    auto* moving = static_cast<te::MidiNote*>(nullptr);
    const auto sourceBeat = beatForStep(sourceStep);
    const auto targetBeat = beatForStep(targetStep);
    for (auto* note : sequence.getNotes())
    {
        const auto noteBeat = note->getStartBeat().inBeats();
        if (std::abs(noteBeat - targetBeat) < 0.0001 && note->getNoteNumber() == targetPitch)
            return juce::Result::fail("That note cell is already occupied.");
        if (std::abs(noteBeat - sourceBeat) < 0.0001 && note->getNoteNumber() == sourcePitch)
            moving = note;
    }
    if (moving == nullptr)
        return juce::Result::fail("Select a note to move.");

    const auto targetStart = tracktion::core::BeatPosition::fromBeats(targetBeat);
    const auto maximumLength = tracktion::core::BeatDuration::fromBeats(
        patternOffsetBeats() + patternLengthBeats() - targetStart.inBeats());
    if (maximumLength <= tracktion::core::BeatDuration())
        return juce::Result::fail("Move notes inside the clip.");
    moving->setStartAndLength(targetStart, std::min(moving->getLengthBeats(), maximumLength), undoManager);
    moving->setNoteNumber(targetPitch, undoManager);
    markModified(patternTrackIndex(), ModelChange::notes);
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::moveNotes(const std::vector<std::pair<int, int>>& sources, int stepDelta, int pitchDelta)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (sources.empty() || (stepDelta == 0 && pitchDelta == 0))
        return juce::Result::ok();
    if (patternClip == nullptr)
        return noOpenClip();

    const auto gridSteps = editorStepCount();
    const auto stepBeats = stepDurationBeats(editorStepResolution());
    struct PlannedMove { te::MidiNote* note; int targetStep, targetPitch; };
    std::vector<PlannedMove> moves;
    std::set<std::pair<int, int>> sourceCells, targetCells;

    for (const auto& [sourceStep, sourcePitch] : sources)
    {
        const auto targetStep = sourceStep + stepDelta;
        const auto targetPitch = sourcePitch + pitchDelta;
        if (targetStep < 0 || targetStep >= gridSteps || targetPitch < 0 || targetPitch > 127)
            return juce::Result::fail("Move notes inside the visible grid.");
        if (!sourceCells.insert({sourceStep, sourcePitch}).second
            || !targetCells.insert({targetStep, targetPitch}).second)
            return juce::Result::fail("That note group does not fit here.");
    }

    for (auto* note : pattern().getSequence().getNotes())
    {
        const auto step = juce::roundToInt(stepForBeat(note->getStartBeat().inBeats()));
        const auto cell = std::pair {step, note->getNoteNumber()};
        if (sourceCells.contains(cell))
        {
            const auto lengthSteps = std::max(1, static_cast<int>(std::ceil(note->getLengthBeats().inBeats() / stepBeats)));
            if (step + stepDelta + lengthSteps > gridSteps)
                return juce::Result::fail("That note group does not fit here.");
            moves.push_back({note, step + stepDelta, note->getNoteNumber() + pitchDelta});
        }
        else if (targetCells.contains(cell))
            return juce::Result::fail("That note cell is already occupied.");
    }
    if (moves.size() != sources.size())
        return juce::Result::fail("Select notes to move.");

    const auto wasPlaying = edit->getTransport().isPlaying();
    // A moved sustained note may already be active in the current playback
    // graph. Its old note-off can otherwise disappear when the MIDI sequence
    // changes, leaving the instrument sounding indefinitely.
    if (wasPlaying)
        panicMidiOnTrack(trackHoldingClip(*edit, pattern()));
    auto* undoManager = &edit->getUndoManager();
    for (const auto& move : moves)
        move.note->setStartAndLength(tracktion::core::BeatPosition::fromBeats(beatForStep(move.targetStep)),
                                     move.note->getLengthBeats(), undoManager);
    for (const auto& move : moves)
        move.note->setNoteNumber(move.targetPitch, undoManager);
    markModified(patternTrackIndex(), ModelChange::notes);
    if (wasPlaying)
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::moveNotes(const std::vector<juce::ValueTree>& states, double stepDelta, int pitchDelta,
                                const std::vector<int>& lanes)
{
    if (states.empty() || (std::abs(stepDelta) < 0.0001 && pitchDelta == 0))
        return juce::Result::ok();
    if (patternClip == nullptr)
        return noOpenClip();

    auto& sequence = pattern().getSequence();
    const auto stepBeats = stepDurationBeats(editorStepResolution());
    // lane is the note's place among lanes, -1 when it is on none of them.
    struct Move { te::MidiNote* note; double start, length; int pitch, lane; };
    std::vector<Move> moves;
    moves.reserve(states.size());
    for (const auto& state : states)
    {
        auto* note = sequence.getNoteFor(state);
        if (note == nullptr)
            return juce::Result::fail("Select notes to move.");
        const auto pitch = note->getNoteNumber();
        const auto lane = std::lower_bound(lanes.begin(), lanes.end(), pitch);
        moves.push_back({note, stepForBeat(note->getStartBeat().inBeats()),
                         note->getLengthBeats().inBeats() / stepBeats, pitch,
                         lane != lanes.end() && *lane == pitch ? static_cast<int>(lane - lanes.begin()) : -1});
    }
    // Where a pitch change puts a note: so many semitones, or so many lanes.
    const auto byLanes = !lanes.empty();
    const auto targetPitchOf = [&lanes, byLanes](const Move& move, int delta)
    {
        if (delta == 0)
            return move.pitch;
        return byLanes ? lanes[static_cast<size_t>(move.lane + delta)] : move.pitch + delta;
    };

    const auto isMoving = [&states](const te::MidiNote& candidate)
    {
        return std::find(states.begin(), states.end(), candidate.state) != states.end();
    };
    constexpr double tolerance = 0.0001;
    const auto gridSteps = static_cast<double>(editorStepCount());

    // The group travels rigidly, so one shift range covers all of it: how far
    // it may slide either way before a note leaves the clip or meets a note
    // that is staying put. Clamping to that range is what keeps a dragged note
    // under the pointer. Sliding it past an obstacle instead would strand a
    // long note several steps ahead of the pointer for the rest of the drag.
    const auto shiftRange = [&](int candidatePitchDelta, double& lowest, double& highest)
    {
        lowest = std::numeric_limits<double>::lowest();
        highest = std::numeric_limits<double>::max();
        for (const auto& move : moves)
        {
            lowest = std::max(lowest, -move.start);
            highest = std::min(highest, gridSteps - move.length - move.start);
            const auto targetPitch = targetPitchOf(move, candidatePitchDelta);
            for (auto* other : sequence.getNotes())
            {
                if (other->getNoteNumber() != targetPitch || isMoving(*other))
                    continue;
                const auto otherStart = stepForBeat(other->getStartBeat().inBeats());
                const auto otherEnd = stepForBeat(other->getEndBeat().inBeats());
                if (otherEnd <= move.start + tolerance)
                    lowest = std::max(lowest, otherEnd - move.start);
                else if (otherStart >= move.start + move.length - tolerance)
                    highest = std::min(highest, otherStart - move.length - move.start);
                else
                    return false; // The lane this pitch shift lands on is already taken here.
            }
        }
        return true;
    };

    for (const auto& move : moves)
        pitchDelta = !byLanes ? std::clamp(pitchDelta, -move.pitch, 127 - move.pitch)
                   : move.lane < 0 ? 0
                   : std::clamp(pitchDelta, -move.lane, static_cast<int>(lanes.size()) - 1 - move.lane);
    double lowest = 0.0, highest = 0.0;
    if (!shiftRange(pitchDelta, lowest, highest))
    {
        // Drop the pitch change and keep the horizontal one. Notes never
        // overlap within a lane, so the range always resolves at the old pitch.
        pitchDelta = 0;
        if (!shiftRange(0, lowest, highest))
            return juce::Result::fail("That note cell is already occupied.");
    }
    const auto resolvedDelta = std::clamp(stepDelta, std::min(0.0, lowest), std::max(0.0, highest));
    if (std::abs(resolvedDelta) < tolerance && pitchDelta == 0)
        return juce::Result::ok();

    const auto wasPlaying = edit->getTransport().isPlaying();
    if (wasPlaying) panicMidiOnTrack(trackHoldingClip(*edit, pattern()));
    auto* undoManager = &edit->getUndoManager();
    for (const auto& move : moves)
    {
        move.note->setStartAndLength(tracktion::core::BeatPosition::fromBeats(beatForStep(move.start + resolvedDelta)),
                                     move.note->getLengthBeats(), undoManager);
        move.note->setNoteNumber(targetPitchOf(move, pitchDelta), undoManager);
    }
    markModified(patternTrackIndex(), ModelChange::notes);
    if (wasPlaying) edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::redistributeNotes(const std::vector<juce::ValueTree>& states, int divisions,
                                        std::vector<juce::ValueTree>& replacementStates)
{
    replacementStates.clear();
    divisions = juce::jlimit(2, 32, divisions);
    if (states.empty())
        return juce::Result::fail("Select a note to divide.");
    if (patternClip == nullptr)
        return noOpenClip();

    auto& sequence = pattern().getSequence();
    std::vector<te::MidiNote*> sources;
    sources.reserve(states.size());
    for (const auto& state : states)
        if (auto* note = sequence.getNoteFor(state)) sources.push_back(note);
    if (sources.size() != states.size())
        return juce::Result::fail("The selected notes changed before they could be divided.");

    const auto pitch = sources.front()->getNoteNumber();
    auto start = sources.front()->getStartBeat().inBeats();
    auto end = sources.front()->getEndBeat().inBeats();
    for (auto* note : sources)
    {
        if (note->getNoteNumber() != pitch)
            return juce::Result::fail("Divide notes on one pitch lane at a time.");
        start = std::min(start, note->getStartBeat().inBeats());
        end = std::max(end, note->getEndBeat().inBeats());
    }
    // What is divided is what the editor shows: a note that began before the
    // window is shown from the window's start, and divided from there too, so
    // no retrigger lands where the clip cannot play it.
    if (const auto windowStart = patternOffsetBeats(); start < windowStart && end > windowStart)
        start = windowStart;
    if (end - start < 0.0001)
        return juce::Result::fail("The selected note is too short to divide.");
    for (auto* other : sequence.getNotes())
        if (other->getNoteNumber() == pitch
            && std::find(sources.begin(), sources.end(), other) == sources.end()
            && other->getStartBeat().inBeats() < end - 0.0001
            && start < other->getEndBeat().inBeats() - 0.0001)
            return juce::Result::fail("The selected span contains another note.");

    const auto velocity = sources.front()->getVelocity();
    const auto colour = sources.front()->getColour();
    auto* undoManager = &edit->getUndoManager();
    for (auto* note : sources)
        sequence.removeNote(*note, undoManager);
    const auto length = (end - start) / divisions;
    replacementStates.reserve(static_cast<size_t>(divisions));
    for (int division = 0; division < divisions; ++division)
        if (auto* note = sequence.addNote(pitch,
                tracktion::core::BeatPosition::fromBeats(start + division * length),
                tracktion::core::BeatDuration::fromBeats(length), velocity, colour, undoManager))
            replacementStates.push_back(note->state);

    markModified(patternTrackIndex(), ModelChange::notes);
    if (edit->getTransport().isPlaying())
    {
        panicMidiOnTrack(trackHoldingClip(*edit, pattern()));
        edit->restartPlayback();
    }
    sendSynchronousChangeMessage();
    return replacementStates.size() == static_cast<size_t>(divisions)
        ? juce::Result::ok() : juce::Result::fail("The note could not be divided.");
}

}
