#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace rhino
{
// A note on its way into a track, stamped with the feed frame it falls on:
// relative to the block it was pushed with, then to the feed as a whole once
// it is in the ring, and relative to the pop that takes it out.
struct DjPortEvent
{
    std::int64_t frame = 0;
    int note = 60, velocity = 0;
    bool on = false;
};

// The way a deck is heard through a track of the song. The booth puts the
// deck's audio and its notes in at the feed; the hidden device at the head
// of the track's chain takes them out and plays them into the chain; the
// hidden device at the chain's end puts the chain's output into the tap; and
// the booth takes that out into the deck's channel strip. So the deck is
// heard through the track's live devices, and every knob on them is heard at
// once, with no render in between.
//
// Each ring has one writer and one reader, each on the audio device's
// thread - the booth's callback on one side, the song's graph on the other -
// and neither waits for the other. In steady state one block is in flight
// each way, which is a block of latency on a track deck; the booth delays its
// file decks by the same block so that the two line up. Events ride beside
// the audio in a ring of their own, each stamped with the feed frame it falls
// on. A reader a block or two behind skips to the freshest block, dropping
// the note-ons it skipped and keeping their note-offs. A feed that had no
// room for a block - the graph was not running - has lost that block's notes,
// note-offs among them, so the reader starts again from what comes next and
// says so, and the device at the track lets go of every note it played.
//
// Sized once by prepare, on the message thread while nothing renders; the
// audio thread allocates nothing here.
class DjTrackPort
{
public:
    static constexpr int maximumEvents = 1024;

    // Sizes both rings for four blocks of this size. Never while rendering.
    void prepare(int maximumBlock);

    // ---- the booth's side ----
    // This block's audio for the track and the notes the deck passed in it,
    // each with its frame within the block. Null audio pushes silence.
    void pushFeed(const float* left, const float* right, int frames, const DjPortEvent* events, int eventCount) noexcept;
    // What the chain gave back, zero-filled where nothing has come. Returns
    // how many frames came from the chain.
    int popTap(float* left, float* right, int frames) noexcept;

    // ---- the track's side ----
    // The next `frames` of the deck's audio, zero-filled where the feed is
    // short, and the events due by the end of them, each stamped with its
    // frame within this pop. Returns how many frames came from the feed.
    // `resynced` is set when the feed had lost blocks and was emptied: what
    // the track was playing from it is to be let go of.
    int popFeed(float* left, float* right, int frames, DjPortEvent* events, int maximumCount, int& eventCount,
                bool& resynced) noexcept;
    // The chain's output for these frames.
    void pushTap(const float* left, const float* right, int frames) noexcept;

    // Whether a deck plays through this port now. The session sets it; the
    // devices read it once a block and pass audio straight through when it
    // is off.
    std::atomic<bool> bound {false};

    // How many frames each ring holds, for the tests.
    int feedFramesWaiting() const noexcept { return feed.available(); }
    int tapFramesWaiting() const noexcept { return tap.available(); }

private:
    struct Ring
    {
        std::vector<float> left, right;
        std::atomic<std::int64_t> written {0}, read {0};
        int capacity = 0;
        void prepare(int frames);
        int available() const noexcept;
        int room() const noexcept;
        // The caller has checked the room, or what is available.
        void push(const float* l, const float* r, int frames) noexcept;
        void pop(float* l, float* r, int frames) noexcept;
        void skip(int frames) noexcept;
    };
    Ring feed, tap;
    // The size of the block last pushed into the feed: one block is what a
    // reader finds waiting in steady state, and anything beyond it is a
    // backlog it skips.
    std::atomic<int> feedBlock {0};
    std::array<DjPortEvent, maximumEvents> events {};
    std::atomic<int> eventHead {0}, eventTail {0};
    // The writer found no room, for a block or for its notes: the reader
    // starts again from what comes after.
    std::atomic<bool> overflowed {false};
};

// Delays audio by exactly one block, whatever the block's size: what the
// booth does to a file deck so that it lines up with a track deck, whose
// audio comes back from the track's chain a block later.
class DjBlockDelay
{
public:
    void prepare(int maximumBlock);
    void reset() noexcept;
    // In place: what comes out is the block that went in last time, or
    // silence when that was another size or there was none.
    void process(float* left, float* right, int frames) noexcept;

private:
    std::array<std::vector<float>, 2> kept;
    int keptFrames = 0;
};
}
