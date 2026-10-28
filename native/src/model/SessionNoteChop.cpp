#include "SessionInternal.h"
#include <algorithm>
#include <cmath>
#include <utility>

// Chopping notes: Ctrl+E in the note editor, Live's Chop Note(s) on Grid.
//
// A chop cuts each note where it stands, on its own pitch, so it reaches every
// note selected whatever its pitch - unlike redistributeNotes, which spreads
// one span of one pitch into equal retriggers and is what the editor's divide
// used before 2026-10-25. The editor holds the notes as they were and chops
// them again from those whenever the count changes, so going up and back down
// returns exactly the cut it started from.

namespace rhino
{
namespace
{
// Where a note is cut, as start and length in steps: at each step line inside
// it, or into equal parts.
std::vector<std::pair<double, double>> chopSpans(double start, double length, int parts)
{
    std::vector<std::pair<double, double>> spans;
    const auto end = start + length;
    if (parts <= 0)
    {
        auto from = start;
        for (auto line = std::floor(start + 0.0001) + 1.0; line < end - 0.0001; line += 1.0)
        {
            spans.emplace_back(from, line - from);
            from = line;
        }
        spans.emplace_back(from, end - from);
        return spans;
    }
    for (int part = 0; part < parts; ++part)
        spans.emplace_back(start + length * part / parts, length / parts);
    return spans;
}
}

int Session::gridChopParts(double startSteps, double lengthSteps)
{
    return static_cast<int>(chopSpans(startSteps, lengthSteps, 0).size());
}

juce::Result Session::chopNotes(const std::vector<juce::ValueTree>& current, const std::vector<NoteChop>& chops,
                                std::vector<juce::ValueTree>& pieces)
{
    const NoteStep undoStep(*this, "Chop notes");
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    pieces.clear();
    if (patternClip == nullptr)
        return juce::Result::fail("Open a MIDI clip to edit its notes.");
    if (chops.empty())
        return juce::Result::fail("Select notes to chop.");
    auto& sequence = pattern().getSequence();
    auto* undoManager = &edit->getUndoManager();
    const auto stepBeats = stepDurationBeats(editorStepResolution());
    for (const auto& state : current)
        if (auto* note = sequence.getNoteFor(state))
            sequence.removeNote(*note, undoManager);
    for (const auto& chop : chops)
        for (const auto& [start, length] : chopSpans(chop.startSteps, chop.lengthSteps, std::clamp(chop.parts, 0, 32)))
            if (auto* note = sequence.addNote(chop.pitch, tracktion::core::BeatPosition::fromBeats(beatForStep(start)),
                                              tracktion::core::BeatDuration::fromBeats(length * stepBeats),
                                              juce::jlimit(1, 127, chop.velocity), chop.colour, undoManager))
                pieces.push_back(note->state);
    markModified(patternTrackIndex(), ModelChange::notes);
    // A cut note may be the one sounding; the pieces replace it in the graph.
    if (edit->getTransport().isPlaying())
    {
        panicMidiOnTrack(trackHoldingClip(*edit, pattern()));
        edit->restartPlayback();
    }
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

}
