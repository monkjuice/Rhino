#include "SessionInternal.h"
#include <algorithm>
#include <limits>
#include <set>

// Note editing and pattern geometry. Serves StepGrid.

namespace rhino
{

bool Session::hasNote(int step, int pitch) const
{
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
    const auto stepBeats = stepDurationBeats(editorStepResolution());
    result.reserve(static_cast<size_t>(pattern().getSequence().getNumNotes()));
    for (auto* note : pattern().getSequence().getNotes())
        result.push_back({note->state,
                          stepForBeat(note->getStartBeat().inBeats()),
                          note->getLengthBeats().inBeats() / stepBeats,
                          note->getNoteNumber(), note->getVelocity(), note->getColour()});
    return result;
}

juce::Result Session::addNote(double startSteps, int pitch, double lengthSteps,
                              juce::ValueTree* addedState, int velocity)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
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
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

bool Session::removeNotes(const std::vector<juce::ValueTree>& states)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    auto& sequence = pattern().getSequence();
    auto& transport = edit->getTransport();
    const auto playing = transport.isPlaying();
    const auto clipStartBeat = edit->tempoSequence.toBeats(pattern().getPosition().time.getStart()).inBeats();
    const auto playheadBeat = edit->tempoSequence.toBeats(transport.getPosition()).inBeats()
                            - clipStartBeat + patternOffsetBeats();
    auto removedActiveNote = false;
    auto changed = false;
    for (const auto& state : states)
        if (auto* note = sequence.getNoteFor(state))
        {
            removedActiveNote = removedActiveNote || (playing
                && playheadBeat >= note->getStartBeat().inBeats()
                && playheadBeat < note->getEndBeat().inBeats());
            sequence.removeNote(*note, &edit->getUndoManager());
            changed = true;
        }
    if (changed)
    {
        markModified();
        if (playing)
        {
            // Rebuild so the next pass uses the edited sequence. A MIDI panic
            // is only needed when the removed note was already sounding;
            // panicking for every edit audibly cuts unrelated notes and FX.
            if (removedActiveNote)
                panicMidiOnTrack(pattern().getClipTrack());
            edit->restartPlayback();
        }
        sendSynchronousChangeMessage();
    }
    return changed;
}

bool Session::adjustNoteVelocities(const std::vector<juce::ValueTree>& states, int percentageDelta)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (states.empty() || percentageDelta == 0)
        return false;
    auto& sequence = pattern().getSequence();
    auto* undoManager = &edit->getUndoManager();
    auto changed = false;
    for (const auto& state : states)
        if (auto* note = sequence.getNoteFor(state))
        {
            const auto oldPercent = juce::roundToInt(note->getVelocity() * 100.0 / 127.0);
            const auto newPercent = juce::jlimit(1, 100, oldPercent + percentageDelta);
            const auto newVelocity = juce::jlimit(1, 127, juce::roundToInt(newPercent * 127.0 / 100.0));
            if (newVelocity != note->getVelocity())
            {
                note->setVelocity(newVelocity, undoManager);
                changed = true;
            }
        }
    if (changed)
    {
        markModified();
        sendSynchronousChangeMessage();
    }
    return changed;
}

void Session::beginNoteGesture(juce::String actionName) { edit->getUndoManager().beginNewTransaction(actionName); }
void Session::endNoteGesture() { edit->getUndoManager().beginNewTransaction(); }

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

double Session::patternOffsetBeats() const
{
    return patternClip == nullptr ? 0.0 : clipOffsetBeats(*patternClip);
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
    markModified();
    sendSynchronousChangeMessage();
}

void Session::setNote(int step, int pitch, bool enabled)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
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
        panicMidiOnTrack(pattern().getClipTrack());
        edit->restartPlayback();
    };
    for (auto* note : sequence.getNotes())
        if (note->getNoteNumber() == pitch
            && std::abs(note->getStartBeat().inBeats() - beat) < 0.0001)
        {
            if (!enabled)
            {
                sequence.removeNote(*note, undoManager);
                markModified();
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
                markModified();
                break;
            }
        }

        pattern().state.removeProperty(starterPlaceholderID, undoManager);
        const auto duration = std::max(0.02, stepBeats);
        sequence.addNote(pitch, tracktion::core::BeatPosition::fromBeats(beat),
                         tracktion::core::BeatDuration::fromBeats(duration), 100, 0, undoManager);
        markModified();
        restartLivePlayback();
    }
    sendSynchronousChangeMessage();
}

int Session::noteLengthSteps(int step, int pitch) const
{
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
            const auto start = tracktion::core::BeatPosition::fromBeats(beat);
            const auto length = tracktion::core::BeatDuration::fromBeats(lengthSteps * stepBeats);
            note->setStartAndLength(start, length, undoManager);
            markModified();
            sendSynchronousChangeMessage();
            return juce::Result::ok();
        }
    return juce::Result::fail("Select a note to resize.");
}

juce::Result Session::resizeNote(const juce::ValueTree& state, double lengthSteps)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    auto& sequence = pattern().getSequence();
    auto* note = sequence.getNoteFor(state);
    if (note == nullptr)
        return juce::Result::fail("Select a note to resize.");
    const auto stepBeats = stepDurationBeats(editorStepResolution());
    const auto startStep = stepForBeat(note->getStartBeat().inBeats());
    lengthSteps = std::clamp(lengthSteps, 0.001, static_cast<double>(editorStepCount()) - startStep);
    auto nextStart = static_cast<double>(editorStepCount());
    for (auto* other : sequence.getNotes())
        if (other != note && other->getNoteNumber() == note->getNoteNumber()
            && other->getStartBeat() > note->getStartBeat())
            nextStart = std::min(nextStart, stepForBeat(other->getStartBeat().inBeats()));
    lengthSteps = std::min(lengthSteps, std::max(0.001, nextStart - startStep));
    note->setStartAndLength(note->getStartBeat(),
                            tracktion::core::BeatDuration::fromBeats(lengthSteps * stepBeats),
                            &edit->getUndoManager());
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::resizeNoteFromLeft(double startStep, int pitch, double newStartStep)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
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
            note->setStartAndLength(tracktion::core::BeatPosition::fromBeats(beatForStep(newStartStep)),
                                    tracktion::core::BeatDuration::fromBeats((endStep - newStartStep) * stepBeats), undoManager);
            markModified();
            sendSynchronousChangeMessage();
            return juce::Result::ok();
        }
    return juce::Result::fail("Select a note to resize.");
}

juce::Result Session::resizeNoteFromLeft(const juce::ValueTree& state, double newStartStep)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
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
    note->setStartAndLength(tracktion::core::BeatPosition::fromBeats(beatForStep(newStartStep)),
                            tracktion::core::BeatDuration::fromBeats((endStep - newStartStep) * stepBeats),
                            &edit->getUndoManager());
    markModified();
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
    const auto end = edit->tempoSequence.toTime(startBeat + tracktion::core::BeatDuration::fromBeats(
        requiredSteps * stepDurationBeats(editorStepResolution())));
    pattern().setLength(end - position.time.getStart(), true);
    markModified();
    refreshLoop();
    sendSynchronousChangeMessage();
    return true;
}

juce::Result Session::fillNoteToClipEnd(int step, int pitch)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
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
            markModified();
            sendSynchronousChangeMessage();
            return juce::Result::ok();
        }
    return juce::Result::fail("Select a note to fill.");
}

juce::Result Session::fillNoteToClipEnd(const juce::ValueTree& state)
{
    auto& sequence = pattern().getSequence();
    auto* note = sequence.getNoteFor(state);
    if (note == nullptr)
        return juce::Result::fail("Select a note to fill.");
    const auto stepBeats = stepDurationBeats(editorStepResolution());
    const auto startStep = stepForBeat(note->getStartBeat().inBeats());
    return resizeNote(state, patternLengthBeats() / stepBeats - startStep);
}

juce::Result Session::moveNote(int sourceStep, int sourcePitch, int targetStep, int targetPitch)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
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
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::moveNotes(const std::vector<std::pair<int, int>>& sources, int stepDelta, int pitchDelta)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (sources.empty() || (stepDelta == 0 && pitchDelta == 0))
        return juce::Result::ok();

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
        panicMidiOnTrack(pattern().getClipTrack());
    auto* undoManager = &edit->getUndoManager();
    for (const auto& move : moves)
        move.note->setStartAndLength(tracktion::core::BeatPosition::fromBeats(beatForStep(move.targetStep)),
                                     move.note->getLengthBeats(), undoManager);
    for (const auto& move : moves)
        move.note->setNoteNumber(move.targetPitch, undoManager);
    markModified();
    if (wasPlaying)
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::moveNotes(const std::vector<juce::ValueTree>& states, double stepDelta, int pitchDelta)
{
    if (states.empty() || (std::abs(stepDelta) < 0.0001 && pitchDelta == 0))
        return juce::Result::ok();

    auto& sequence = pattern().getSequence();
    const auto stepBeats = stepDurationBeats(editorStepResolution());
    struct Move { te::MidiNote* note; double start, length; int pitch; };
    std::vector<Move> moves;
    moves.reserve(states.size());
    for (const auto& state : states)
    {
        auto* note = sequence.getNoteFor(state);
        if (note == nullptr)
            return juce::Result::fail("Select notes to move.");
        moves.push_back({note, stepForBeat(note->getStartBeat().inBeats()),
                         note->getLengthBeats().inBeats() / stepBeats,
                         note->getNoteNumber()});
    }

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
            const auto targetPitch = move.pitch + candidatePitchDelta;
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
        pitchDelta = std::clamp(pitchDelta, -move.pitch, 127 - move.pitch);
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
    if (wasPlaying) panicMidiOnTrack(pattern().getClipTrack());
    auto* undoManager = &edit->getUndoManager();
    for (const auto& move : moves)
    {
        move.note->setStartAndLength(tracktion::core::BeatPosition::fromBeats(beatForStep(move.start + resolvedDelta)),
                                     move.note->getLengthBeats(), undoManager);
        move.note->setNoteNumber(move.pitch + pitchDelta, undoManager);
    }
    markModified();
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

    markModified();
    if (edit->getTransport().isPlaying())
    {
        panicMidiOnTrack(pattern().getClipTrack());
        edit->restartPlayback();
    }
    sendSynchronousChangeMessage();
    return replacementStates.size() == static_cast<size_t>(divisions)
        ? juce::Result::ok() : juce::Result::fail("The note could not be divided.");
}

}
