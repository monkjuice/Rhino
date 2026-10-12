#pragma once
#include "DjDeck.h"
#include "DjMixer.h"
#include "DjReleaseQueue.h"
#include "DjTrackPort.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace rhino
{
// What a deck plays, handed to it whole in one command so that no block sees
// half of a change. The engine reads what it points at; the message thread
// owns it until the engine hands it back.
struct DjDeckSource
{
    const DjTrack* material = nullptr;
    // Carries the position, cues and loop across by beat, for material made
    // again from the same clip.
    bool keepBeat = false;
};

// The DJ booth: up to six decks into a mixer, rendered as one source.
//
// Pure C++ in RhinoCore, as the Drum Rack's engine is: a test can load a
// deck, press play, process a few blocks and measure what came out. The
// session wraps it in an audio callback of its own, beside the edit's, so
// every deck has a transport of its own and none of them is the song's.
//
// Each deck lives in a slot that is its own for as long as it is on the
// console, whatever its place there: the session keeps the console's order,
// so taking a deck off moves nothing the engine plays. Commands cross from
// the message thread in a single-producer queue, every one of them - the
// material a deck plays included - applied at the start of a block for every
// slot before anything is decided, so a block sees one consistent material
// and position. What a deck is doing is read back from its atomics.
//
// A deck playing a track of the song is heard through the track itself: its
// slot's port (DjTrackPort) carries the deck's audio and its notes into the
// track's chain and the chain's output back into the deck's channel strip,
// so the track's live devices are what the deck sounds like. A file deck is
// delayed by the block that round trip takes, so the two kinds line up.
//
// Material is handed over by pointer and owned by the message thread here
// until the audio thread hands it back through a ring, which it does only
// once it will never read it again - its fade ended, superseded, its slot
// emptied. collect() frees exactly what came back, so nothing is freed on a
// guess of how long a reader might need it.
class DjEngine
{
public:
    static constexpr int maximumDecks = 6;
    static constexpr int queueSize = 256;
    // The most notes a deck passes in one block.
    static constexpr int maximumBlockEvents = 256;

    // When a start or a jump lands: at once, or in phase with the master
    // deck's beat, bar or four bars - the launch quantisation Live has,
    // optional.
    enum class Quantise : int { off = 0, beat, bar, fourBars };
    static int boundaryBeats(Quantise, int beatsPerBar);

    // A command for the audio thread, made by the factories below, which say
    // what each carries.
    struct Command
    {
        enum class Type : int
        {
            play, pause, togglePlay, cueDown, cueUp, hotCue, clearHotCue, setCue, setHotCue,
            loopIn, loopOut, beatLoop, reloopExit, loopHalve, loopDouble, clearLoop, setLoop,
            beatJump, seek, syncAlign, setMaster, stopAll, setSource, setSlotActive, tempoMoved
        };
        Type type = Type::play;
        int slot = -1;
        double frame = 0.0, endFrame = 0.0, beats = 0.0;
        int index = 0;
        bool on = false;
        DjDeckSource source;

        static Command make(Type, int slot) noexcept;
        static Command play(int slot) noexcept { return make(Type::play, slot); }
        static Command pause(int slot) noexcept { return make(Type::pause, slot); }
        static Command togglePlay(int slot) noexcept { return make(Type::togglePlay, slot); }
        static Command cueDown(int slot) noexcept { return make(Type::cueDown, slot); }
        static Command cueUp(int slot) noexcept { return make(Type::cueUp, slot); }
        static Command hotCue(int slot, int index) noexcept;
        static Command clearHotCue(int slot, int index) noexcept;
        static Command setCue(int slot, double frame) noexcept;
        static Command setHotCue(int slot, int index, double frame) noexcept;
        static Command loopIn(int slot) noexcept { return make(Type::loopIn, slot); }
        static Command loopOut(int slot) noexcept { return make(Type::loopOut, slot); }
        static Command beatLoop(int slot, double beats) noexcept;
        static Command reloopExit(int slot) noexcept { return make(Type::reloopExit, slot); }
        static Command loopHalve(int slot) noexcept { return make(Type::loopHalve, slot); }
        static Command loopDouble(int slot) noexcept { return make(Type::loopDouble, slot); }
        static Command clearLoop(int slot) noexcept { return make(Type::clearLoop, slot); }
        static Command setLoop(int slot, double startFrame, double endFrame, bool active) noexcept;
        static Command beatJump(int slot, int beats) noexcept;
        static Command seek(int slot, double frame) noexcept;
        // Jumps the deck onto the master's phase within the master's bar.
        static Command syncAlign(int slot) noexcept { return make(Type::syncAlign, slot); }
        static Command setMaster(int slot) noexcept { return make(Type::setMaster, slot); }
        static Command stopAll() noexcept { return make(Type::stopAll, -1); }
        // The fader has moved, so a tempo the deck held since it took over
        // as master gives way to it.
        static Command tempoMoved(int slot) noexcept { return make(Type::tempoMoved, slot); }
        // These two are what setDeckSource and setSlotActive send.
        static Command setSource(int slot, DjDeckSource) noexcept;
        static Command setSlotActive(int slot, bool active) noexcept;
    };

    DjEngine();
    ~DjEngine();

    // ---- message thread ----
    // Never while rendering. The decks keep what they play, where and how;
    // the buffers are sized again and the generation moves on.
    void prepare(double sampleRate, int maximumBlock);
    // What the engine was last prepared for, which anything built to run
    // beside it is built at, and how many times it has been prepared.
    double sampleRate() const noexcept { return preparedRate.load(std::memory_order_acquire); }
    int maximumBlockSize() const noexcept { return preparedBlock.load(std::memory_order_acquire); }
    std::uint32_t generation() const noexcept { return preparedGeneration.load(std::memory_order_acquire); }
    // A slot is rendered while active. One made inactive is emptied at its
    // next block, on the audio thread: its material handed back, its points,
    // fades and strip cleared. One made active starts as a new deck does,
    // whatever was left in it. Its controls are left for whoever takes the
    // slot next to set. False, and nothing changed, when the queue is full.
    bool setSlotActive(int slot, bool active);
    bool slotActive(int slot) const noexcept;
    // The slot plays this from its next block, crossfading from what it
    // played; null empties it. Moved from only when it is handed over: when
    // the queue is full this returns false and the material is still the
    // caller's, to hand over again later rather than make again.
    bool setDeckSource(int slot, std::unique_ptr<DjTrack>&& material, bool keepBeat);
    // The material most recently handed to a slot, for the displays: alive
    // until another is handed over and the engine gives this one back.
    const DjTrack* trackOf(int slot) const noexcept;
    // Frees what the audio thread has handed back. Returns how many objects
    // it still holds that no slot was last handed: superseded, on their way.
    int collect();
    bool push(const Command&) noexcept;
    int pendingCommands() const noexcept;
    // A slot's channel strip back to a new deck's, but for the filters'
    // resonance and the faders' curve, which the console sets on every
    // channel at once.
    void resetChannelControls(int slot);

    // ---- any thread ----
    DjDeck& deck(int slot) noexcept { return deckList[static_cast<size_t>(slot)]; }
    const DjDeck& deck(int slot) const noexcept { return deckList[static_cast<size_t>(slot)]; }
    DjChannelStrip& channel(int slot) noexcept { return channels[static_cast<size_t>(slot)]; }
    const DjChannelStrip& channel(int slot) const noexcept { return channels[static_cast<size_t>(slot)]; }
    // The port a slot's deck is heard through a track of the song by, when
    // its `bound` flag is on; the session points the track's hidden devices
    // at it.
    DjTrackPort& port(int slot) noexcept { return ports[static_cast<size_t>(slot)]; }
    DjCrossfader& crossfader() noexcept { return fader; }
    DjMasterSection& master() noexcept { return masterSection; }
    DjMicSection& mic() noexcept { return micSection; }
    DjSendFx& sendFx() noexcept { return sendEffect; }
    DjBeatFx& fx() noexcept { return effect; }

    std::atomic<int> quantise {static_cast<int>(Quantise::bar)};
    // The slot the others sync to: the one chosen, else the one already
    // master, else the first playing.
    std::atomic<int> masterDeck {-1};
    // The tempo everything is timed to: the master's tempo, never its
    // platter's speed, or 0 for none.
    std::atomic<double> masterBpm {0.0};
    // The tempo tapped in, which times the effect when no deck plays.
    std::atomic<double> tapBpm {128.0};
    // Phase lock: a synced deck is nudged back onto the master's beat when
    // it drifts, rather than only aligned when sync was pressed.
    std::atomic<bool> phaseLock {true};

    // ---- audio thread ----
    // Clears the outputs and writes the master to channels 0 and 1 and the
    // headphones to 2 and 3 when there are that many. The first input, when
    // there is one, is the mic.
    void process(const float* const* inputs, int inputChannels, float* const* outputs, int outputChannels, int frames);
    void process(float* const* outputs, int outputChannels, int frames) { process(nullptr, 0, outputs, outputChannels, frames); }

private:
    // Something handed to the engine, owned here on the message thread until
    // the audio thread hands it back: a deck's material today.
    struct Owned
    {
        DjObjectKind kind = DjObjectKind::material;
        std::unique_ptr<const void, void (*)(const void*)> object {nullptr, nullptr};
    };
    void applyCommands();
    void apply(const Command&);
    void setSlot(int slot, bool on);
    void alignToMaster(int slot);
    void chooseMaster();
    void setRates();
    void setRate(DjDeck&, const DjDeck* master, double masterTempo);
    void landPendings(int frames);
    // The frame of this chunk at which the master, read on as its plan will
    // read it, stands at a target beat's phase within `span` of its own beats
    // multiplied by `fold`; -1 when that is not in this chunk.
    int landingFrame(const DjDeck& master, double fold, double targetBeat, int span, int frames) const;
    void renderChunk(const float* micInput, float* const* outputs, int outputChannels, int frames);
    // The sequencer for one slot, run once a block after the plan: the
    // material's notes the deck passed in the block, each at its frame, into
    // the slot's block events.
    void sequence(int slot, int frames);
    void addEvent(int slot, int frame, int note, int velocity, bool on) noexcept;
    void releaseSounding(int slot, int frame) noexcept;
    struct Sequencer
    {
        const DjTrack* material = nullptr;
        double lastBeat = 0.0;
        std::uint32_t lastJumps = 0;
        bool wasActive = false;
        std::array<bool, 128> sounding {};
    };
    std::array<Sequencer, maximumDecks> sequencers;
    std::array<std::array<DjPortEvent, maximumBlockEvents>, maximumDecks> blockEvents {};
    std::array<int, maximumDecks> blockEventCount {};

    double rate = 48000.0;
    int maximumBlock = 512;
    std::atomic<double> preparedRate {48000.0};
    std::atomic<int> preparedBlock {512};
    std::atomic<std::uint32_t> preparedGeneration {0};
    std::array<DjDeck, maximumDecks> deckList;
    std::array<DjChannelStrip, maximumDecks> channels;
    std::array<DjTrackPort, maximumDecks> ports;
    std::array<DjBlockDelay, maximumDecks> delays;
    // Whether each slot went through its port last block: a slot that changes
    // sides starts its block delay afresh rather than replay a stale block.
    std::array<bool, maximumDecks> wentThroughPort {};
    DjCrossfader fader;
    DjMasterSection masterSection;
    DjMicSection micSection;
    DjSendFx sendEffect;
    DjBeatFx effect;
    std::atomic<int> requestedMaster {-1};
    // A running count of master beats for the effect's sweeps, kept moving
    // from the clock when nothing plays.
    double effectBeat = 0.0;
    // The slots the audio thread renders, as the commands left them, and the
    // message thread's own record of them.
    std::array<bool, maximumDecks> rendering {};
    std::array<bool, maximumDecks> active {};

    std::array<Command, queueSize> queue;
    std::atomic<int> head {0}, tail {0};
    DjReleaseQueue released;
    std::vector<Owned> owned;
    std::array<const DjTrack*, maximumDecks> handedOver {};
    int lostReported = 0;

    std::array<std::array<std::vector<float>, 2>, maximumDecks> scratch;
    std::array<std::vector<float>, 2> masterBus, cueBus, sendBus;
    std::vector<float> micBuffer;
};
}
