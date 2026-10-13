#pragma once
#include "DjLinearRamp.h"
#include "DjReleaseQueue.h"
#include "DjTrack.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace rhino
{
// What the message thread sets on a deck and the audio thread reads, in one
// place, so a reset or a copy cannot miss a field. The engine clamps nothing:
// the session does.
struct DjDeckControls
{
    // The tempo fader, in per cent, within the chosen range (6, 10, 16 or
    // 100).
    std::atomic<float> tempoPercent {0.0f};
    std::atomic<int> tempoRange {6};
    // The jog wheel's push, in per cent, held rather than set.
    std::atomic<float> nudgePercent {0.0f};
    std::atomic<bool> synced {false};
    std::atomic<bool> reversed {false};
    // The hand on the platter: while scratching, the deck plays at this rate,
    // backwards for a negative one, whatever the fader says.
    std::atomic<bool> scratching {false};
    std::atomic<double> scratchRate {0.0};
    // How long a stop takes and a start spins up, in seconds: the CDJ's vinyl
    // speed adjust. Zero is at once.
    std::atomic<float> brakeSeconds {0.0f};
    // Whether cue and loop points snap to the grid: the CDJ's Quantize key.
    std::atomic<bool> quantiseSnap {true};

    // Back to how a new deck stands, which is how these are initialised.
    void reset() noexcept;
    void copyFrom(const DjDeckControls&) noexcept;
};

// One player: what a CDJ is to a mixer. It owns a position into its
// material, a cue point, eight hot cues, a loop and a tempo, and it plays its
// material at whatever rate the engine asks for.
//
// Everything the interface reads is an atomic the audio thread writes, and
// everything the interface sets is an atomic the audio thread reads, so
// neither thread ever waits for the other. Commands - play, cue, a hot cue, a
// loop, the material itself - arrive through the engine's queue and are
// applied at a block boundary, on the audio thread, which is the only place
// the position is ever changed. Positions are frames of the material at its
// own sample rate.
//
// A block is played in two steps. `plan` holds every rule of the transport -
// starts and stops with their fades, the rate with its brake and spin-up,
// loops, the end of the material, the landings the engine timed - and writes
// down, frame by frame, where the material is read, how loud, what the deck
// is fading from, and which frames begin a new stretch. `read` only reads: the
// material at those positions with Hermite interpolation, crossfaded from
// what it was.
class DjDeck
{
public:
    static constexpr int hotCueCount = 8;
    // What the deck is doing. `waiting` is a start held back until the
    // master reaches the start's place in a beat or a bar; `cueing` is the
    // cue button held down.
    enum class State : int { empty = 0, stopped, playing, waiting, cueing };
    // What a quantised start or jump will do when its moment arrives.
    enum class Pending : int { none = 0, start, jump };
    // Why a frame of a plan does not follow the frame before it.
    enum Discontinuity : std::uint8_t { started = 1, landed = 2, jumped = 4, wrapped = 8 };
    // How long a jump, a loop's wrap or a change of material crossfades from
    // what the deck was playing: long enough to take the step out, short
    // enough not to be heard as a fade.
    static constexpr double declickSeconds = 0.005;

    DjDeckControls controls;

    // ---- state: the audio thread writes, anyone reads ----
    // The material the deck plays now. The engine hands material back only
    // once the deck has moved this on, and the message thread frees only
    // what it is handed, so the message thread may read what this points at.
    std::atomic<const DjTrack*> track {nullptr};
    std::atomic<int> state {static_cast<int>(State::empty)};
    std::atomic<int> pending {static_cast<int>(Pending::none)};
    std::atomic<double> position {0.0};
    std::atomic<double> pendingPosition {-1.0};
    std::atomic<double> cuePoint {0.0};
    std::array<std::atomic<double>, hotCueCount> hotCues {};
    // A loop may end at the material's length: a whole-track loop does.
    std::atomic<double> loopStart {-1.0}, loopEnd {-1.0};
    std::atomic<bool> loopActive {false};
    // The platter's speed: the smoothed rate in use, the brake, a spin-up, a
    // hand on the platter and the phase correction all in it.
    std::atomic<double> playbackRate {1.0};
    // The tempo the deck plays at, as a rate of its own: the fader with the
    // jog's push, a synced deck's master's tempo, or the tempo a deck that
    // took over as master holds. Sync follows this, never the platter.
    std::atomic<double> tempoRate {1.0};
    // That held tempo, until the deck's fader moves; zero while none is held.
    std::atomic<double> heldTempoRate {0.0};
    std::atomic<float> peak {0.0f};
    // Bumped whenever the deck lands somewhere it was not travelling to - a
    // cue, a hot cue, a loop wrap, a jump - so a display can tell a jump
    // from motion.
    std::atomic<std::uint32_t> jumps {0};

    DjDeck();

    State currentState() const noexcept { return static_cast<State>(state.load(std::memory_order_relaxed)); }
    Pending currentPending() const noexcept { return static_cast<Pending>(pending.load(std::memory_order_relaxed)); }
    bool isPlaying() const noexcept
    {
        const auto s = currentState();
        return s == State::playing || s == State::cueing;
    }
    bool hasTrack() const noexcept { return track.load(std::memory_order_relaxed) != nullptr; }
    // The material's own tempo; the tempo it plays at, which a console reads
    // out and sync follows; and the platter's speed in beats a minute.
    double nativeBpm() const noexcept;
    double tempoBpm() const noexcept;
    double effectiveBpm() const noexcept;
    // The rate the fader and the jog ask for on their own.
    double baseRate() const noexcept;
    // Fractional beats at the current position, from the material's grid.
    double beatPosition() const noexcept;

private:
    friend class DjEngine;
    // tests/DjTest.cpp drives one deck on its own, as the engine would.
    friend class DjDeckTest;

    struct Grid
    {
        double sampleRate = 44100.0, framesPerBeat = 0.0, firstBeatFrame = 0.0;
        int length = 0;
        bool valid() const noexcept { return framesPerBeat > 0.0; }
        double beatAt(double frame) const noexcept { return valid() ? (frame - firstBeatFrame) / framesPerBeat : 0.0; }
        double frameAt(double beat) const noexcept { return firstBeatFrame + beat * framesPerBeat; }
    };
    static Grid gridOf(const DjTrack*) noexcept;

    // ---- the audio thread, through the engine ----
    // Sizes the plan for blocks of up to maximumBlock frames. Never while
    // rendering; nothing the deck plays or fades from is let go of.
    void prepare(int maximumBlock, double deviceRate);
    void plan(int frames, double deviceRate);
    void read(float* left, float* right, int frames);
    // Material from its next frame. A deck that is heard crossfades into it
    // from what it played; keepBeat carries the position, the cues and the
    // loop across by beat, for a track bounced again. Null empties the deck.
    void setSource(const DjTrack* material, bool keepBeat);
    // The slot is emptied: the material and what it fades from are let go
    // of, and every point, fade and rate goes back to a new deck's.
    void reset();
    // Commands. Each lands at a block boundary; quantise says whether a cue,
    // a hot cue or a loop point snaps to the material's nearest beat.
    void play();
    void pause();
    void togglePlay();
    void cueDown(bool quantise);
    void cueUp();
    void hotCue(int index, bool quantise);
    void clearHotCue(int index);
    // Points set outright, in frames, as a document read back sets them.
    void setCuePoint(double frame);
    void setHotCuePoint(int index, double frame);
    void loopIn(bool quantise);
    void loopOut(bool quantise);
    void beatLoop(double beats, bool quantise);
    void reloopExit();
    void loopHalve();
    void loopDouble();
    void clearLoop();
    void setLoop(double start, double end, bool active);
    void beatJump(int beats);
    void seek(double frame);
    // Moves the deck so its beat phase within a span of boundaryBeats equals
    // the master's. A jump, not a glide, and never out of a loop the deck
    // plays in: Sync is not a landing the DJ asked for.
    void alignPhase(double masterPhase, int boundaryBeats);
    // The engine, having timed a pending start or jump: it lands frameOffset
    // frames into the next plan, at exactly the frame it asked for.
    void landPending(int frameOffset);
    // The position a pending start or jump lands at.
    double pendingFrame() const noexcept;
    // Where the nearest boundary of `beats` beats is to a frame, on the
    // material's grid; the frame itself without a grid.
    double snapToGrid(double frame, double beats) const noexcept;
    // Neither backwards, nor under a hand, nor winding down or up: a deck
    // whose beat moves steadily, which is what phase lock may correct.
    bool steady() const noexcept;
    // The material's own tempo, for the audio thread, which knows what it
    // plays without reading the published pointer.
    double ownBpm() const noexcept { return current != nullptr ? current->analysis.bpm : 0.0; }

    // Every landing a command or a timed jump makes goes through here: one
    // outside an active loop leaves it, as a CDJ's does, and a deck that is
    // heard crossfades from where it was rather than stepping.
    void jumpTo(double frame);
    // The move itself, crossfaded, leaving the loop alone: what jumpTo does
    // after the loop's rule, and all that Sync's alignment does.
    void moveTo(double frame);
    // What the deck fades from for the next frames: `from` read at `at`
    // onwards at `step` frames a frame, its weight falling to nothing over
    // `frames`. A fade still running goes on under the new one, so the new
    // fades from what was heard, the running fade included; one already
    // under it is let go of. A fade not yet read a frame of stays as it is,
    // made as long as the new one would be: what the deck was about to play
    // in its place was never heard.
    void beginFade(const DjTrack* from, double at, double step, int frames);
    void dropFade();
    void planFadeFrame(int frame) noexcept;
    // Lets an object go back to the message thread once nothing on the deck
    // reads it: neither playing nor faded from.
    void letGo(const DjTrack*) noexcept;
    double stepNow() const noexcept;
    bool audible() const noexcept { return gain > 0.0f; }
    int capacity() const noexcept { return static_cast<int>(planPos.size()); }
    void setState(State s) noexcept { state.store(static_cast<int>(s), std::memory_order_relaxed); }
    void setPending(Pending p) noexcept { pending.store(static_cast<int>(p), std::memory_order_relaxed); }
    void stopWithFade(double returnToFrame);
    void beginPlaying(double fromFrame);
    // A position stands inside the material; a loop point may stand at its
    // end.
    double clampFrame(double frame) const noexcept;
    double clampPoint(double frame) const noexcept;
    void noteJump() noexcept { jumps.fetch_add(1, std::memory_order_relaxed); }
    void publish() noexcept { position.store(pos, std::memory_order_relaxed); }

    DjReleaseQueue* releases = nullptr;
    const DjTrack* current = nullptr;
    Grid grid;
    double pos = 0.0;
    double outputRate = 48000.0;
    int declickFrames = 240;
    // The rate the engine wants this chunk, smoothed into `rate` on the way.
    double targetRate = 1.0;
    double rate = 1.0;
    float gain = 0.0f;
    bool stopping = false;
    double returnTo = -1.0;
    // A start and a jump the engine timed: their frame in the next plan.
    int startAt = -1, jumpAt = -1;
    double jumpTarget = 0.0;
    bool cueHeld = false;
    // The brake: the rate winds down from where it was over brakeSeconds
    // and the deck stops when it reaches nothing; a start winds up the
    // same way.
    bool braking = false, spinningUp = false;
    double brakeStep = 0.0;
    // The engine's sync bookkeeping: the tempo rate in use, a tempo held
    // since the deck took over as master, and whether it followed a master
    // last chunk.
    double tempo = 1.0, heldTempo = 0.0;
    bool following = false;
    // What the deck crossfades from after a jump, a wrap or a change of
    // material: that material, read on from where it was at the rate it
    // had, its weight a linear ramp to nothing. Kept until the ramp ends,
    // which is when the material is let go of. A second discontinuity within
    // the fade - a quantised jump landing just after a loop's wrap, two jumps
    // within five milliseconds - puts the running fade under the new one,
    // where it goes on fading out of what the new one fades from; when the
    // fade over it ends, it is no longer heard and goes too.
    struct Fade
    {
        const DjTrack* material = nullptr;
        double at = 0.0, step = 0.0;
        DjLinearRamp weight;
    };
    Fade fade, under;
    // A landing a command made, marked on the next plan's first frame.
    std::uint8_t pendingMark = 0;
    // The plan of one block: where the material is read and how loud, what
    // the deck fades from, where and how much, the same of the fade under
    // that, and why a frame begins a new stretch.
    std::vector<double> planPos, planFromAt, planUnderAt;
    std::vector<float> planGain, planFromWeight, planUnderWeight;
    std::vector<const DjTrack*> planFrom, planUnder;
    std::vector<std::uint8_t> planMark;
};
}
