#include "DjTrackPort.h"
#include <algorithm>

// The rings a deck's audio and notes cross into a track of the song by, and
// the chain's output crosses back by. See DjTrackPort.h.

namespace rhino
{
void DjTrackPort::Ring::prepare(int frames)
{
    capacity = std::max(16, frames);
    left.assign(static_cast<size_t>(capacity), 0.0f);
    right.assign(static_cast<size_t>(capacity), 0.0f);
    written.store(0, std::memory_order_relaxed);
    read.store(0, std::memory_order_relaxed);
}

int DjTrackPort::Ring::available() const noexcept
{
    return static_cast<int>(written.load(std::memory_order_acquire) - read.load(std::memory_order_acquire));
}

int DjTrackPort::Ring::room() const noexcept
{
    return capacity - available();
}

void DjTrackPort::Ring::push(const float* l, const float* r, int frames) noexcept
{
    auto at = written.load(std::memory_order_relaxed);
    for (int f = 0; f < frames; ++f, ++at)
    {
        const auto i = static_cast<size_t>(at % capacity);
        left[i] = l != nullptr ? l[f] : 0.0f;
        right[i] = r != nullptr ? r[f] : (l != nullptr ? l[f] : 0.0f);
    }
    written.store(at, std::memory_order_release);
}

void DjTrackPort::Ring::pop(float* l, float* r, int frames) noexcept
{
    auto at = read.load(std::memory_order_relaxed);
    for (int f = 0; f < frames; ++f, ++at)
    {
        const auto i = static_cast<size_t>(at % capacity);
        if (l != nullptr) l[f] = left[i];
        if (r != nullptr) r[f] = right[i];
    }
    read.store(at, std::memory_order_release);
}

void DjTrackPort::Ring::skip(int frames) noexcept
{
    read.store(read.load(std::memory_order_relaxed) + frames, std::memory_order_release);
}

void DjTrackPort::prepare(int maximumBlock)
{
    feed.prepare(maximumBlock * 4);
    tap.prepare(maximumBlock * 4);
    feedBlock.store(0, std::memory_order_relaxed);
    eventHead.store(0, std::memory_order_relaxed);
    eventTail.store(0, std::memory_order_relaxed);
    overflowed.store(false, std::memory_order_relaxed);
}

void DjTrackPort::pushFeed(const float* left, const float* right, int frames, const DjPortEvent* blockEvents, int eventCount) noexcept
{
    if (frames <= 0) return;
    // No room means the chain is not taking anything: the block goes, and
    // its notes with it, rather than piling up behind a graph that is not
    // running - and the reader is told, since a note-off may be among them.
    if (feed.room() < frames)
    {
        overflowed.store(true, std::memory_order_release);
        return;
    }
    const auto start = feed.written.load(std::memory_order_relaxed);
    auto tail = eventTail.load(std::memory_order_relaxed);
    const auto head = eventHead.load(std::memory_order_acquire);
    for (int e = 0; e < eventCount; ++e)
    {
        const auto next = (tail + 1) % maximumEvents;
        if (next == head)
        {
            // A full ring drops the rest of this block's notes, a note-off
            // perhaps among them.
            overflowed.store(true, std::memory_order_release);
            break;
        }
        auto event = blockEvents[e];
        event.frame = start + std::clamp(event.frame, std::int64_t(0), std::int64_t(frames - 1));
        events[static_cast<size_t>(tail)] = event;
        tail = next;
    }
    // The audio goes in after its events, so a reader that sees the audio
    // sees the notes that fall in it.
    eventTail.store(tail, std::memory_order_release);
    feed.push(left, right, frames);
    feedBlock.store(frames, std::memory_order_relaxed);
}

int DjTrackPort::popFeed(float* left, float* right, int frames, DjPortEvent* out, int maximumCount, int& eventCount,
                         bool& resynced) noexcept
{
    eventCount = 0;
    resynced = false;
    if (frames <= 0) return 0;
    if (overflowed.exchange(false, std::memory_order_acq_rel))
    {
        // Blocks were lost while the reader was away. Nothing waiting can be
        // trusted to finish what it starts, so all of it goes, and the caller
        // lets go of every note it played; the next block is played whole.
        feed.skip(feed.available());
        eventHead.store(eventTail.load(std::memory_order_acquire), std::memory_order_release);
        resynced = true;
        for (int f = 0; f < frames; ++f)
        {
            if (left != nullptr) left[f] = 0.0f;
            if (right != nullptr) right[f] = 0.0f;
        }
        return 0;
    }
    // One block is what waits here in steady state; more is a backlog from
    // a graph that was not running, skipped so the chain plays the freshest.
    const auto block = std::max(frames, feedBlock.load(std::memory_order_relaxed));
    if (const auto waiting = feed.available(); waiting > block)
        feed.skip(waiting - block);
    const auto from = feed.read.load(std::memory_order_relaxed);
    const auto taken = std::min(frames, feed.available());
    feed.pop(left, right, taken);
    for (int f = taken; f < frames; ++f)
    {
        if (left != nullptr) left[f] = 0.0f;
        if (right != nullptr) right[f] = 0.0f;
    }
    // Every event due by the end of this pop, late ones at its first frame.
    // A note-on left behind by a skip would start a note nothing will end
    // at its time, so a late one is dropped; a late note-off ends a note
    // the chain may still hold.
    auto head = eventHead.load(std::memory_order_relaxed);
    const auto tail = eventTail.load(std::memory_order_acquire);
    const auto end = from + frames;
    while (head != tail)
    {
        const auto& event = events[static_cast<size_t>(head)];
        if (event.frame >= end) break;
        head = (head + 1) % maximumEvents;
        const auto late = event.frame < from;
        if (late && event.on) continue;
        if (eventCount >= maximumCount) continue;
        auto& placed = out[eventCount++];
        placed = event;
        placed.frame = late ? 0 : event.frame - from;
    }
    eventHead.store(head, std::memory_order_release);
    return taken;
}

void DjTrackPort::pushTap(const float* left, const float* right, int frames) noexcept
{
    if (frames <= 0 || tap.room() < frames) return;
    tap.push(left, right, frames);
}

int DjTrackPort::popTap(float* left, float* right, int frames) noexcept
{
    if (frames <= 0) return 0;
    if (const auto waiting = tap.available(); waiting > frames)
        tap.skip(waiting - frames);
    const auto taken = std::min(frames, tap.available());
    tap.pop(left, right, taken);
    for (int f = taken; f < frames; ++f)
    {
        if (left != nullptr) left[f] = 0.0f;
        if (right != nullptr) right[f] = 0.0f;
    }
    return taken;
}

void DjBlockDelay::prepare(int maximumBlock)
{
    for (auto& channel : kept)
        channel.assign(static_cast<size_t>(std::max(16, maximumBlock)), 0.0f);
    keptFrames = 0;
}

void DjBlockDelay::reset() noexcept
{
    keptFrames = 0;
}

void DjBlockDelay::process(float* left, float* right, int frames) noexcept
{
    frames = std::min(frames, static_cast<int>(kept[0].size()));
    const auto replay = keptFrames == frames;
    for (int f = 0; f < frames; ++f)
    {
        const auto i = static_cast<size_t>(f);
        const auto l = left[f], r = right[f];
        left[f] = replay ? kept[0][i] : 0.0f;
        right[f] = replay ? kept[1][i] : 0.0f;
        kept[0][i] = l;
        kept[1][i] = r;
    }
    keptFrames = frames;
}
}
