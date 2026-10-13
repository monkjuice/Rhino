#pragma once
#include <array>
#include <atomic>
#include <cstdint>

namespace rhino
{
// What the booth's audio thread reads and the message thread owns: a deck's
// material today; the live chain and its note list will join it.
enum class DjObjectKind : std::uint8_t { material = 1 };

// The way back for what the audio thread has let go of for good - material
// faded out, superseded or left in a slot that was emptied - so the message
// thread frees exactly what it is handed and nothing on a guess of how many
// blocks a reader might still need.
//
// The audio thread lets go of an object (`release`) as it stops reading it,
// and hands what it let go of over at the end of its block (`flush`), so
// nothing read in a block goes back before the block's last read. The ring
// the message thread takes them from (`take`) holds 64; when it is full the
// rest wait in a backlog and go on a later block. The backlog holds what a
// block can let go of with nobody collecting - every queued command a swap,
// and each deck its material and what it fades from - and should that ever
// fill, an object is counted and leaked rather than freed under a reader.
class DjReleaseQueue
{
public:
    static constexpr int ringSize = 64;
    static constexpr int backlogSize = 320;
    struct Entry
    {
        DjObjectKind kind = DjObjectKind::material;
        const void* object = nullptr;
    };

    // ---- audio thread ----
    void release(DjObjectKind, const void*) noexcept;
    void flush() noexcept;

    // ---- message thread ----
    bool take(Entry&) noexcept;
    // Objects a full backlog had no room for: leaked, never freed.
    int lost() const noexcept { return overflowed.load(std::memory_order_relaxed); }

private:
    std::array<Entry, ringSize> ring {};
    // Counted on, never wrapped: the ring is full when they are a ring apart.
    std::atomic<std::uint32_t> written {0}, taken {0};
    std::array<Entry, backlogSize> backlog {};
    int waiting = 0;
    std::atomic<int> overflowed {0};
};
}
