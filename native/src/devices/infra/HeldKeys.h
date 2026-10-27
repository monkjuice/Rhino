#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <bitset>
#include <cstdint>

namespace rhino
{
// Which keys a stream of MIDI is holding down, kept for another thread to read:
// the audio thread hears every message as it passes, and the message thread
// takes the keys once a frame. A key struck and let go between two reads is
// reported by the next one all the same, so a note shorter than a frame is
// still seen. Notes are counted on any channel; two channels holding one key
// count as one key. Lock-free: each half of the 128 keys is one atomic word.
class HeldKeys
{
public:
    // Audio thread.
    void hear(const juce::MidiMessage& message) noexcept
    {
        if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            releaseAll();
            return;
        }
        if (!message.isNoteOnOrOff())
            return;
        const auto key = message.getNoteNumber();
        auto& word = down[static_cast<size_t>(key >> 6)];
        const auto bit = std::uint64_t {1} << (key & 63);
        if (message.isNoteOn())
        {
            word.fetch_or(bit, std::memory_order_relaxed);
            struck[static_cast<size_t>(key >> 6)].fetch_or(bit, std::memory_order_relaxed);
        }
        else
        {
            word.fetch_and(~bit, std::memory_order_relaxed);
        }
    }
    // Any thread: every key let go, as a panic or an all-notes-off lets them go.
    void releaseAll() noexcept
    {
        for (auto& word : down)
            word.store(0, std::memory_order_relaxed);
    }
    // Message thread: the keys down now and those struck since the last take.
    std::bitset<128> take() noexcept
    {
        std::bitset<128> keys;
        for (size_t half = 0; half < down.size(); ++half)
        {
            const auto word = down[half].load(std::memory_order_relaxed)
                            | struck[half].exchange(0, std::memory_order_relaxed);
            for (int bit = 0; bit < 64; ++bit)
                if ((word >> bit) & 1u)
                    keys.set(half * 64 + static_cast<size_t>(bit));
        }
        return keys;
    }

private:
    std::array<std::atomic<std::uint64_t>, 2> down {}, struck {};
};
}
