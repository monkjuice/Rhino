#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>

// Binding a controller's knobs and pads to Forge's own controls.
//
// A plugin cannot tell a knob from a key. Both arrive as ordinary MIDI, and the
// only thing separating the eight pots on a Launchkey from its pitch strip is
// the controller number they happen to send — which is firmware, and differs
// between two units with the same name on the box. So there is no table of
// known hardware here and no device detection: a binding is learned. The panel
// arms a control, the next thing the controller moves drives it. That is what
// makes this work with a keyboard nobody has tested it against, and it is why
// the eight-knobs-to-eight-macros fit that prompted the feature is a default
// written on top rather than the mechanism underneath.
//
// Two threads reach this. The audio thread asks, of every MIDI message it
// receives, whether that message is bound, and it has to be answered without
// locking or allocating — which is why the bindings are a flat array of atomics
// indexed by channel and number rather than a map. The message thread makes and
// breaks bindings and is the only thread that ever writes a parameter, so
// nothing here touches one: `target` answers with an index and leaves the
// caller to decide what that means.
namespace rhino::forge
{
// A pad and a key are the same message, so there are only two kinds of thing a
// binding can come from. Pitch bend is deliberately not one of them: it is
// already wired to the pitch wheel everywhere, it is the one controller that
// springs back on release, and a binding learned from it would be learned by
// accident on nearly every keyboard the moment the strip was brushed.
enum class MidiSourceKind { controller, note };

struct MidiSource
{
    MidiSourceKind kind = MidiSourceKind::controller;
    int channel = 1;  // 1-16, as MIDI itself numbers them
    int number = 0;   // the controller number, or the note

    bool valid() const noexcept
    {
        return channel >= 1 && channel <= 16 && number >= 0 && number <= 127;
    }

    bool operator== (const MidiSource& other) const noexcept
    {
        return kind == other.kind && channel == other.channel && number == other.number;
    }

    bool operator!= (const MidiSource& other) const noexcept { return !(*this == other); }

    // What the panel calls it. The channel is named only when it is not the
    // first: most controllers send everything on channel 1, and printing it on
    // every line would be a column of the same number telling nobody anything.
    // A pad is named by its note rather than its number because that is what
    // the keyboard itself prints, where it prints anything at all.
    juce::String label() const
    {
        const auto body = kind == MidiSourceKind::controller
            ? "CC " + juce::String(number)
            : "Note " + juce::MidiMessage::getMidiNoteName(number, true, true, 3);
        return channel == 1 ? body : body + " ch " + juce::String(channel);
    }
};

class MidiMap
{
public:
    static constexpr int channelCount = 16;
    static constexpr int numberCount = 128;
    static constexpr int kindCount = 2;
    // No parameter has this index, so it is what an unbound source answers and
    // what arming answers when nothing is armed.
    static constexpr int unbound = -1;

    MidiMap() { clear(); }

    // Audio thread. The parameter index this source drives, or `unbound`.
    int target(const MidiSource& source) const noexcept
    {
        if (!source.valid()) return unbound;
        return bindings[slot(source)].load(std::memory_order_relaxed);
    }

    bool isBound(const MidiSource& source) const noexcept { return target(source) != unbound; }

    // Message thread. Points a source at a parameter, replacing whatever that
    // source drove before. A source drives one parameter and one only: a knob
    // moving two controls at once is a mixer, and the second binding is far
    // more likely to be a slip than an intention.
    void bind(const MidiSource& source, int parameterIndex)
    {
        if (!source.valid() || parameterIndex < 0) return;
        bindings[slot(source)].store(parameterIndex, std::memory_order_relaxed);
    }

    void forget(const MidiSource& source)
    {
        if (!source.valid()) return;
        bindings[slot(source)].store(unbound, std::memory_order_relaxed);
    }

    // Everything pointed at this parameter is dropped. The panel offers this
    // per control, so it has to undo a control bound from two knobs as readily
    // as one bound from a single knob.
    void forgetParameter(int parameterIndex)
    {
        if (parameterIndex < 0) return;
        for (auto& binding : bindings)
            if (binding.load(std::memory_order_relaxed) == parameterIndex)
                binding.store(unbound, std::memory_order_relaxed);
    }

    // What drives this parameter, or an invalid source if nothing does. Where
    // more than one thing does, the first in slot order answers — the panel
    // uses this to print one name beside a control, and a control bound twice
    // has to print something rather than nothing.
    MidiSource sourceFor(int parameterIndex) const
    {
        if (parameterIndex >= 0)
            for (size_t index = 0; index < bindings.size(); ++index)
                if (bindings[index].load(std::memory_order_relaxed) == parameterIndex)
                    return sourceAt(index);
        return { MidiSourceKind::controller, 0, -1 };
    }

    int boundCount() const
    {
        auto found = 0;
        for (const auto& binding : bindings)
            if (binding.load(std::memory_order_relaxed) != unbound) ++found;
        return found;
    }

    void clear()
    {
        for (auto& binding : bindings) binding.store(unbound, std::memory_order_relaxed);
        armed.store(unbound, std::memory_order_relaxed);
    }

    // Every binding there is, as (source, parameter index). Used to write them
    // out and to list them; the order is slot order, which is stable, so a file
    // written twice from the same bindings is the same file.
    template <typename Fn>
    void forEachBinding(Fn&& visit) const
    {
        for (size_t index = 0; index < bindings.size(); ++index)
        {
            const auto parameterIndex = bindings[index].load(std::memory_order_relaxed);
            if (parameterIndex != unbound) visit(sourceAt(index), parameterIndex);
        }
    }

    // Learning. The panel arms a parameter and the next source the controller
    // moves is bound to it. The arming has to be visible to the audio thread
    // because the MIDI stream exists only inside processBlock — the panel
    // cannot watch it for itself, so it leaves a note asking to be told.
    void arm(int parameterIndex)
    {
        armedAt.store(noticed.load(std::memory_order_relaxed), std::memory_order_relaxed);
        armed.store(parameterIndex, std::memory_order_relaxed);
    }

    void cancelLearn() { armed.store(unbound, std::memory_order_relaxed); }
    bool isLearning() const noexcept { return armed.load(std::memory_order_relaxed) != unbound; }
    int learningParameter() const noexcept { return armed.load(std::memory_order_relaxed); }

    // Audio thread. Records what just arrived so a pending learn can pick it
    // up. Two stores, and only while something is armed.
    void notice(const MidiSource& source) noexcept
    {
        if (!source.valid()) return;
        lastSource.store(encode(source), std::memory_order_relaxed);
        // Released after the source, so a reader that sees the new count is
        // guaranteed to see the source that came with it.
        noticed.fetch_add(1, std::memory_order_release);
    }

    // Message thread. Completes a pending learn if the controller has moved
    // since it was armed, and says what was bound. Answers false while armed
    // and waiting, which is the state the panel draws as "listening".
    bool completeLearn(MidiSource& learned)
    {
        const auto parameterIndex = armed.load(std::memory_order_relaxed);
        if (parameterIndex == unbound) return false;
        if (noticed.load(std::memory_order_acquire) == armedAt.load(std::memory_order_relaxed))
            return false;

        learned = decode(lastSource.load(std::memory_order_relaxed));
        if (!learned.valid()) return false;
        // Learning replaces, at both ends.
        //
        // The source is moved rather than shared, because the knob you just
        // turned is the one you meant and leaving its old binding in place would
        // make one knob do two things without saying so. And the control lets go
        // of whatever used to drive it, because a control with two masters jumps
        // when either one moves and there is no way to tell from the panel which
        // of them did it. Both are what the menu offers when it says the learn
        // replaces what is already there.
        forget(learned);
        forgetParameter(parameterIndex);
        bind(learned, parameterIndex);
        armed.store(unbound, std::memory_order_relaxed);
        return true;
    }

private:
    static size_t slot(const MidiSource& source)
    {
        const auto kind = source.kind == MidiSourceKind::note ? 1 : 0;
        return static_cast<size_t>((kind * channelCount + (source.channel - 1)) * numberCount
                                   + source.number);
    }

    static MidiSource sourceAt(size_t index)
    {
        const auto number = static_cast<int>(index % numberCount);
        const auto rest = static_cast<int>(index / numberCount);
        return { rest >= channelCount ? MidiSourceKind::note : MidiSourceKind::controller,
                 (rest % channelCount) + 1, number };
    }

    // One int, so the audio thread can publish what it saw in a single store.
    static int encode(const MidiSource& source)
    {
        return (source.kind == MidiSourceKind::note ? 1 << 16 : 0)
             | (source.channel << 8) | source.number;
    }

    static MidiSource decode(int encoded)
    {
        if (encoded < 0) return { MidiSourceKind::controller, 0, -1 };
        return { (encoded & (1 << 16)) != 0 ? MidiSourceKind::note : MidiSourceKind::controller,
                 (encoded >> 8) & 0xFF, encoded & 0xFF };
    }

    std::array<std::atomic<int>, kindCount * channelCount * numberCount> bindings;
    std::atomic<int> armed {unbound};
    // What the audio thread last saw, and how many things it has seen. The
    // count is what tells a learn that the controller has moved: a knob nudged
    // back to a value it already sent is still a knob that moved, and comparing
    // the sources alone would miss it.
    std::atomic<int> lastSource {-1};
    std::atomic<unsigned int> noticed {0};
    std::atomic<unsigned int> armedAt {0};
};

// One bound message on its way from the audio thread to the message thread.
//
// The value cannot be applied where it arrives. Writing a parameter means
// telling the host about it, which takes locks and can allocate, and the audio
// thread may do neither — so the message is carried across and applied on the
// thread that is allowed to apply it. The crossing also coalesces nothing and
// drops nothing: a pad tapped and released between two ticks of the timer is
// still two events when the timer gets there, which is the whole reason this is
// a queue rather than a published last-value.
struct MidiControlEvent
{
    MidiSource source;
    int value = 0;        // the controller's value, or the note's velocity
    bool release = false; // a note-off; a controller never sets this
};

// Single producer, single consumer, fixed size, no allocation. The audio thread
// pushes and the message thread pops, and nothing else touches either end.
class MidiControlQueue
{
public:
    // Deep enough that a timer tick has to be missed entirely before anything
    // is lost: eight knobs streaming 7-bit values fill well under a hundred
    // slots in the sixtieth of a second between drains.
    static constexpr unsigned int capacity = 256;

    // Audio thread. False when the queue is full, which is a dropped event
    // rather than a stall — a controller value that never arrives is corrected
    // by the next one the knob sends.
    bool push(const MidiControlEvent& event) noexcept
    {
        const auto write = writeIndex.load(std::memory_order_relaxed);
        const auto next = (write + 1) % capacity;
        if (next == readIndex.load(std::memory_order_acquire)) return false;
        events[write] = event;
        writeIndex.store(next, std::memory_order_release);
        return true;
    }

    // Message thread.
    bool pop(MidiControlEvent& event) noexcept
    {
        const auto read = readIndex.load(std::memory_order_relaxed);
        if (read == writeIndex.load(std::memory_order_acquire)) return false;
        event = events[read];
        readIndex.store((read + 1) % capacity, std::memory_order_release);
        return true;
    }

private:
    std::array<MidiControlEvent, capacity> events {};
    std::atomic<unsigned int> writeIndex {0};
    std::atomic<unsigned int> readIndex {0};
};
}
