#include "DjDeck.h"
#include <algorithm>
#include <cmath>

// The deck's player: the plan, where the material is read frame by frame and
// how loud, and the read, which reads it there. See DjDeck.h.

namespace rhino
{
namespace
{
struct Frame
{
    float left = 0.0f, right = 0.0f;
};

// Four-point Hermite, as the Drum Rack's sample voices read, bit-exact for an
// untuned track at the device's own rate.
inline float hermite(float x0, float x1, float x2, float x3, float fraction) noexcept
{
    const auto c1 = 0.5f * (x2 - x0);
    const auto c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
    const auto c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
    return ((c3 * fraction + c2) * fraction + c1) * fraction + x1;
}

// Both channels from one index and fraction. The taps are clamped to the
// material only near its ends, where reading past them holds the end sample.
inline Frame readFrame(const DjTrack& material, double at) noexcept
{
    const auto count = material.length();
    // Material with no audio - a MIDI clip's notes, played through the
    // track's own instrument - reads as silence of its length.
    if (count == 0 || material.left.empty()) return {};
    const auto index = static_cast<int>(std::floor(at));
    const auto fraction = static_cast<float>(at - index);
    const auto& left = material.left;
    const auto& right = material.stereo() ? material.right : material.left;
    if (index >= 1 && index + 2 < count)
    {
        const auto i = static_cast<size_t>(index);
        return {hermite(left[i - 1], left[i], left[i + 1], left[i + 2], fraction),
                hermite(right[i - 1], right[i], right[i + 1], right[i + 2], fraction)};
    }
    const auto tap = [count](const std::vector<float>& samples, int i) { return samples[static_cast<size_t>(std::clamp(i, 0, count - 1))]; };
    return {hermite(tap(left, index - 1), tap(left, index), tap(left, index + 1), tap(left, index + 2), fraction),
            hermite(tap(right, index - 1), tap(right, index), tap(right, index + 1), tap(right, index + 2), fraction)};
}

constexpr double fadeSeconds = 0.002;
constexpr double rateSmoothingSeconds = 0.03;
}

// Every rule of the transport, frame by frame: a start landing where the
// engine timed it, a jump landing likewise, the rate smoothed or wound by the
// brake, the fades in and out, the loop's wrap and the end of the material.
// What it writes is the whole of what `read` needs.
void DjDeck::plan(int frames, double deviceRate)
{
    frames = std::clamp(frames, 0, capacity());
    if (deviceRate > 0.0)
        outputRate = deviceRate;
    const auto firstMark = pendingMark;
    pendingMark = 0;
    if (current == nullptr || grid.length <= 0 || deviceRate <= 0.0)
    {
        // Nothing to read, but what an emptied deck fades out at the gain it
        // was heard at.
        for (int f = 0; f < frames; ++f)
        {
            const auto i = static_cast<size_t>(f);
            planPos[i] = 0.0;
            planMark[i] = f == 0 ? firstMark : std::uint8_t {0};
            planGain[i] = fade.weight.active() ? gain : 0.0f;
            planFadeFrame(f);
        }
        if (!fade.weight.active())
            gain = 0.0f;
        playbackRate.store(targetRate, std::memory_order_relaxed);
        return;
    }
    auto s = currentState();
    auto playing = s == State::playing || s == State::cueing;
    if (!playing && startAt < 0 && !fade.weight.active())
    {
        std::fill(planPos.begin(), planPos.begin() + frames, pos);
        std::fill(planGain.begin(), planGain.begin() + frames, 0.0f);
        std::fill(planFromWeight.begin(), planFromWeight.begin() + frames, 0.0f);
        std::fill(planMark.begin(), planMark.begin() + frames, std::uint8_t {0});
        if (frames > 0) planMark[0] = firstMark;
        rate = targetRate;
        playbackRate.store(rate, std::memory_order_relaxed);
        return;
    }

    const auto fadeStep = static_cast<float>(1.0 / std::max(1.0, fadeSeconds * deviceRate));
    const auto smoothing = 1.0 - std::exp(-1.0 / (rateSmoothingSeconds * deviceRate));
    const auto rateScale = grid.sampleRate / deviceRate;
    const auto backwards = controls.reversed.load(std::memory_order_relaxed);
    const auto end = static_cast<double>(grid.length);
    // The loop, read once a block: only this thread writes it, and a landing
    // outside it, which leaves it, is read again.
    double lStart = 0.0, lEnd = 0.0;
    bool looping = false;
    const auto readLoop = [&]
    {
        lStart = loopStart.load(std::memory_order_relaxed);
        lEnd = loopEnd.load(std::memory_order_relaxed);
        looping = loopActive.load(std::memory_order_relaxed) && lStart >= 0.0 && lEnd > lStart + 1.0;
    };
    readLoop();
    std::uint8_t nextMark = firstMark;
    bool stoppedThisBlock = false;
    for (int f = 0; f < frames; ++f)
    {
        const auto i = static_cast<size_t>(f);
        planMark[i] = nextMark;
        nextMark = 0;
        if (!playing)
        {
            if (f == startAt)
            {
                playing = true;
                gain = 0.0f;
                stopping = false;
                braking = false;
                returnTo = -1.0;
                startAt = -1;
                const auto brake = controls.brakeSeconds.load(std::memory_order_relaxed);
                if (brake > 0.01f && !cueHeld)
                {
                    // Winds up from a standstill over the brake time.
                    spinningUp = true;
                    rate = 0.0;
                    brakeStep = std::max(0.05, std::abs(targetRate)) / (brake * deviceRate);
                }
                setState(cueHeld ? State::cueing : State::playing);
                setPending(Pending::none);
                pendingPosition.store(-1.0, std::memory_order_relaxed);
                planMark[i] |= started;
            }
            else
            {
                planPos[i] = pos;
                planGain[i] = 0.0f;
                planFadeFrame(f);
                continue;
            }
        }
        if (f == jumpAt)
        {
            // The jump the engine timed, at exactly this frame.
            jumpAt = -1;
            jumpTo(jumpTarget);
            setPending(Pending::none);
            pendingPosition.store(-1.0, std::memory_order_relaxed);
            planMark[i] |= static_cast<std::uint8_t>(pendingMark | landed);
            pendingMark = 0;
            readLoop();
        }
        if (braking)
        {
            // Winding down; the stop itself follows at nothing.
            rate = rate > 0.0 ? std::max(0.0, rate - brakeStep * (grid.sampleRate / deviceRate))
                              : std::min(0.0, rate + brakeStep * (grid.sampleRate / deviceRate));
            if (std::abs(rate) <= 1.0e-4)
            {
                braking = false;
                stopping = true;
                returnTo = -1.0;
            }
        }
        else if (spinningUp)
        {
            rate += brakeStep;
            if (rate >= targetRate)
            {
                rate = targetRate;
                spinningUp = false;
            }
        }
        else
            rate += smoothing * (targetRate - rate);
        const auto step = (backwards ? -1.0 : 1.0) * rate * rateScale;
        planPos[i] = pos;
        if (stopping)
            gain = std::max(0.0f, gain - fadeStep);
        else
            gain = std::min(1.0f, gain + fadeStep);
        planGain[i] = gain;
        planFadeFrame(f);
        pos += step;

        // The loop's wrap: a modulo, so a position far past the loop comes
        // straight back into it rather than a loop length a frame. What lies
        // past the loop end fades out while the loop start fades in, over
        // the declick or the loop, whichever is shorter - counted in floating
        // point first: at a crawl, a whole track's loop is more frames than
        // an int holds.
        if (looping && ((step > 0.0 && pos >= lEnd) || (step < 0.0 && pos < lStart)))
        {
            const auto length = lEnd - lStart;
            const auto loopFrames = length / std::max(1.0e-9, std::abs(step));
            beginFade(current, pos, step, std::max(1, static_cast<int>(std::min(loopFrames, static_cast<double>(declickFrames)))));
            pos = step > 0.0 ? lStart + std::fmod(pos - lStart, length) : lEnd - std::fmod(lStart - pos, length);
            nextMark |= wrapped;
            noteJump();
        }
        // The end of the material, or its top played backwards: the deck
        // stops through its fade, reading the end sample held.
        if (!stopping && (pos >= end || pos < 0.0))
        {
            stopping = true;
            returnTo = -1.0;
        }
        if (stopping && gain <= 0.0f)
        {
            playing = false;
            stopping = false;
            braking = false;
            spinningUp = false;
            cueHeld = false;
            // A jump timed for later in the block goes with the pending state
            // the stop clears below.
            jumpAt = -1;
            pos = std::clamp(pos, 0.0, end);
            dropFade();
            if (returnTo >= 0.0)
            {
                jumpTo(clampFrame(returnTo));
                nextMark |= pendingMark;
                pendingMark = 0;
            }
            returnTo = -1.0;
            stoppedThisBlock = true;
            setState(State::stopped);
            setPending(Pending::none);
            pendingPosition.store(-1.0, std::memory_order_relaxed);
            for (int rest = f + 1; rest < frames; ++rest)
            {
                const auto r = static_cast<size_t>(rest);
                planPos[r] = pos;
                planGain[r] = 0.0f;
                planFromWeight[r] = 0.0f;
                planMark[r] = nextMark;
                nextMark = 0;
            }
            break;
        }
    }
    if (!playing && !stoppedThisBlock && startAt >= frames)
    {
        // A start past the end of this block waits for the next one.
        startAt -= frames;
    }
    if (jumpAt >= frames)
        jumpAt -= frames;
    // A wrap after the block's last frame begins the next block's stretch.
    pendingMark |= nextMark;
    publish();
    playbackRate.store(rate, std::memory_order_relaxed);
}

// The material where the plan says, crossfaded from what the deck was
// playing - itself crossfaded from the fade under it, when two overlap -
// through the transport's gain.
void DjDeck::read(float* left, float* right, int frames)
{
    frames = std::clamp(frames, 0, capacity());
    float blockPeak = 0.0f;
    for (int f = 0; f < frames; ++f)
    {
        const auto i = static_cast<size_t>(f);
        const auto g = planGain[i];
        if (g <= 0.0f)
        {
            left[f] = 0.0f;
            right[f] = 0.0f;
            continue;
        }
        auto frame = current != nullptr ? readFrame(*current, planPos[i]) : Frame {};
        const auto w = planFromWeight[i];
        if (w > 0.0f)
        {
            auto from = readFrame(*planFrom[i], planFromAt[i]);
            if (const auto u = planUnderWeight[i]; u > 0.0f)
            {
                const auto beneath = readFrame(*planUnder[i], planUnderAt[i]);
                from.left = from.left * (1.0f - u) + beneath.left * u;
                from.right = from.right * (1.0f - u) + beneath.right * u;
            }
            frame.left = frame.left * (1.0f - w) + from.left * w;
            frame.right = frame.right * (1.0f - w) + from.right * w;
        }
        left[f] = frame.left * g;
        right[f] = frame.right * g;
        blockPeak = std::max(blockPeak, std::max(std::abs(left[f]), std::abs(right[f])));
    }
    peak.store(blockPeak, std::memory_order_relaxed);
}
}
