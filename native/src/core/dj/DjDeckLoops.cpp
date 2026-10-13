#include "DjDeck.h"
#include <algorithm>
#include <cmath>

// The deck's loop: its points and their editing. The wrap itself is played in
// DjDeckRender.cpp. See DjDeck.h.

namespace rhino
{
void DjDeck::loopIn(bool quantise)
{
    if (currentState() == State::empty) return;
    loopStart.store(quantise ? snapToGrid(pos, 1.0) : pos, std::memory_order_relaxed);
    loopEnd.store(-1.0, std::memory_order_relaxed);
    loopActive.store(false, std::memory_order_relaxed);
}

void DjDeck::loopOut(bool quantise)
{
    const auto start = loopStart.load(std::memory_order_relaxed);
    if (currentState() == State::empty || start < 0.0) return;
    const auto end = quantise ? snapToGrid(pos, 1.0) : pos;
    if (end <= start + 1.0) return;
    loopEnd.store(end, std::memory_order_relaxed);
    loopActive.store(true, std::memory_order_relaxed);
}

void DjDeck::beatLoop(double beats, bool quantise)
{
    if (currentState() == State::empty || beats <= 0.0) return;
    double start = pos, end;
    if (grid.valid())
    {
        // From the beat the deck is on - the beat's own start when
        // quantised - for the given number of beats.
        const auto beat = grid.beatAt(pos);
        const auto from = quantise ? std::floor(beat) : beat;
        start = grid.frameAt(from);
        end = grid.frameAt(from + beats);
    }
    else
    {
        end = pos + beats * 0.5 * grid.sampleRate;
    }
    start = clampFrame(start);
    end = clampPoint(end);
    if (end <= start + 1.0) return;
    loopStart.store(start, std::memory_order_relaxed);
    loopEnd.store(end, std::memory_order_relaxed);
    loopActive.store(true, std::memory_order_relaxed);
}

void DjDeck::reloopExit()
{
    const auto start = loopStart.load(std::memory_order_relaxed), end = loopEnd.load(std::memory_order_relaxed);
    if (currentState() == State::empty || start < 0.0 || end <= start) return;
    if (loopActive.load(std::memory_order_relaxed))
    {
        loopActive.store(false, std::memory_order_relaxed);
        return;
    }
    loopActive.store(true, std::memory_order_relaxed);
    if (isPlaying())
    {
        jumpTo(start);
    }
    else
    {
        pendingPosition.store(start, std::memory_order_relaxed);
        setPending(Pending::start);
        setState(State::waiting);
    }
}

void DjDeck::loopHalve()
{
    const auto start = loopStart.load(std::memory_order_relaxed), end = loopEnd.load(std::memory_order_relaxed);
    if (start < 0.0 || end <= start) return;
    const auto length = (end - start) * 0.5;
    if (length < 32.0) return;
    loopEnd.store(start + length, std::memory_order_relaxed);
}

void DjDeck::loopDouble()
{
    const auto start = loopStart.load(std::memory_order_relaxed), end = loopEnd.load(std::memory_order_relaxed);
    if (start < 0.0 || end <= start) return;
    const auto doubled = clampPoint(start + (end - start) * 2.0);
    if (doubled <= end) return;
    loopEnd.store(doubled, std::memory_order_relaxed);
}

void DjDeck::clearLoop()
{
    loopStart.store(-1.0, std::memory_order_relaxed);
    loopEnd.store(-1.0, std::memory_order_relaxed);
    loopActive.store(false, std::memory_order_relaxed);
}

// Both points stand within the material, the end at its length at most: a
// whole-track loop, which every fresh bounce lands with, ends there.
void DjDeck::setLoop(double start, double end, bool active)
{
    if (currentState() == State::empty) return;
    start = clampPoint(start);
    end = clampPoint(end);
    if (end <= start + 1.0)
    {
        clearLoop();
        return;
    }
    loopStart.store(start, std::memory_order_relaxed);
    loopEnd.store(end, std::memory_order_relaxed);
    loopActive.store(active, std::memory_order_relaxed);
}
}
