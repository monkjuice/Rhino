#pragma once

#include <array>
#include <atomic>
#include <cstdint>

// A note the voices have just been told to play, handed from the audio thread
// to the panel so the ARP plate can draw it falling.
//
// A queue rather than a flag. The panel reads at 24 Hz and a fast arp starts
// notes far more often than that, so a single "last note" slot would show one
// note of every chord and lose the rest. It is the same single-producer,
// single-consumer ring MidiControlQueue is: the audio thread pushes, the
// message thread pops, and a full ring drops the note rather than waiting —
// a spark that never appears costs nothing, where a stall on the audio thread
// costs a click. Nothing here allocates or locks.
namespace rhino::forge
{
struct NoteFlash
{
    int note = 0;
    float velocity = 0.0f;
};

class NoteFlashQueue
{
public:
    // A sixteen-key chord at the fastest division is well under this between
    // two ticks of the panel's timer. Past it, the notes lost are the ones the
    // eye would not have separated anyway.
    static constexpr unsigned int capacity = 64;

    // Audio thread. False when the ring is full, which is a dropped spark
    // rather than a stall.
    bool push(const NoteFlash& flash) noexcept
    {
        const auto write = writeIndex.load(std::memory_order_relaxed);
        const auto next = (write + 1) % capacity;
        if (next == readIndex.load(std::memory_order_acquire)) return false;
        flashes[write] = flash;
        writeIndex.store(next, std::memory_order_release);
        return true;
    }

    // Message thread.
    bool pop(NoteFlash& flash) noexcept
    {
        const auto read = readIndex.load(std::memory_order_relaxed);
        if (read == writeIndex.load(std::memory_order_acquire)) return false;
        flash = flashes[read];
        readIndex.store((read + 1) % capacity, std::memory_order_release);
        return true;
    }

private:
    std::array<NoteFlash, capacity> flashes {};
    std::atomic<unsigned int> writeIndex {0};
    std::atomic<unsigned int> readIndex {0};
};

// Which notes the voices are holding right now: told to play and not yet told
// to stop. The queue says when a note started; this says whether it is still
// down, which is what keeps a held note lit on the plate rather than letting it
// fall away under a finger that has not moved.
//
// A set rather than releases sent down the queue. A queue that is full drops
// what it is handed, and a dropped release would leave a light hanging in the
// well for good; a set cannot fall behind, because every start and stop writes
// it directly. The audio thread writes a bit before it pushes the note's flash,
// so a reader that pops the flash and then asks here never finds a note it has
// just been told about already let go.
class HeldNotes
{
public:
    // Audio thread.
    void set(int note, bool held) noexcept
    {
        if (note < 0 || note > 127) return;
        auto& word = words[static_cast<size_t>(note >> 6)];
        const auto bit = std::uint64_t {1} << (note & 63);
        if (held) word.fetch_or(bit, std::memory_order_release);
        else word.fetch_and(~bit, std::memory_order_release);
    }

    // Audio thread, or prepareToPlay while no block is running.
    void clear() noexcept
    {
        for (auto& word : words) word.store(0, std::memory_order_release);
    }

    // Message thread.
    bool isHeld(int note) const noexcept
    {
        if (note < 0 || note > 127) return false;
        const auto bits = words[static_cast<size_t>(note >> 6)].load(std::memory_order_acquire);
        return (bits >> (note & 63) & 1) != 0;
    }

private:
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    std::array<std::atomic<std::uint64_t>, 2> words {};
};
}
