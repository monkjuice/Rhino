#include "DjDeck.h"
#include <algorithm>
#include <cmath>

// The deck's transport: the commands that start, stop or move it, applied at
// a block boundary on the audio thread. See DjDeck.h.

namespace rhino
{
void DjDeck::beginPlaying(double fromFrame)
{
    pos = clampFrame(fromFrame);
    publish();
    stopping = false;
    braking = false;
    returnTo = -1.0;
    gain = 0.0f;
    setState(State::playing);
    setPending(Pending::none);
    pendingPosition.store(-1.0, std::memory_order_relaxed);
}

void DjDeck::stopWithFade(double returnToFrame)
{
    if (!isPlaying())
    {
        if (returnToFrame >= 0.0)
            jumpTo(clampFrame(returnToFrame));
        if (currentState() != State::empty)
            setState(State::stopped);
        setPending(Pending::none);
        pendingPosition.store(-1.0, std::memory_order_relaxed);
        return;
    }
    stopping = true;
    returnTo = returnToFrame;
}

void DjDeck::play()
{
    const auto s = currentState();
    if (s == State::empty || s == State::playing || s == State::waiting)
        return;
    if (s == State::cueing)
    {
        // Play pressed while cue is held: carry on from here, as a CDJ does.
        cueHeld = false;
        setState(State::playing);
        return;
    }
    pendingPosition.store(pos, std::memory_order_relaxed);
    setPending(Pending::start);
    setState(State::waiting);
}

void DjDeck::pause()
{
    const auto s = currentState();
    if (s == State::waiting)
    {
        setState(State::stopped);
        setPending(Pending::none);
        pendingPosition.store(-1.0, std::memory_order_relaxed);
        return;
    }
    if (s == State::playing || s == State::cueing)
    {
        cueHeld = false;
        const auto brake = controls.brakeSeconds.load(std::memory_order_relaxed);
        if (brake > 0.01f && !braking)
        {
            // The platter winds down rather than stopping dead; the stop
            // itself comes when the rate reaches nothing.
            braking = true;
            spinningUp = false;
            brakeStep = std::abs(rate) / (brake * grid.sampleRate);
            return;
        }
        stopWithFade(-1.0);
    }
}

void DjDeck::togglePlay()
{
    const auto s = currentState();
    if (s == State::playing || s == State::waiting || s == State::cueing)
        pause();
    else
        play();
}

// The cue button, as a CDJ reads it. Playing: stop and go back to the cue
// point. Stopped at the cue point: play from it for as long as the button is
// held. Stopped anywhere else: this is the cue point now.
void DjDeck::cueDown(bool quantise)
{
    const auto s = currentState();
    if (s == State::empty) return;
    if (s == State::playing || s == State::waiting)
    {
        if (s == State::waiting)
        {
            setState(State::stopped);
            setPending(Pending::none);
            pendingPosition.store(-1.0, std::memory_order_relaxed);
        }
        stopWithFade(cuePoint.load(std::memory_order_relaxed));
        return;
    }
    if (s == State::cueing) return;
    const auto cue = cuePoint.load(std::memory_order_relaxed);
    if (std::abs(pos - cue) < 1.0)
    {
        cueHeld = true;
        beginPlaying(cue);
        setState(State::cueing);
        return;
    }
    const auto point = quantise ? snapToGrid(pos, 1.0) : pos;
    cuePoint.store(point, std::memory_order_relaxed);
    jumpTo(point);
}

void DjDeck::cueUp()
{
    if (currentState() != State::cueing || !cueHeld) return;
    cueHeld = false;
    stopWithFade(cuePoint.load(std::memory_order_relaxed));
}

void DjDeck::hotCue(int index, bool quantise)
{
    if (index < 0 || index >= hotCueCount || currentState() == State::empty) return;
    auto& cue = hotCues[static_cast<size_t>(index)];
    const auto at = cue.load(std::memory_order_relaxed);
    if (at < 0.0)
    {
        cue.store(quantise ? snapToGrid(pos, 1.0) : pos, std::memory_order_relaxed);
        return;
    }
    // Set already: go there and play. The engine decides when, so a playing
    // deck lands in phase with the master when quantised.
    pendingPosition.store(at, std::memory_order_relaxed);
    if (isPlaying())
    {
        cueHeld = false;
        setState(State::playing);
        setPending(Pending::jump);
    }
    else
    {
        setPending(Pending::start);
        setState(State::waiting);
    }
}

void DjDeck::clearHotCue(int index)
{
    if (index < 0 || index >= hotCueCount) return;
    hotCues[static_cast<size_t>(index)].store(-1.0, std::memory_order_relaxed);
}

void DjDeck::setCuePoint(double frame)
{
    if (currentState() == State::empty) return;
    cuePoint.store(clampFrame(frame), std::memory_order_relaxed);
}

void DjDeck::setHotCuePoint(int index, double frame)
{
    if (index < 0 || index >= hotCueCount || currentState() == State::empty) return;
    hotCues[static_cast<size_t>(index)].store(frame < 0.0 ? -1.0 : clampFrame(frame), std::memory_order_relaxed);
}

// Exactly so many beats from where the deck stands, so a deck off the beat
// stays off it by the same fraction. An active loop travels with the jump, as
// a CDJ moves one, when it fits in the material; otherwise it stays, and a
// landing outside it leaves it.
void DjDeck::beatJump(int beats)
{
    if (currentState() == State::empty || beats == 0) return;
    const auto waiting = currentState() == State::waiting;
    const auto here = waiting ? pendingFrame() : pos;
    const auto distance = grid.valid() ? beats * grid.framesPerBeat : beats * 0.5 * grid.sampleRate;
    const auto target = clampFrame(here + distance);
    const auto moved = target - here;
    const auto start = loopStart.load(std::memory_order_relaxed), end = loopEnd.load(std::memory_order_relaxed);
    if (loopActive.load(std::memory_order_relaxed) && start >= 0.0 && end > start && start + moved >= 0.0
        && end + moved <= static_cast<double>(grid.length))
    {
        loopStart.store(start + moved, std::memory_order_relaxed);
        loopEnd.store(end + moved, std::memory_order_relaxed);
    }
    if (waiting)
        pendingPosition.store(target, std::memory_order_relaxed);
    jumpTo(target);
}

void DjDeck::seek(double frame)
{
    if (currentState() == State::empty) return;
    const auto target = clampFrame(frame);
    if (currentState() == State::waiting)
        pendingPosition.store(target, std::memory_order_relaxed);
    jumpTo(target);
}

// The master's phase within its boundary, placed into this deck's nearest
// boundary of the same size. A deck playing in its loop stays there: the
// place a span along when that lies inside the loop, so the bars still line
// up, and otherwise the place the loop would have carried it to, a loop's
// length round, which keeps the beat's phase when the loop is whole beats.
// Outside its loop it moves without leaving it, as the deck has not landed
// anywhere it asked to.
void DjDeck::alignPhase(double masterPhase, int boundaryBeats)
{
    if (!grid.valid() || currentState() == State::empty) return;
    const auto span = std::max(1, boundaryBeats);
    const auto beat = grid.beatAt(pos);
    const auto base = std::floor(beat / span) * span;
    auto target = base + masterPhase;
    if (target - beat > span * 0.5) target -= span;
    else if (beat - target > span * 0.5) target += span;
    const auto lStart = loopStart.load(std::memory_order_relaxed), lEnd = loopEnd.load(std::memory_order_relaxed);
    if (loopActive.load(std::memory_order_relaxed) && lStart >= 0.0 && lEnd > lStart + 1.0 && pos >= lStart && pos < lEnd)
    {
        const auto first = grid.beatAt(lStart), last = grid.beatAt(lEnd);
        if (target >= last && target - span >= first) target -= span;
        else if (target < first && target + span < last) target += span;
        auto frame = grid.frameAt(target);
        if (frame < lStart || frame >= lEnd)
            frame = lStart + std::fmod(std::fmod(frame - lStart, lEnd - lStart) + (lEnd - lStart), lEnd - lStart);
        moveTo(frame);
        return;
    }
    moveTo(clampFrame(grid.frameAt(target)));
}

// The landing itself happens in the plan, at the frame the engine chose: a
// jump crossfades there from the stretch it leaves, a start begins there. The
// target is exact: the engine timed the landing to it, not it to the landing.
void DjDeck::landPending(int frameOffset)
{
    const auto what = currentPending();
    if (what == Pending::none) return;
    const auto at = clampFrame(pendingFrame());
    if (what == Pending::jump)
    {
        jumpAt = std::max(0, frameOffset);
        jumpTarget = at;
        return;
    }
    // A start from where the deck stands is not a landing, and leaves a loop
    // it stands outside alone; one from a cue lands, and leaves it.
    if (std::abs(at - pos) >= 1.0)
        jumpTo(at);
    else
        noteJump();
    pos = at;
    publish();
    startAt = std::max(0, frameOffset);
}
}
