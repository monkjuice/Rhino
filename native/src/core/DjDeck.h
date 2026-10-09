#pragma once
#include "DjTrack.h"
#include <array>
#include <atomic>
#include <cstdint>

namespace rhino
{
// One player: what a CDJ is to a mixer. It owns a position into its track,
// a cue point, eight hot cues, a loop and a tempo, and it renders its track
// at whatever rate the engine asks for.
//
// Everything the interface reads is an atomic the audio thread writes, and
// everything the interface sets is an atomic the audio thread reads, so
// neither thread ever waits for the other. Commands - play, cue, a hot cue,
// a loop - arrive through the engine's queue and are applied here at a block
// boundary, on the audio thread, which is the only place the position is
// ever changed. Positions are frames of the track at its own sample rate.
class DjDeck
{
public:
    static constexpr int hotCueCount = 8;
    // What the deck is doing. `waiting` is a start held back until the
    // master's next beat or bar; `cueing` is the cue button held down.
    enum class State : int { empty = 0, stopped, playing, waiting, cueing };
    // What a quantised start or jump will do when the boundary arrives.
    enum class Pending : int { none = 0, start, jump };

    // ---- settings: the message thread writes, the audio thread reads ----
    // The tempo fader, in per cent, within the chosen range (6, 10, 16 or
    // 100). The engine clamps nothing: the session does.
    std::atomic<float> tempoPercent {0.0f};
    std::atomic<int> tempoRange {6};
    // The jog wheel's push, in per cent, held rather than set.
    std::atomic<float> nudgePercent {0.0f};
    std::atomic<bool> synced {false};
    std::atomic<bool> reversed {false};
    // The hand on the platter: while scratching, the deck plays at this
    // rate, backwards for a negative one, whatever the fader says.
    std::atomic<bool> scratching {false};
    std::atomic<double> scratchRate {0.0};
    // How long a stop takes and a start spins up, in seconds: the CDJ's
    // vinyl speed adjust. Zero is at once.
    std::atomic<float> brakeSeconds {0.0f};
    // Whether cue and loop points snap to the grid: the CDJ's Quantize key.
    std::atomic<bool> quantiseSnap {true};
    // Set before a track is swapped in so the new one picks up at the same
    // beat the old one was at, which is what a re-bounce wants.
    std::atomic<bool> keepBeatOnSwap {false};

    // ---- state: the audio thread writes, anyone reads ----
    std::atomic<const DjTrack*> track {nullptr};
    std::atomic<int> state {static_cast<int>(State::empty)};
    std::atomic<int> pending {static_cast<int>(Pending::none)};
    std::atomic<double> position {0.0};
    std::atomic<double> pendingPosition {-1.0};
    std::atomic<double> cuePoint {0.0};
    std::array<std::atomic<double>, hotCueCount> hotCues {};
    std::atomic<double> loopStart {-1.0}, loopEnd {-1.0};
    std::atomic<bool> loopActive {false};
    // The smoothed rate actually in use, for the BPM readout.
    std::atomic<double> playbackRate {1.0};
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
    // The track's own tempo, and the tempo it is playing at.
    double nativeBpm() const noexcept;
    double effectiveBpm() const noexcept;
    // The rate the fader and the jog ask for on their own.
    double baseRate() const noexcept;
    // Fractional beats at the current position, from the track's grid.
    double beatPosition() const noexcept;

    // ---- audio thread ----
    // The rate the engine wants this block. Smoothed on the way in.
    double targetRate = 1.0;
    // Writes the next frames; silent until a start lands. Picks up a track
    // the message thread has swapped in.
    void render(float* left, float* right, int frames, double deviceRate);
    // Commands. Each lands at a block boundary; quantise says whether a cue,
    // a hot cue or a loop point snaps to the track's nearest beat.
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
    // Takes up a track the message thread has swapped in. render does this
    // itself; a command arriving before the next render calls it first.
    void adoptPending();
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
    // Moves the deck so its beat phase equals the master's. A jump, not a
    // glide.
    void alignPhase(double masterPhase, int boundaryBeats);
    // The engine, having decided when a pending start or jump lands: at
    // frameOffset into the next render. snapBeats is the boundary size the
    // landing position is snapped to on the deck's own grid (0 for none).
    void landPending(int frameOffset, int snapBeats);
    // The position a pending start would begin from.
    double pendingFrame() const noexcept;
    // Where the nearest boundary of `beats` beats is to a frame, on the
    // track's grid; the frame itself without a grid.
    double snapToGrid(double frame, double beats) const noexcept;

private:
    struct Grid
    {
        double sampleRate = 44100.0, framesPerBeat = 0.0, firstBeatFrame = 0.0;
        int length = 0;
        bool valid() const noexcept { return framesPerBeat > 0.0; }
        double beatAt(double frame) const noexcept { return valid() ? (frame - firstBeatFrame) / framesPerBeat : 0.0; }
        double frameAt(double beat) const noexcept { return firstBeatFrame + beat * framesPerBeat; }
    };
    static Grid gridOf(const DjTrack*) noexcept;
    void adopt(const DjTrack* next);
    void setState(State s) noexcept { state.store(static_cast<int>(s), std::memory_order_relaxed); }
    void setPending(Pending p) noexcept { pending.store(static_cast<int>(p), std::memory_order_relaxed); }
    void stopWithFade(double returnToFrame);
    void beginPlaying(double fromFrame);
    double clampFrame(double frame) const noexcept;
    void noteJump() noexcept { jumps.fetch_add(1, std::memory_order_relaxed); }

    const DjTrack* current = nullptr;
    Grid grid;
    double rate = 1.0;
    float gain = 0.0f;
    bool stopping = false;
    double returnTo = -1.0;
    int startAt = -1;
    bool cueHeld = false;
    // The brake: the rate winds down from where it was over brakeSeconds
    // and the deck stops when it reaches nothing; a start winds up the
    // same way.
    bool braking = false, spinningUp = false;
    double brakeStep = 0.0;
};
}
