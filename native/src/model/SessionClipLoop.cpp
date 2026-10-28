#include "SessionInternal.h"
#include <cmath>

// The note editor's Loop switch, Live's clip Loop.
//
// The engine loops a MIDI clip itself: given a loop range, a stretch of its
// sequence in beats, te::MidiClip plays that stretch over and over for as long
// as the clip runs (getSequenceLooped at 4536d8a), and the clip's offset then
// counts into the loop rather than into the sequence. Nothing here repeats a
// note; this only sets the range, and keeps what is heard the same at the
// moment the switch is thrown.
//
// While a clip loops, the note editor shows the loop and nothing else, because
// patternOffsetBeats and patternLengthBeats are the loop's (SessionNotes.cpp),
// so every note edit lands in the loop and is heard on every pass of it.

namespace rhino
{
namespace
{
double positiveModulo(double value, double modulus)
{
    const auto wrapped = std::fmod(value, modulus);
    return wrapped < 0.0 ? wrapped + modulus : wrapped;
}
}

bool Session::isPatternLooping() const
{
    return patternClip != nullptr && patternClip->isLooping();
}

juce::Result Session::setPatternLooping(bool shouldLoop)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (patternClip == nullptr)
        return juce::Result::fail("Open a MIDI clip to loop it.");
    auto& clip = pattern();
    if (clip.isLooping() == shouldLoop)
        return juce::Result::ok();
    const auto position = clip.getPosition();
    const auto secondsPerBeat = 60.0 / tempo();
    edit->getUndoManager().beginNewTransaction(shouldLoop ? "Loop clip" : "Stop looping clip");
    if (shouldLoop)
    {
        // The loop is the window the clip plays now, so turning it on is
        // heard only once the clip is dragged longer.
        const auto start = clipOffsetBeats(clip);
        const auto length = patternLengthBeats();
        clip.setLoopRangeBeats({tracktion::core::BeatPosition::fromBeats(start),
                                tracktion::core::BeatPosition::fromBeats(start + length)});
        clip.setPosition({position.time, tracktion::core::TimeDuration()});
    }
    else
    {
        // The clip keeps its length, as Live's does, and plays its sequence
        // once from where the loop stood at its start; past the notes it is
        // silent. The engine's own disableLooping cuts the clip to one pass.
        const auto loopStart = clip.getLoopStartBeats().inBeats();
        const auto phase = positiveModulo(clipOffsetBeats(clip), std::max(0.0001, clip.getLoopLengthBeats().inBeats()));
        clip.setLoopRangeBeats({});
        clip.setPosition({position.time, tracktion::core::TimeDuration::fromSeconds((loopStart + phase) * secondsPerBeat)});
    }
    closeUndoStep();
    markModified(patternTrackIndex(), ModelChange::notes);
    if (edit->getTransport().isPlaying())
    {
        // What sounds now may not be what plays next.
        panicMidiOnTrack(trackHoldingClip(*edit, clip));
        edit->restartPlayback();
    }
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

double Session::patternStepAt(double seconds) const
{
    if (patternClip == nullptr)
        return -1.0;
    const auto position = patternClip->getPosition();
    const auto clipStart = position.time.getStart().inSeconds();
    if (seconds < clipStart || seconds >= position.time.getEnd().inSeconds())
        return -1.0;
    // From the clip's start the editor's step 0 plays, unless the clip loops:
    // then its offset is how far into the loop it starts, and every pass of
    // the loop is the same steps again.
    auto beat = secondsToBeats(seconds) - secondsToBeats(clipStart);
    if (patternClip->isLooping())
        beat = positiveModulo(beat + clipOffsetBeats(*patternClip), patternLengthBeats());
    return beat / stepDurationBeats(editorStepResolution());
}

}
