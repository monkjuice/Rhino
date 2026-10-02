#pragma once

#include "ForgeSample.h"
#include "ForgeVoiceParts.h"

#include <atomic>
#include <memory>
#include <vector>

// Who owns an oscillator's sample, and how one is handed to the audio thread
// without a lock and without freeing something that is still being read.
//
// This is WavetableStore's problem exactly, so it is WavetableStore's answer
// exactly — one atomic per oscillator carrying what the audio thread should
// read, and a parity guard the audio thread bumps on the way into and out of a
// block, which is odd precisely while a block is running. The full argument for
// why that is safe is written out in ForgeTableStore.h and is not repeated
// here; if you are changing this, read it there first.
//
// What differs is only what is being handed over and where it comes from: a
// table is drawn or imported and has an editable form the panel works on, while
// a sample is loaded and analysed and has none. So there is no `edit()` here
// and no `publishFrame()` — a sample arrives whole or not at all.
namespace rhino::forge
{
class SampleStore
{
public:
    // --- Message thread -------------------------------------------------------

    // Hand a newly analysed sample to an oscillator. Null clears it, which is
    // what a spectral oscillator with nothing loaded reads.
    void publish(int osc, std::unique_ptr<Sample> next)
    {
        auto& slot = slots[static_cast<size_t>(index(osc))];
        auto* raw = next != nullptr && !next->isEmpty() ? next.get() : nullptr;
        auto previous = std::move(slot.live);
        slot.live = std::move(next);
        slot.published.store(raw, std::memory_order_seq_cst);
        slot.publishedFrames.store(raw != nullptr ? raw->frameCount() : 0, std::memory_order_relaxed);
        slot.revision.fetch_add(1, std::memory_order_relaxed);

        // Read after the store, never before: see ForgeTableStore.h.
        const auto g = guard.load(std::memory_order_seq_cst);
        if (previous != nullptr && (g & 1u) != 0u)
            slot.retired.push_back({std::move(previous), g + 1});
        collect();
    }

    void clear(int osc) { publish(osc, nullptr); }

    // Free anything the audio thread has since moved past. Publishing does this
    // too; the editor calls it on its timer for the same reason it calls the
    // table store's.
    void collect()
    {
        const auto now = guard.load(std::memory_order_seq_cst);
        for (auto& slot : slots)
            slot.retired.erase(std::remove_if(slot.retired.begin(), slot.retired.end(),
                                              [now] (const Retired& entry) { return now >= entry.safeAt; }),
                               slot.retired.end());
    }

    int revision(int osc) const noexcept
    {
        return slots[static_cast<size_t>(index(osc))].revision.load(std::memory_order_relaxed);
    }

    // --- Readable from any thread --------------------------------------------

    int frameCount(int osc) const noexcept
    {
        return slots[static_cast<size_t>(index(osc))].publishedFrames.load(std::memory_order_relaxed);
    }

    // What the panel puts in the chooser, and what the preset stores so the
    // same sample can be found again. Held beside the sample rather than read
    // off it, because the formatter that wants it may run while a new one is
    // being published.
    juce::String sourceName(int osc) const
    {
        const juce::ScopedLock lock(namesLock);
        return names[static_cast<size_t>(index(osc))];
    }

    void setSourceName(int osc, juce::String name)
    {
        const juce::ScopedLock lock(namesLock);
        names[static_cast<size_t>(index(osc))] = std::move(name);
    }

    // --- Audio thread ---------------------------------------------------------

    void beginBlock() noexcept { guard.fetch_add(1, std::memory_order_seq_cst); }
    void endBlock() noexcept { guard.fetch_add(1, std::memory_order_seq_cst); }

    const Sample* sample(int osc) const noexcept
    {
        return slots[static_cast<size_t>(index(osc))].published.load(std::memory_order_seq_cst);
    }

    struct ScopedBlock
    {
        explicit ScopedBlock(SampleStore& s) : store(s) { store.beginBlock(); }
        ~ScopedBlock() { store.endBlock(); }
        ScopedBlock(const ScopedBlock&) = delete;
        ScopedBlock& operator= (const ScopedBlock&) = delete;
        SampleStore& store;
    };

private:
    struct Retired
    {
        std::unique_ptr<Sample> sample;
        std::uint64_t safeAt = 0;
    };

    struct Slot
    {
        std::unique_ptr<Sample> live;
        std::atomic<const Sample*> published {nullptr};
        std::vector<Retired> retired;
        std::atomic<int> revision {0};
        std::atomic<int> publishedFrames {0};
    };

    static int index(int osc) noexcept { return juce::jlimit(0, oscillatorCount - 1, osc); }

    std::array<Slot, oscillatorCount> slots;
    std::array<juce::String, oscillatorCount> names;
    mutable juce::CriticalSection namesLock;
    std::atomic<std::uint64_t> guard {0};
};
}
