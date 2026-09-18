#pragma once

#include "ForgeCore.h"

#include <atomic>
#include <memory>
#include <vector>

// Who owns an oscillator's table, and how one is handed to the audio thread
// without a lock and without freeing something that is still being read.
//
// M9b-1 left `Oscillator::table` a raw pointer that was null everywhere, so
// nothing could dangle yet. This is where it becomes real: the message thread
// draws on a WavetableEdit, builds a new Wavetable out of it, and publishes it
// while a note may be sounding through the old one.
namespace rhino::forge
{
// --- Why the hand-over is safe -----------------------------------------------
//
// One atomic per oscillator carries the table the audio thread should read. The
// audio thread loads it once per block, into the Patch, and uses that one
// pointer for the whole block — so a pointer it loaded during a block can only
// be in use until that block ends.
//
// The trouble is the other half: the message thread has to know when the old
// table has stopped being read before it frees it. A mutex is not available
// (the audio thread cannot wait), and freeing on the spot is exactly the bug.
//
// So the audio thread counts its own blocks. `guard` is incremented on the way
// into a block and again on the way out, which makes it odd exactly while a
// block is running. After publishing, the message thread reads it once:
//
//   * **Even** — no block is running. Any block that starts from here on loads
//     the pointer *after* the store that has already happened, so it cannot
//     reach the old table. Nothing holds it and it is freed immediately. This
//     is the case in a suspended plugin or between notes in a stopped host,
//     which is most editing, so most edits free straight away.
//   * **Odd, at some value g** — one block is running and may be holding the
//     old table. It will finish when the guard reaches g + 1. The old table is
//     parked until then and freed at the next publish or collect.
//
// Both the publish and the guard read are sequentially consistent, and so are
// the audio thread's two increments and its load. That single total order is
// what the argument above rests on: a weaker ordering allows the store and the
// guard read to be seen in the other order, which is precisely the case that
// would free a table out from under a running block.
//
// Nothing on the audio thread allocates, frees, waits or branches on any of
// this — it is two increments and a load per block.

class WavetableStore
{
public:
    WavetableStore()
    {
        for (int osc = 0; osc < oscillatorCount; ++osc) resetToBuiltIn(osc);
    }

    // --- Message thread -------------------------------------------------------

    // The frames, to draw on. Every change has to be followed by publish() or
    // publishFrame(), or the voice goes on reading the table as it was.
    WavetableEdit& edit(int osc) noexcept { return slots[index(osc)].authored; }
    const WavetableEdit& edit(int osc) const noexcept { return slots[index(osc)].authored; }

    // Back to the ten built-in shapes. The frames are taken from the built-in
    // table rather than regenerated, so the editor starts from exactly what the
    // oscillator has been playing all along.
    void resetToBuiltIn(int osc)
    {
        const auto& source = builtInWavetable();
        std::vector<float> samples(static_cast<size_t>(source.frameCount()) * wavetableFrameSize);
        std::vector<juce::String> names;
        names.reserve(static_cast<size_t>(source.frameCount()));
        for (int frame = 0; frame < source.frameCount(); ++frame)
        {
            names.push_back(source.frameTitle(frame));
            std::copy_n(source.frameData(frame), wavetableFrameSize,
                        samples.begin() + static_cast<ptrdiff_t>(frame) * wavetableFrameSize);
        }
        edit(osc).setFrames(samples.data(), source.frameCount(), source.title(), std::move(names), true);
        publish(osc);
    }

    // Rebuild every frame and hand the result over. What a preset load, an
    // import, or adding and removing frames needs.
    void publish(int osc) { hand(index(osc), edit(osc).build()); }

    // Rebuild one frame and hand the result over: the same table with one frame
    // re-transformed and the rest copied. This is the path a stroke of the pen
    // takes, and it is what makes drawing audible while the hand is still
    // moving rather than only when it stops.
    void publishFrame(int osc, int frame)
    {
        auto& slot = slots[index(osc)];
        if (slot.live == nullptr || slot.live->frameCount() != edit(osc).frameCount()) { publish(osc); return; }
        hand(index(osc), slot.live->withFrame(frame, edit(osc).frame(frame)));
    }

    // Free anything the audio thread has since moved past. Publishing does this
    // too; the editor calls it on its timer so a table retired by the last edit
    // of a session does not sit there until the next one.
    void collect()
    {
        const auto now = guard.load(std::memory_order_seq_cst);
        for (auto& slot : slots)
            slot.retired.erase(std::remove_if(slot.retired.begin(), slot.retired.end(),
                                              [now] (const Retired& entry) { return now >= entry.safeAt; }),
                               slot.retired.end());
    }

    // Bumped by every change, so an editor that was not the one making the
    // change — a preset arriving from the host, the other editor of the same
    // plugin — still notices and redraws.
    int revision(int osc) const noexcept
    {
        return slots[index(osc)].revision.load(std::memory_order_relaxed);
    }

    // --- Readable from any thread --------------------------------------------
    //
    // A parameter's value formatter runs on whichever thread asks it — the
    // message thread for Forge's own readout, a host thread for an automation
    // lane — so what POSITION needs to name a frame is published rather than
    // read off the table.

    int frameCount(int osc) const noexcept
    {
        return slots[index(osc)].publishedFrames.load(std::memory_order_relaxed);
    }

    bool isBuiltIn(int osc) const noexcept
    {
        return slots[index(osc)].publishedBuiltIn.load(std::memory_order_relaxed);
    }

    // --- Audio thread ---------------------------------------------------------

    // Brackets a block. Every read of table() has to sit between these, which is
    // what the retirement rule above is counting.
    void beginBlock() noexcept { guard.fetch_add(1, std::memory_order_seq_cst); }
    void endBlock() noexcept { guard.fetch_add(1, std::memory_order_seq_cst); }

    const Wavetable* table(int osc) const noexcept
    {
        return slots[index(osc)].published.load(std::memory_order_seq_cst);
    }

    // So a block cannot return without closing its bracket.
    struct ScopedBlock
    {
        explicit ScopedBlock(WavetableStore& s) : store(s) { store.beginBlock(); }
        ~ScopedBlock() { store.endBlock(); }
        ScopedBlock(const ScopedBlock&) = delete;
        ScopedBlock& operator= (const ScopedBlock&) = delete;
        WavetableStore& store;
    };

private:
    struct Retired
    {
        std::unique_ptr<Wavetable> table;
        std::uint64_t safeAt = 0;  // the guard value at which the last reader has finished
    };

    struct Slot
    {
        WavetableEdit authored;
        std::unique_ptr<Wavetable> live;
        std::atomic<const Wavetable*> published {nullptr};
        std::vector<Retired> retired;
        std::atomic<int> revision {0};
        std::atomic<int> publishedFrames {waveShapeCount};
        std::atomic<bool> publishedBuiltIn {true};
    };

    static int index(int osc) noexcept { return juce::jlimit(0, oscillatorCount - 1, osc); }

    void hand(int osc, std::unique_ptr<Wavetable> next)
    {
        auto& slot = slots[static_cast<size_t>(osc)];
        if (next == nullptr) return;
        auto* raw = next.get();
        auto previous = std::move(slot.live);
        slot.live = std::move(next);
        slot.published.store(raw, std::memory_order_seq_cst);
        slot.publishedFrames.store(raw->frameCount(), std::memory_order_relaxed);
        slot.publishedBuiltIn.store(slot.authored.isUntouched(), std::memory_order_relaxed);
        slot.revision.fetch_add(1, std::memory_order_relaxed);

        // Read after the store, never before: see the reasoning above.
        const auto g = guard.load(std::memory_order_seq_cst);
        if (previous != nullptr && (g & 1u) != 0u)
            slot.retired.push_back({std::move(previous), g + 1});
        collect();
    }

    std::array<Slot, oscillatorCount> slots;
    std::atomic<std::uint64_t> guard {0};
};

// What POSITION reads out. On the built-in ten that is the shape it has landed
// on, or the pair it sits between, because "where is the saw?" is the question
// a wavetable knob is being asked. On a table somebody drew there are no shape
// names to give, so the frames are numbered instead.
inline juce::String positionLabel(int frames, bool builtIn, float position)
{
    if (builtIn) return waveLabel(position);
    if (frames <= 1) return "1 / 1";
    int frame = 0;
    auto blend = 0.0f;
    wavetableFrameAt(frames, position, frame, blend);
    const auto nearest = blend <= 0.5f ? frame : frame + 1;
    return juce::String(nearest + 1) + " / " + juce::String(frames);
}
}
