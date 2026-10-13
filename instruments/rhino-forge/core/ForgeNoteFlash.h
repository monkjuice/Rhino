#pragma once

#include <array>
#include <atomic>

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
}
