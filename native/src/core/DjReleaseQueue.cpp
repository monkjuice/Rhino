#include "DjReleaseQueue.h"
#include <algorithm>

// The booth's way back for what its audio thread let go of: see
// DjReleaseQueue.h.

namespace rhino
{
void DjReleaseQueue::release(DjObjectKind kind, const void* object) noexcept
{
    if (object == nullptr) return;
    if (waiting == backlogSize)
    {
        overflowed.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    backlog[static_cast<size_t>(waiting++)] = {kind, object};
}

void DjReleaseQueue::flush() noexcept
{
    int sent = 0;
    auto at = written.load(std::memory_order_relaxed);
    const auto readUpTo = taken.load(std::memory_order_acquire);
    while (sent < waiting && at - readUpTo < static_cast<std::uint32_t>(ringSize))
        ring[at++ % ringSize] = backlog[static_cast<size_t>(sent++)];
    if (sent == 0) return;
    written.store(at, std::memory_order_release);
    std::move(backlog.begin() + sent, backlog.begin() + waiting, backlog.begin());
    waiting -= sent;
}

bool DjReleaseQueue::take(Entry& entry) noexcept
{
    const auto at = taken.load(std::memory_order_relaxed);
    if (at == written.load(std::memory_order_acquire))
        return false;
    entry = ring[at % ringSize];
    taken.store(at + 1, std::memory_order_release);
    return true;
}
}
