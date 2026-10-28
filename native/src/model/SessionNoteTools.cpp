#include "SessionInternal.h"
#include <algorithm>
#include <cmath>
#include <limits>

// The note editor's tools panel, Live's Pitch and Time Utilities: Reverse,
// Legato and Humanize.
//
// Each takes the notes named, or every note the editor shows when none are,
// which is Live's rule for the panel's buttons. Each works on where the editor
// shows a note (editorNotes), so a note that began before the clip's window is
// moved from the clip's start and its hidden part dropped, as a chop drops it.
// A note wholly outside the window plays nothing and is left where it is.

namespace rhino
{
namespace
{
constexpr double tolerance = 0.0001;

bool named(const std::vector<juce::ValueTree>& states, const juce::ValueTree& state)
{
    return std::find(states.begin(), states.end(), state) != states.end();
}

// The notes a tool acts on: those named that start inside the clip, where they
// sound, or with none named, every note that does.
std::vector<Session::EditorNote> targetsOf(const std::vector<Session::EditorNote>& all,
                                           const std::vector<juce::ValueTree>& states, double clipEnd)
{
    std::vector<Session::EditorNote> targets;
    for (const auto& note : all)
        if (note.startSteps >= 0.0 && note.startSteps < clipEnd && (states.empty() || named(states, note.state)))
            targets.push_back(note);
    return targets;
}

bool isTarget(const std::vector<Session::EditorNote>& targets, const juce::ValueTree& state)
{
    return std::any_of(targets.begin(), targets.end(), [&state](const auto& note) { return note.state == state; });
}

juce::Result nothingTo(const char* verb, bool wholeClip)
{
    return juce::Result::fail(wholeClip ? juce::String("The clip has no notes to ") + verb + "."
                                        : juce::String("Select notes to ") + verb + ".");
}
}

// The clip's own length in the editor's steps, which the step count rounds up.
double Session::clipLengthSteps() const
{
    return patternLengthBeats() / stepDurationBeats(editorStepResolution());
}

bool Session::placeNotes(const std::vector<EditorNote>& placements)
{
    auto& sequence = pattern().getSequence();
    auto* undoManager = &edit->getUndoManager();
    const auto stepBeats = stepDurationBeats(editorStepResolution());
    const auto playing = edit->getTransport().isPlaying();
    auto changed = false;
    for (const auto& placed : placements)
        if (auto* note = sequence.getNoteFor(placed.state))
        {
            const auto start = tracktion::core::BeatPosition::fromBeats(beatForStep(placed.startSteps));
            const auto length = tracktion::core::BeatDuration::fromBeats(std::max(0.001, placed.lengthSteps) * stepBeats);
            if (std::abs(note->getStartBeat().inBeats() - start.inBeats()) < 1.0e-9
                && std::abs(note->getLengthBeats().inBeats() - length.inBeats()) < 1.0e-9)
                continue;
            // A moved note may be sounding, and its old note-off can vanish
            // with the sequence it belonged to, as moveNotes knows.
            if (!changed && playing)
                panicMidiOnTrack(trackHoldingClip(*edit, pattern()));
            note->setStartAndLength(start, length, undoManager);
            changed = true;
        }
    if (!changed)
        return false;
    markModified(patternTrackIndex(), ModelChange::notes);
    if (playing)
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return true;
}

juce::Result Session::reverseNotes(const std::vector<juce::ValueTree>& states)
{
    const NoteStep undoStep(*this, "Reverse notes");
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (patternClip == nullptr)
        return juce::Result::fail("Open a MIDI clip to edit its notes.");
    const auto all = editorNotes();
    const auto clipEnd = clipLengthSteps();
    auto notes = targetsOf(all, states, clipEnd);
    if (notes.empty())
        return nothingTo("reverse", states.empty());
    // The span the notes are mirrored across: their own, or the whole clip's.
    // What runs past the clip's end never sounds, and cutting it there keeps
    // its mirror image from landing before the span.
    auto spanStart = states.empty() ? 0.0 : std::numeric_limits<double>::max();
    auto spanEnd = states.empty() ? clipEnd : 0.0;
    for (auto& note : notes)
    {
        note.lengthSteps = std::max(0.001, std::min(note.startSteps + note.lengthSteps, clipEnd) - note.startSteps);
        spanStart = std::min(spanStart, note.startSteps);
        spanEnd = std::max(spanEnd, note.startSteps + note.lengthSteps);
    }
    for (auto& note : notes)
        note.startSteps = spanStart + spanEnd - (note.startSteps + note.lengthSteps);
    // A mirror keeps the reversed notes clear of each other, but not of a note
    // between them that is staying where it is.
    for (const auto& placed : notes)
        for (const auto& other : all)
            if (other.pitch == placed.pitch && !isTarget(notes, other.state)
                && other.startSteps < placed.startSteps + placed.lengthSteps - tolerance
                && placed.startSteps < other.startSteps + other.lengthSteps - tolerance)
                return juce::Result::fail("Reversing would land on notes that are not selected. Select them too.");
    placeNotes(notes);
    return juce::Result::ok();
}

juce::Result Session::legatoNotes(const std::vector<juce::ValueTree>& states)
{
    const NoteStep undoStep(*this, "Legato");
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (patternClip == nullptr)
        return juce::Result::fail("Open a MIDI clip to edit its notes.");
    const auto all = editorNotes();
    const auto clipEnd = clipLengthSteps();
    const auto notes = targetsOf(all, states, clipEnd);
    if (notes.empty())
        return nothingTo("make legato", states.empty());
    std::vector<double> starts;
    for (const auto& note : notes)
        starts.push_back(note.startSteps);
    std::sort(starts.begin(), starts.end());
    std::vector<EditorNote> placed;
    for (auto note : notes)
    {
        // The next start after this one, a chord's notes sharing theirs, or
        // for the last the clip's end, which is Live's loop end here.
        const auto next = std::upper_bound(starts.begin(), starts.end(), note.startSteps + tolerance);
        auto end = next != starts.end() ? *next : clipEnd;
        // Never over the next note on its own pitch, selected or not.
        for (const auto& other : all)
            if (other.pitch == note.pitch && other.state != note.state && other.startSteps > note.startSteps + tolerance)
                end = std::min(end, other.startSteps);
        if (end - note.startSteps <= tolerance)
            continue;
        note.lengthSteps = end - note.startSteps;
        placed.push_back(note);
    }
    placeNotes(placed);
    return juce::Result::ok();
}

juce::Result Session::humanizeNotes(const std::vector<juce::ValueTree>& states, double amount, juce::int64 seed)
{
    const NoteStep undoStep(*this, "Humanize");
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (patternClip == nullptr)
        return juce::Result::fail("Open a MIDI clip to edit its notes.");
    amount = std::clamp(amount, 0.0, 1.0);
    if (amount <= 0.0)
        return juce::Result::fail("Turn the Humanize amount up from 0% to move the notes.");
    const auto all = editorNotes();
    const auto clipEnd = clipLengthSteps();
    const auto notes = targetsOf(all, states, clipEnd);
    if (notes.empty())
        return nothingTo("humanize", states.empty());
    const auto reach = amount * humanizeReachSteps;
    juce::Random random(seed);
    // Each pitch in time order, every note on it, so a moving note is held
    // between where the one before it now ends and where the one after it
    // starts: no drift makes two notes on one pitch overlap or change places.
    auto ordered = all;
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b)
    {
        return a.pitch != b.pitch ? a.pitch < b.pitch : a.startSteps < b.startSteps;
    });
    std::vector<EditorNote> placed;
    auto previousEnd = 0.0;
    for (size_t index = 0; index < ordered.size(); ++index)
    {
        auto note = ordered[index];
        if (index == 0 || ordered[index - 1].pitch != note.pitch)
            previousEnd = 0.0;
        if (isTarget(notes, note.state))
        {
            const auto nextStart = index + 1 < ordered.size() && ordered[index + 1].pitch == note.pitch
                ? ordered[index + 1].startSteps : std::numeric_limits<double>::max();
            const auto lowest = previousEnd;
            // A start past the clip's end would play nothing.
            const auto highest = std::min(nextStart - note.lengthSteps, clipEnd - tolerance);
            const auto drift = (random.nextDouble() * 2.0 - 1.0) * reach;
            // Held to a range the note already stands in, so it never moves
            // further than its drift; notes that overlapped before stay put.
            if (lowest <= note.startSteps && note.startSteps <= highest)
                note.startSteps = std::clamp(note.startSteps + drift, lowest, highest);
            placed.push_back(note);
        }
        previousEnd = std::max(previousEnd, note.startSteps + note.lengthSteps);
    }
    placeNotes(placed);
    return juce::Result::ok();
}

}
