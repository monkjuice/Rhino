#include "DjDeck.h"
#include <algorithm>
#include <cmath>

// The deck: its controls, what any thread may ask it, and what it plays -
// material taken up, faded from and let go of. The transport is in
// DjDeckTransport.cpp, the loops in DjDeckLoops.cpp and the player in
// DjDeckRender.cpp. See DjDeck.h.

namespace rhino
{
void DjDeckControls::reset() noexcept
{
    copyFrom(DjDeckControls {});
}

void DjDeckControls::copyFrom(const DjDeckControls& other) noexcept
{
    constexpr auto order = std::memory_order_relaxed;
    tempoPercent.store(other.tempoPercent.load(order), order);
    tempoRange.store(other.tempoRange.load(order), order);
    nudgePercent.store(other.nudgePercent.load(order), order);
    synced.store(other.synced.load(order), order);
    reversed.store(other.reversed.load(order), order);
    scratching.store(other.scratching.load(order), order);
    scratchRate.store(other.scratchRate.load(order), order);
    brakeSeconds.store(other.brakeSeconds.load(order), order);
    quantiseSnap.store(other.quantiseSnap.load(order), order);
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

double DjDeck::tempoBpm() const noexcept
{
    return nativeBpm() * tempoRate.load(std::memory_order_relaxed);
}

double DjDeck::effectiveBpm() const noexcept
{
    return nativeBpm() * playbackRate.load(std::memory_order_relaxed);
}

double DjDeck::baseRate() const noexcept
{
    const auto percent = controls.tempoPercent.load(std::memory_order_relaxed) + controls.nudgePercent.load(std::memory_order_relaxed);
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

double DjDeck::clampPoint(double frame) const noexcept
{
    return std::clamp(frame, 0.0, static_cast<double>(std::max(0, grid.length)));
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
    return at >= 0.0 ? at : pos;
}

bool DjDeck::steady() const noexcept
{
    return !controls.reversed.load(std::memory_order_relaxed) && !controls.scratching.load(std::memory_order_relaxed)
        && !braking && !spinningUp;
}

double DjDeck::stepNow() const noexcept
{
    const auto direction = controls.reversed.load(std::memory_order_relaxed) ? -1.0 : 1.0;
    return direction * rate * grid.sampleRate / std::max(1.0, outputRate);
}

void DjDeck::prepare(int maximumBlock, double deviceRate)
{
    const auto frames = static_cast<size_t>(std::max(1, maximumBlock));
    planPos.assign(frames, 0.0);
    planFromAt.assign(frames, 0.0);
    planUnderAt.assign(frames, 0.0);
    planGain.assign(frames, 0.0f);
    planFromWeight.assign(frames, 0.0f);
    planUnderWeight.assign(frames, 0.0f);
    planFrom.assign(frames, nullptr);
    planUnder.assign(frames, nullptr);
    planMark.assign(frames, 0);
    if (deviceRate > 0.0)
        outputRate = deviceRate;
    declickFrames = std::max(1, static_cast<int>(std::lround(declickSeconds * outputRate)));
}

void DjDeck::letGo(const DjTrack* material) noexcept
{
    if (material == nullptr || material == current || material == fade.material || material == under.material
        || releases == nullptr)
        return;
    releases->release(DjObjectKind::material, material);
}

void DjDeck::beginFade(const DjTrack* from, double at, double step, int frames)
{
    if (fade.weight.fresh())
    {
        fade.weight.start(std::max(fade.weight.total, frames));
        return;
    }
    const auto* dropped = under.material;
    under = fade.weight.active() ? fade : Fade {};
    fade.material = from;
    fade.at = at;
    fade.step = step;
    fade.weight.start(frames);
    letGo(dropped);
}

void DjDeck::dropFade()
{
    const auto* faded = fade.material;
    const auto* beneath = under.material;
    fade = {};
    under = {};
    letGo(faded);
    if (beneath != faded)
        letGo(beneath);
}

// One frame of the fades, written into the plan: the material each reads,
// where, and its weight, which reaches exactly nothing on its last frame -
// the frame the material is let go of, to go back once the block's reads are
// done. The fade under the newest is heard only through it, so it goes when
// that one ends, whatever its own ramp has left.
void DjDeck::planFadeFrame(int frame) noexcept
{
    const auto f = static_cast<size_t>(frame);
    if (!fade.weight.active())
    {
        planFromWeight[f] = 0.0f;
        return;
    }
    planFrom[f] = fade.material;
    planFromAt[f] = fade.at;
    planFromWeight[f] = fade.weight.next();
    fade.at += fade.step;
    if (under.weight.active())
    {
        planUnder[f] = under.material;
        planUnderAt[f] = under.at;
        planUnderWeight[f] = under.weight.next();
        under.at += under.step;
        if (!under.weight.active())
        {
            const auto* ended = under.material;
            under = {};
            letGo(ended);
        }
    }
    else
        planUnderWeight[f] = 0.0f;
    if (!fade.weight.active())
        dropFade();
}

void DjDeck::jumpTo(double frame)
{
    if (loopActive.load(std::memory_order_relaxed)
        && (frame < loopStart.load(std::memory_order_relaxed) || frame >= loopEnd.load(std::memory_order_relaxed)))
        loopActive.store(false, std::memory_order_relaxed);
    moveTo(frame);
}

void DjDeck::moveTo(double frame)
{
    if (audible() && current != nullptr)
        beginFade(current, pos, stepNow(), declickFrames);
    pos = frame;
    publish();
    pendingMark |= jumped;
    noteJump();
}

// Material from the message thread, through the engine's queue. Everything
// about the material let go of that matters was copied into `grid` when it
// was taken up: that is what converts a position, the cues and the loop to
// the new material. A deck that is heard goes on reading the old for the
// declick, at the rate it had, while the new fades in.
void DjDeck::setSource(const DjTrack* next, bool keepBeat)
{
    if (next == current) return;
    const auto previous = grid;
    const auto keep = keepBeat && previous.valid();
    const auto* outgoing = current;
    if (outgoing != nullptr && audible())
        beginFade(outgoing, pos, stepNow(), declickFrames);
    else if (outgoing == nullptr)
    {
        // An emptied deck still fading its last material out takes the new
        // from silence.
        dropFade();
        gain = 0.0f;
    }
    current = next;
    grid = gridOf(next);
    track.store(next, std::memory_order_release);
    letGo(outgoing);
    // A tempo held since the deck took over as master is a rate of the
    // material it was held on; a new track plays at its own fader's.
    if (!keep)
    {
        heldTempo = 0.0;
        following = false;
        heldTempoRate.store(0.0, std::memory_order_relaxed);
    }
    const auto clearLoopPoints = [this]
    {
        loopStart.store(-1.0, std::memory_order_relaxed);
        loopEnd.store(-1.0, std::memory_order_relaxed);
        loopActive.store(false, std::memory_order_relaxed);
    };
    if (next == nullptr)
    {
        // Emptied. What it played fades out at the gain it was heard at.
        setState(State::empty);
        setPending(Pending::none);
        pos = 0.0;
        pendingPosition.store(-1.0, std::memory_order_relaxed);
        cuePoint.store(0.0, std::memory_order_relaxed);
        for (auto& cue : hotCues) cue.store(-1.0, std::memory_order_relaxed);
        clearLoopPoints();
        stopping = false;
        returnTo = -1.0;
        startAt = jumpAt = -1;
        cueHeld = braking = spinningUp = false;
        if (!fade.weight.active())
            gain = 0.0f;
        publish();
        return;
    }
    const auto wasEmpty = outgoing == nullptr;
    // A deck looping its whole material loops the new whole too, and a point
    // past the new one's end comes round by its length rather than stopping
    // at its last frame: a cell swapped in under a song comes in on the beat
    // the deck was on.
    const auto start = loopStart.load(std::memory_order_relaxed), end = loopEnd.load(std::memory_order_relaxed);
    const auto loopedWhole = !wasEmpty && keep && loopActive.load(std::memory_order_relaxed) && start <= 0.5
                          && end >= previous.length - 0.5;
    // Frames of the old material to frames of the new: by beat when both
    // have a grid and the swap asked for it, by time otherwise.
    const auto converted = [&](double frame)
    {
        auto at = keep && grid.valid() ? grid.frameAt(previous.beatAt(frame))
                                       : frame * grid.sampleRate / std::max(1.0, previous.sampleRate);
        if (loopedWhole && at >= grid.length)
            at = std::fmod(at, static_cast<double>(grid.length));
        return at;
    };
    const auto convert = [&](double frame) { return frame < 0.0 ? frame : clampFrame(converted(frame)); };
    pos = wasEmpty ? 0.0 : convert(pos);
    pendingPosition.store(convert(pendingPosition.load(std::memory_order_relaxed)), std::memory_order_relaxed);
    cuePoint.store(wasEmpty ? 0.0 : convert(cuePoint.load(std::memory_order_relaxed)), std::memory_order_relaxed);
    for (auto& cue : hotCues)
        cue.store(wasEmpty ? -1.0 : convert(cue.load(std::memory_order_relaxed)), std::memory_order_relaxed);
    if (loopedWhole)
    {
        loopStart.store(0.0, std::memory_order_relaxed);
        loopEnd.store(static_cast<double>(grid.length), std::memory_order_relaxed);
    }
    else if (!wasEmpty && start >= 0.0 && end > start)
    {
        const auto newStart = clampPoint(converted(start)), newEnd = clampPoint(converted(end));
        if (newEnd > newStart + 1.0)
        {
            loopStart.store(newStart, std::memory_order_relaxed);
            loopEnd.store(newEnd, std::memory_order_relaxed);
        }
        else
            clearLoopPoints();
    }
    else
        clearLoopPoints();
    if (wasEmpty)
        setState(State::stopped);
    publish();
}

void DjDeck::reset()
{
    const auto* played = current;
    const auto* faded = fade.material;
    const auto* beneath = under.material;
    current = nullptr;
    fade = {};
    under = {};
    grid = {};
    track.store(nullptr, std::memory_order_release);
    letGo(played);
    if (faded != played)
        letGo(faded);
    if (beneath != played && beneath != faded)
        letGo(beneath);
    pos = 0.0;
    targetRate = rate = 1.0;
    gain = 0.0f;
    stopping = false;
    returnTo = -1.0;
    startAt = jumpAt = -1;
    jumpTarget = 0.0;
    cueHeld = braking = spinningUp = false;
    brakeStep = 0.0;
    tempo = 1.0;
    heldTempo = 0.0;
    following = false;
    pendingMark = 0;
    setState(State::empty);
    setPending(Pending::none);
    pendingPosition.store(-1.0, std::memory_order_relaxed);
    cuePoint.store(0.0, std::memory_order_relaxed);
    for (auto& cue : hotCues) cue.store(-1.0, std::memory_order_relaxed);
    loopStart.store(-1.0, std::memory_order_relaxed);
    loopEnd.store(-1.0, std::memory_order_relaxed);
    loopActive.store(false, std::memory_order_relaxed);
    playbackRate.store(1.0, std::memory_order_relaxed);
    tempoRate.store(1.0, std::memory_order_relaxed);
    heldTempoRate.store(0.0, std::memory_order_relaxed);
    peak.store(0.0f, std::memory_order_relaxed);
    publish();
    noteJump();
}
}
