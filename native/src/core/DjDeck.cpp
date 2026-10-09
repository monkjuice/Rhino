#include "DjDeck.h"
#include <algorithm>
#include <cmath>

// The deck player: see DjDeck.h.

namespace rhino
{
namespace
{
// Four-point Hermite, as the Drum Rack's sample voices read, bit-exact for an
// untuned track at the device's own rate.
inline float readInterpolated(const std::vector<float>& samples, double at) noexcept
{
    const auto count = static_cast<int>(samples.size());
    if (count == 0) return 0.0f;
    const auto index = static_cast<int>(std::floor(at));
    const auto fraction = static_cast<float>(at - index);
    const auto sample = [&samples, count](int i) { return samples[static_cast<size_t>(std::clamp(i, 0, count - 1))]; };
    const auto x0 = sample(index - 1), x1 = sample(index), x2 = sample(index + 1), x3 = sample(index + 2);
    const auto c1 = 0.5f * (x2 - x0);
    const auto c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
    const auto c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
    return ((c3 * fraction + c2) * fraction + c1) * fraction + x1;
}

constexpr double fadeSeconds = 0.002;
constexpr double rateSmoothingSeconds = 0.03;
}

DjDeck::DjDeck()
{
    for (auto& cue : hotCues)
        cue.store(-1.0, std::memory_order_relaxed);
}

DjDeck::Grid DjDeck::gridOf(const DjTrack* t) noexcept
{
    Grid g;
    if (t == nullptr) return g;
    g.sampleRate = t->sampleRate;
    g.framesPerBeat = t->framesPerBeat();
    g.firstBeatFrame = t->firstBeatFrame();
    g.length = t->length();
    return g;
}

double DjDeck::nativeBpm() const noexcept
{
    const auto* t = track.load(std::memory_order_acquire);
    return t != nullptr ? t->analysis.bpm : 0.0;
}

double DjDeck::effectiveBpm() const noexcept
{
    return nativeBpm() * playbackRate.load(std::memory_order_relaxed);
}

double DjDeck::baseRate() const noexcept
{
    const auto percent = tempoPercent.load(std::memory_order_relaxed) + nudgePercent.load(std::memory_order_relaxed);
    return std::max(0.0, 1.0 + percent / 100.0);
}

double DjDeck::beatPosition() const noexcept
{
    const auto* t = track.load(std::memory_order_acquire);
    return t != nullptr ? t->beatAtFrame(position.load(std::memory_order_relaxed)) : 0.0;
}

double DjDeck::clampFrame(double frame) const noexcept
{
    if (grid.length <= 0) return 0.0;
    return std::clamp(frame, 0.0, static_cast<double>(grid.length - 1));
}

double DjDeck::snapToGrid(double frame, double beats) const noexcept
{
    if (!grid.valid() || beats <= 0.0) return clampFrame(frame);
    const auto beat = grid.beatAt(frame);
    return clampFrame(grid.frameAt(std::round(beat / beats) * beats));
}

double DjDeck::pendingFrame() const noexcept
{
    const auto at = pendingPosition.load(std::memory_order_relaxed);
    return at >= 0.0 ? at : position.load(std::memory_order_relaxed);
}

// A new track from the message thread. The old one may already be retired,
// so everything about it that matters was copied into `grid` when it was
// adopted: that is what converts a position, the cues and the loop to beats
// of the new one when asked to.
void DjDeck::adopt(const DjTrack* next)
{
    const auto previous = grid;
    const auto keep = keepBeatOnSwap.load(std::memory_order_relaxed) && previous.valid();
    current = next;
    grid = gridOf(next);
    if (next == nullptr)
    {
        setState(State::empty);
        setPending(Pending::none);
        position.store(0.0, std::memory_order_relaxed);
        pendingPosition.store(-1.0, std::memory_order_relaxed);
        cuePoint.store(0.0, std::memory_order_relaxed);
        for (auto& cue : hotCues) cue.store(-1.0, std::memory_order_relaxed);
        loopStart.store(-1.0, std::memory_order_relaxed);
        loopEnd.store(-1.0, std::memory_order_relaxed);
        loopActive.store(false, std::memory_order_relaxed);
        stopping = false;
        startAt = -1;
        cueHeld = false;
        gain = 0.0f;
        return;
    }
    // Frames of the old track to frames of the new: by beat when both have
    // a grid and the swap asked for it, by time otherwise.
    const auto convert = [&](double frame)
    {
        if (frame < 0.0) return frame;
        if (keep && grid.valid())
            return clampFrame(grid.frameAt(previous.beatAt(frame)));
        return clampFrame(frame * grid.sampleRate / std::max(1.0, previous.sampleRate));
    };
    const auto wasEmpty = currentState() == State::empty;
    position.store(wasEmpty ? 0.0 : convert(position.load(std::memory_order_relaxed)), std::memory_order_relaxed);
    pendingPosition.store(convert(pendingPosition.load(std::memory_order_relaxed)), std::memory_order_relaxed);
    cuePoint.store(wasEmpty ? 0.0 : convert(cuePoint.load(std::memory_order_relaxed)), std::memory_order_relaxed);
    for (auto& cue : hotCues)
        cue.store(wasEmpty ? -1.0 : convert(cue.load(std::memory_order_relaxed)), std::memory_order_relaxed);
    const auto start = loopStart.load(std::memory_order_relaxed), end = loopEnd.load(std::memory_order_relaxed);
    if (!wasEmpty && start >= 0.0 && end > start)
    {
        loopStart.store(convert(start), std::memory_order_relaxed);
        loopEnd.store(convert(end), std::memory_order_relaxed);
    }
    else
    {
        loopStart.store(-1.0, std::memory_order_relaxed);
        loopEnd.store(-1.0, std::memory_order_relaxed);
        loopActive.store(false, std::memory_order_relaxed);
    }
    if (wasEmpty)
        setState(State::stopped);
}

void DjDeck::beginPlaying(double fromFrame)
{
    position.store(clampFrame(fromFrame), std::memory_order_relaxed);
    stopping = false;
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
        {
            position.store(clampFrame(returnToFrame), std::memory_order_relaxed);
            noteJump();
        }
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
    pendingPosition.store(position.load(std::memory_order_relaxed), std::memory_order_relaxed);
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
    const auto at = position.load(std::memory_order_relaxed);
    const auto cue = cuePoint.load(std::memory_order_relaxed);
    if (std::abs(at - cue) < 1.0)
    {
        cueHeld = true;
        beginPlaying(cue);
        setState(State::cueing);
        return;
    }
    cuePoint.store(quantise ? snapToGrid(at, 1.0) : at, std::memory_order_relaxed);
    position.store(cuePoint.load(std::memory_order_relaxed), std::memory_order_relaxed);
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
        const auto here = position.load(std::memory_order_relaxed);
        cue.store(quantise ? snapToGrid(here, 1.0) : here, std::memory_order_relaxed);
        return;
    }
    // Set already: go there and play. The engine decides when, so a playing
    // deck lands on the master's next beat when quantised.
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

void DjDeck::adoptPending()
{
    const auto* next = track.load(std::memory_order_acquire);
    if (next != current)
        adopt(next);
}

void DjDeck::loopIn(bool quantise)
{
    if (currentState() == State::empty) return;
    const auto here = position.load(std::memory_order_relaxed);
    loopStart.store(quantise ? snapToGrid(here, 1.0) : here, std::memory_order_relaxed);
    loopEnd.store(-1.0, std::memory_order_relaxed);
    loopActive.store(false, std::memory_order_relaxed);
}

void DjDeck::loopOut(bool quantise)
{
    const auto start = loopStart.load(std::memory_order_relaxed);
    if (currentState() == State::empty || start < 0.0) return;
    const auto here = position.load(std::memory_order_relaxed);
    const auto end = quantise ? snapToGrid(here, 1.0) : here;
    if (end <= start + 1.0) return;
    loopEnd.store(end, std::memory_order_relaxed);
    loopActive.store(true, std::memory_order_relaxed);
}

void DjDeck::beatLoop(double beats, bool quantise)
{
    if (currentState() == State::empty || beats <= 0.0) return;
    const auto here = position.load(std::memory_order_relaxed);
    double start = here, end;
    if (grid.valid())
    {
        // From the beat the deck is on - the beat's own start when
        // quantised - for the given number of beats.
        const auto beat = grid.beatAt(here);
        const auto from = quantise ? std::floor(beat) : beat;
        start = grid.frameAt(from);
        end = grid.frameAt(from + beats);
    }
    else
    {
        end = here + beats * 0.5 * grid.sampleRate;
    }
    start = clampFrame(start);
    end = std::min(end, static_cast<double>(std::max(1, grid.length)));
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
        position.store(start, std::memory_order_relaxed);
        noteJump();
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
    const auto doubled = std::min(start + (end - start) * 2.0, static_cast<double>(std::max(1, grid.length)));
    if (doubled <= end) return;
    loopEnd.store(doubled, std::memory_order_relaxed);
}

void DjDeck::clearLoop()
{
    loopStart.store(-1.0, std::memory_order_relaxed);
    loopEnd.store(-1.0, std::memory_order_relaxed);
    loopActive.store(false, std::memory_order_relaxed);
}

void DjDeck::setLoop(double start, double end, bool active)
{
    if (currentState() == State::empty) return;
    start = clampFrame(start);
    end = std::min(end, static_cast<double>(std::max(1, grid.length)));
    if (end <= start + 1.0)
    {
        clearLoop();
        return;
    }
    loopStart.store(start, std::memory_order_relaxed);
    loopEnd.store(end, std::memory_order_relaxed);
    loopActive.store(active, std::memory_order_relaxed);
}

void DjDeck::beatJump(int beats)
{
    if (currentState() == State::empty || beats == 0) return;
    const auto here = position.load(std::memory_order_relaxed);
    double target;
    if (grid.valid())
        target = grid.frameAt(std::round(grid.beatAt(here)) + beats);
    else
        target = here + beats * 0.5 * grid.sampleRate;
    target = clampFrame(target);
    const auto moved = target - here;
    // An active loop travels with the jump, as a CDJ moves one.
    const auto start = loopStart.load(std::memory_order_relaxed), end = loopEnd.load(std::memory_order_relaxed);
    if (loopActive.load(std::memory_order_relaxed) && start >= 0.0 && end > start)
    {
        const auto length = end - start;
        auto newStart = clampFrame(start + moved);
        auto newEnd = std::min(newStart + length, static_cast<double>(std::max(1, grid.length)));
        if (newEnd > newStart + 1.0)
        {
            loopStart.store(newStart, std::memory_order_relaxed);
            loopEnd.store(newEnd, std::memory_order_relaxed);
        }
    }
    position.store(target, std::memory_order_relaxed);
    if (currentState() == State::waiting)
        pendingPosition.store(target, std::memory_order_relaxed);
    noteJump();
}

void DjDeck::seek(double frame)
{
    if (currentState() == State::empty) return;
    const auto target = clampFrame(frame);
    position.store(target, std::memory_order_relaxed);
    if (currentState() == State::waiting)
        pendingPosition.store(target, std::memory_order_relaxed);
    noteJump();
}

void DjDeck::alignPhase(double masterPhase, int boundaryBeats)
{
    if (!grid.valid() || currentState() == State::empty) return;
    const auto span = std::max(1, boundaryBeats);
    const auto here = position.load(std::memory_order_relaxed);
    const auto beat = grid.beatAt(here);
    // The master's phase within its boundary, placed into this deck's
    // nearest boundary of the same size.
    const auto base = std::floor(beat / span) * span;
    auto target = base + masterPhase;
    if (target - beat > span * 0.5) target -= span;
    else if (beat - target > span * 0.5) target += span;
    position.store(clampFrame(grid.frameAt(target)), std::memory_order_relaxed);
    noteJump();
}

void DjDeck::landPending(int frameOffset, int snapBeats)
{
    const auto what = currentPending();
    if (what == Pending::none) return;
    auto at = pendingFrame();
    if (snapBeats > 0)
        at = snapToGrid(at, snapBeats);
    if (what == Pending::jump)
    {
        // Lands at the block's start: the few frames of difference are below
        // what a beat boundary can be heard to.
        position.store(clampFrame(at), std::memory_order_relaxed);
        setPending(Pending::none);
        pendingPosition.store(-1.0, std::memory_order_relaxed);
        noteJump();
        return;
    }
    position.store(clampFrame(at), std::memory_order_relaxed);
    startAt = std::max(0, frameOffset);
    noteJump();
}

void DjDeck::render(float* left, float* right, int frames, double deviceRate)
{
    adoptPending();
    const auto clear = [left, right, frames]
    {
        std::fill(left, left + frames, 0.0f);
        if (right != nullptr) std::fill(right, right + frames, 0.0f);
    };
    if (current == nullptr || grid.length <= 0 || deviceRate <= 0.0)
    {
        clear();
        peak.store(0.0f, std::memory_order_relaxed);
        playbackRate.store(targetRate, std::memory_order_relaxed);
        return;
    }
    auto s = currentState();
    auto playing = s == State::playing || s == State::cueing;
    if (!playing && startAt < 0)
    {
        clear();
        peak.store(0.0f, std::memory_order_relaxed);
        rate = targetRate;
        playbackRate.store(rate, std::memory_order_relaxed);
        return;
    }

    const auto fadeStep = static_cast<float>(1.0 / std::max(1.0, fadeSeconds * deviceRate));
    const auto smoothing = 1.0 - std::exp(-1.0 / (rateSmoothingSeconds * deviceRate));
    const auto rateScale = grid.sampleRate / deviceRate;
    const auto backwards = reversed.load(std::memory_order_relaxed);
    auto pos = position.load(std::memory_order_relaxed);
    const auto end = static_cast<double>(grid.length);
    float blockPeak = 0.0f;
    const auto& leftSamples = current->left;
    const auto& rightSamples = current->stereo() ? current->right : current->left;
    bool stoppedThisBlock = false;

    for (int f = 0; f < frames; ++f)
    {
        if (!playing)
        {
            if (f == startAt)
            {
                playing = true;
                gain = 0.0f;
                stopping = false;
                returnTo = -1.0;
                startAt = -1;
                setState(cueHeld ? State::cueing : State::playing);
                setPending(Pending::none);
                pendingPosition.store(-1.0, std::memory_order_relaxed);
            }
            else
            {
                left[f] = 0.0f;
                if (right != nullptr) right[f] = 0.0f;
                continue;
            }
        }
        rate += smoothing * (targetRate - rate);
        const auto step = (backwards ? -1.0 : 1.0) * rate * rateScale;
        const auto l = readInterpolated(leftSamples, pos);
        const auto r = readInterpolated(rightSamples, pos);
        if (stopping)
            gain = std::max(0.0f, gain - fadeStep);
        else
            gain = std::min(1.0f, gain + fadeStep);
        left[f] = l * gain;
        if (right != nullptr) right[f] = r * gain;
        blockPeak = std::max(blockPeak, std::max(std::abs(left[f]), std::abs(r * gain)));
        pos += step;

        // The loop, and the ends of the track.
        const auto lStart = loopStart.load(std::memory_order_relaxed), lEnd = loopEnd.load(std::memory_order_relaxed);
        if (loopActive.load(std::memory_order_relaxed) && lStart >= 0.0 && lEnd > lStart + 1.0)
        {
            const auto length = lEnd - lStart;
            if (step > 0.0 && pos >= lEnd)
            {
                pos -= length;
                noteJump();
            }
            else if (step < 0.0 && pos < lStart)
            {
                pos += length;
                noteJump();
            }
        }
        if (pos >= end - 1.0 || pos < 0.0)
        {
            pos = pos < 0.0 ? 0.0 : end - 1.0;
            stopping = true;
            returnTo = -1.0;
            gain = 0.0f;
        }
        if (stopping && gain <= 0.0f)
        {
            playing = false;
            stopping = false;
            cueHeld = false;
            if (returnTo >= 0.0)
            {
                pos = clampFrame(returnTo);
                noteJump();
            }
            returnTo = -1.0;
            stoppedThisBlock = true;
            setState(State::stopped);
            setPending(Pending::none);
            pendingPosition.store(-1.0, std::memory_order_relaxed);
            for (int rest = f + 1; rest < frames; ++rest)
            {
                left[rest] = 0.0f;
                if (right != nullptr) right[rest] = 0.0f;
            }
            break;
        }
    }
    if (!playing && !stoppedThisBlock && startAt >= frames)
    {
        // A start past the end of this block waits for the next one.
        startAt -= frames;
    }
    position.store(pos, std::memory_order_relaxed);
    playbackRate.store(rate, std::memory_order_relaxed);
    peak.store(blockPeak, std::memory_order_relaxed);
}
}
