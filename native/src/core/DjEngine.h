#pragma once
#include "DjDeck.h"
#include "DjMixer.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace rhino
{
// The DJ booth: up to six decks into a mixer, rendered as one source.
//
// Pure C++ in RhinoCore, as the Drum Rack's engine is: a test can load a
// deck, press play, process a few blocks and measure what came out. The
// session wraps it in an audio callback of its own, beside the edit's, so
// every deck has a transport of its own and none of them is the song's.
//
// Commands cross from the message thread in a single-producer queue and are
// applied at the start of a block; what a deck is doing is read back from
// its atomics. A track is handed to a deck by pointer and freed only once
// no block can still be reading it, counted the way the Drum Rack counts
// its samples out.
// Where the live preview's notes go: the session, which plays them into the
// track's own instrument. Called from the audio thread, so an implementation
// takes no lock it could wait on and allocates nothing.
struct DjLiveSink
{
    virtual ~DjLiveSink() = default;
    virtual void noteOn(int deck, int note, int velocity) = 0;
    virtual void noteOff(int deck, int note) = 0;
};

class DjEngine
{
public:
    static constexpr int maximumDecks = 6;
    static constexpr int queueSize = 256;

    // When a start or a jump lands: at once, or on the master deck's next
    // beat, bar or four bars - the launch quantisation Live has, optional.
    enum class Quantise : int { off = 0, beat, bar, fourBars };
    static int boundaryBeats(Quantise, int beatsPerBar);

    struct Command
    {
        enum class Type : int
        {
            play, pause, togglePlay, cueDown, cueUp, hotCue, clearHotCue, setCue, setHotCue,
            loopIn, loopOut, beatLoop, reloopExit, loopHalve, loopDouble, clearLoop, setLoop,
            beatJump, seek, syncAlign, setMaster, stopAll
        };
        Type type = Type::play;
        int deck = -1;
        double value = 0.0, value2 = 0.0;
        int count = 0;
        bool flag = false;
    };

    DjEngine();
    ~DjEngine();

    // ---- message thread ----
    // Never while rendering.
    void prepare(double sampleRate, int maximumBlock);
    double sampleRate() const noexcept { return rate; }
    void setDeckCount(int count);
    int deckCount() const noexcept { return decks.load(std::memory_order_relaxed); }
    // The deck plays this from its next block. keepBeat carries the position,
    // cues and loop across by beat, for a track bounced again.
    void setTrack(int deck, std::unique_ptr<DjTrack>, bool keepBeat);
    const DjTrack* trackOf(int deck) const noexcept;
    // Frees the tracks decks have let go of that no block can still read.
    // Returns how many are still waiting.
    int collect();
    bool push(const Command&) noexcept;
    int pendingCommands() const noexcept;

    // ---- any thread ----
    DjDeck& deck(int index) noexcept { return deckList[static_cast<size_t>(index)]; }
    const DjDeck& deck(int index) const noexcept { return deckList[static_cast<size_t>(index)]; }
    DjChannelStrip& channel(int index) noexcept { return channels[static_cast<size_t>(index)]; }
    const DjChannelStrip& channel(int index) const noexcept { return channels[static_cast<size_t>(index)]; }
    DjCrossfader& crossfader() noexcept { return fader; }
    DjMasterSection& master() noexcept { return masterSection; }
    DjMicSection& mic() noexcept { return micSection; }
    DjSendFx& sendFx() noexcept { return sendEffect; }
    DjBeatFx& fx() noexcept { return effect; }

    std::atomic<int> quantise {static_cast<int>(Quantise::bar)};
    std::atomic<int> beatsPerBar {4};
    // The deck the others sync to: the one chosen, else the first playing.
    std::atomic<int> masterDeck {-1};
    // The tempo everything is timed to, from the master deck, or 0 for none.
    std::atomic<double> masterBpm {0.0};
    // The tempo tapped in, which times the effect when no deck plays.
    std::atomic<double> tapBpm {128.0};
    // Phase lock: a synced deck is nudged back onto the master's beat when
    // it drifts, rather than only aligned when sync was pressed.
    std::atomic<bool> phaseLock {true};
    // The live preview's sink, set once before the engine renders and
    // outliving it. A deck with livePreview set is silenced here and its
    // material's notes go to the sink instead.
    DjLiveSink* liveSink = nullptr;

    // ---- audio thread ----
    // Clears the outputs and writes the master to channels 0 and 1 and the
    // headphones to 2 and 3 when there are that many. The first input, when
    // there is one, is the mic.
    void process(const float* const* inputs, int inputChannels, float* const* outputs, int outputChannels, int frames);
    void process(float* const* outputs, int outputChannels, int frames) { process(nullptr, 0, outputs, outputChannels, frames); }
    std::uint64_t blocksBegun() const noexcept { return begun.load(std::memory_order_acquire); }

private:
    struct Retired
    {
        std::unique_ptr<DjTrack> track;
        std::uint64_t after = 0;
    };
    void applyCommands();
    void apply(const Command&);
    void chooseMaster();
    void setRates();
    void landPendings(int frames, double deviceRate);
    void renderChunk(const float* micInput, float* const* outputs, int outputChannels, int frames);
    bool quantised() const noexcept { return quantise.load(std::memory_order_relaxed) != static_cast<int>(Quantise::off); }
    // The live preview's sequencer for one deck, run once a block.
    void sequence(int deck, bool active, int frames);
    void releaseSounding(int deck);
    struct Sequencer
    {
        double lastBeat = 0.0;
        std::uint32_t lastJumps = 0;
        bool wasActive = false;
        std::array<bool, 128> sounding {};
    };
    std::array<Sequencer, maximumDecks> sequencers;

    double rate = 48000.0;
    int maximumBlock = 512;
    std::atomic<int> decks {0};
    std::array<DjDeck, maximumDecks> deckList;
    std::array<DjChannelStrip, maximumDecks> channels;
    DjCrossfader fader;
    DjMasterSection masterSection;
    DjMicSection micSection;
    DjSendFx sendEffect;
    DjBeatFx effect;
    std::atomic<int> requestedMaster {-1};
    // A running count of master beats for the effect's sweeps, kept moving
    // from the clock when nothing plays.
    double effectBeat = 0.0;

    std::array<Command, queueSize> queue;
    std::atomic<int> head {0}, tail {0};
    std::atomic<std::uint64_t> begun {0}, ended {0};
    std::array<std::unique_ptr<DjTrack>, maximumDecks> owned;
    std::vector<Retired> retired;

    std::array<std::array<std::vector<float>, 2>, maximumDecks> scratch;
    std::array<std::vector<float>, 2> masterBus, cueBus, sendBus;
    std::vector<float> micBuffer;
};
}
