#include "model/Session.h"
#include "dj/DjEngine.h"
#include "dj/DjTrack.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>
#if JUCE_INTEL
 #include <xmmintrin.h>
#endif

// The booth's cost check counts the allocations its blocks make rather than
// trusting a reading of the code, as Forge's tests count theirs
// (instruments/rhino-forge/tests/ForgeTestSupport.cpp): the binary's global
// operator new keeps a tally for the thread that asked for one. Every other
// thread, and this one outside the check, pays one thread-local read per
// allocation. The array and nothrow forms default to these two, so replacing
// them covers those too; juce::HeapBlock calls malloc directly and is not
// seen.
namespace
{
thread_local long long* allocationTally = nullptr;
}

void* operator new(std::size_t size)
{
    if (allocationTally != nullptr) ++*allocationTally;
    if (auto* memory = std::malloc(size == 0 ? 1 : size)) return memory;
    throw std::bad_alloc();
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

// The DJ booth's engine, measured offline. Nothing here reads the engine's
// own arithmetic back: a deck's speed is measured by how far a ramp got, a
// kill by how much of a tone is left, a quantised start by where the master
// stood when the other deck began, and the tempo of a synthesised beat by
// the analyser that never saw the number it was built from.
//
// The deck is driven on its own for the transport checks, with its commands
// called the way the engine calls them at a block boundary, because the
// mixer's crossovers between a deck and the output delay a ramp by their
// group delay and would blur a check that reads the position off it.
namespace rhino
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double deviceRate = 48000.0;
constexpr int block = 512;
// The declick at the device rate: five milliseconds.
constexpr int declickFrames = 240;
using Command = DjEngine::Command;

void require(bool valid, const juce::String& what)
{
    if (!valid)
        throw std::runtime_error(("DJ booth: " + what).toStdString());
}

// A track whose left channel counts its own frames, so a position can be
// read off the output, and whose right channel is a tone.
std::unique_ptr<DjTrack> rampTrack(double sampleRate, double seconds, double bpm = 0.0)
{
    auto track = std::make_unique<DjTrack>();
    track->sampleRate = sampleRate;
    track->name = "ramp";
    const auto frames = static_cast<int>(seconds * sampleRate);
    track->left.resize(static_cast<size_t>(frames));
    track->right.resize(static_cast<size_t>(frames));
    for (int i = 0; i < frames; ++i)
    {
        track->left[static_cast<size_t>(i)] = static_cast<float>(i) / static_cast<float>(frames);
        track->right[static_cast<size_t>(i)] = 0.5f * static_cast<float>(std::sin(2.0 * pi * 220.0 * i / sampleRate));
    }
    track->analysis.bpm = bpm;
    track->analysis.firstBeatSeconds = 0.0;
    track->analysis.beatsPerBar = 4;
    return track;
}

std::unique_ptr<DjTrack> toneTrack(double frequency, double sampleRate, double seconds, float amplitude = 0.5f)
{
    auto track = std::make_unique<DjTrack>();
    track->sampleRate = sampleRate;
    track->name = "tone";
    const auto frames = static_cast<int>(seconds * sampleRate);
    track->left.resize(static_cast<size_t>(frames));
    for (int i = 0; i < frames; ++i)
        track->left[static_cast<size_t>(i)] = amplitude * static_cast<float>(std::sin(2.0 * pi * frequency * i / sampleRate));
    return track;
}

// One level throughout.
std::unique_ptr<DjTrack> constantTrack(double seconds, float level)
{
    auto track = std::make_unique<DjTrack>();
    track->sampleRate = deviceRate;
    track->name = "constant";
    track->left.assign(static_cast<size_t>(seconds * deviceRate), level);
    return track;
}

// White noise from a fixed seed, a different stream in each channel.
std::unique_ptr<DjTrack> noiseTrack(double sampleRate, double seconds)
{
    auto track = std::make_unique<DjTrack>();
    track->sampleRate = sampleRate;
    track->name = "noise";
    const auto frames = static_cast<size_t>(seconds * sampleRate);
    track->left.resize(frames);
    track->right.resize(frames);
    std::uint32_t state = 0x2545f491u;
    const auto next = [&state]
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<double>(state) / 4294967296.0 * 2.0 - 1.0);
    };
    for (size_t i = 0; i < frames; ++i)
    {
        track->left[i] = next();
        track->right[i] = next();
    }
    return track;
}

// Silence with one full-scale sample a little way in.
std::unique_ptr<DjTrack> clickTrack(double seconds)
{
    auto track = std::make_unique<DjTrack>();
    track->sampleRate = deviceRate;
    track->name = "click";
    track->left.assign(static_cast<size_t>(deviceRate * seconds), 0.0f);
    track->left[1000] = 1.0f;
    return track;
}

struct Output
{
    std::vector<float> left, right;
};

// Runs the engine for a number of blocks, keeping the output, and frees what
// the blocks handed back.
Output run(DjEngine& engine, int blocks, int frames = block)
{
    Output out;
    std::vector<float> l(static_cast<size_t>(frames)), r(static_cast<size_t>(frames));
    float* outputs[2] {l.data(), r.data()};
    for (int b = 0; b < blocks; ++b)
    {
        engine.process(outputs, 2, frames);
        out.left.insert(out.left.end(), l.begin(), l.end());
        out.right.insert(out.right.end(), r.begin(), r.end());
    }
    engine.collect();
    return out;
}

// Slots for the first few decks, as the session takes them for a new booth.
void activate(DjEngine& engine, int decks)
{
    for (int slot = 0; slot < decks; ++slot)
        engine.setSlotActive(slot, true);
}

float rms(const std::vector<float>& samples, size_t from = 0, size_t to = 0)
{
    if (to == 0) to = samples.size();
    double sum = 0.0;
    for (size_t i = from; i < to && i < samples.size(); ++i)
        sum += static_cast<double>(samples[i]) * samples[i];
    const auto count = to > from ? to - from : 1;
    return static_cast<float>(std::sqrt(sum / static_cast<double>(count)));
}

float decibels(float ratio)
{
    return 20.0f * std::log10(std::max(1.0e-9f, ratio));
}

// The largest change from one sample to the next.
float largestStep(const std::vector<float>& samples, size_t from = 0, size_t to = 0)
{
    if (to == 0) to = samples.size();
    float largest = 0.0f;
    for (size_t i = std::max<size_t>(from, 1); i < to && i < samples.size(); ++i)
        largest = std::max(largest, std::abs(samples[i] - samples[i - 1]));
    return largest;
}

// The same through what came out after a change and the sample before it: a
// command lands between two blocks, so the step it makes is across them.
float largestStepAcross(const std::vector<float>& before, const std::vector<float>& after)
{
    if (before.empty() || after.empty()) return largestStep(after);
    return std::max(std::abs(after.front() - before.back()), largestStep(after));
}

// Counts the allocations this thread makes while it is alive.
class AllocationCounter
{
public:
    AllocationCounter() : outer(allocationTally) { allocationTally = &counted; }
    ~AllocationCounter() { allocationTally = outer; }
    AllocationCounter(const AllocationCounter&) = delete;
    AllocationCounter& operator=(const AllocationCounter&) = delete;
    long long count() const noexcept { return counted; }

private:
    long long counted = 0;
    long long* outer = nullptr;
};

// Whether any arithmetic since it was made took a denormal for an operand:
// the processor keeps a sticky flag of it, with flushing off. Nothing to
// watch on a processor without one.
class DenormalWatch
{
public:
#if JUCE_INTEL
    static constexpr bool available = true;
    DenormalWatch() : saved(_mm_getcsr())
    {
        // The six exception flags, denormals-are-zero and flush-to-zero off.
        _mm_setcsr(saved & ~(0x3Fu | 0x40u | 0x8000u));
    }
    ~DenormalWatch() { _mm_setcsr(saved); }
    bool touched() const noexcept { return (_mm_getcsr() & 0x2u) != 0; }

private:
    unsigned int saved;
#else
    static constexpr bool available = false;
    bool touched() const noexcept { return false; }
#endif
};

// The value below which a share of the samples falls, of a copy.
double percentile(std::vector<double> samples, double share)
{
    if (samples.empty()) return 0.0;
    std::sort(samples.begin(), samples.end());
    const auto at = static_cast<size_t>(std::clamp(share, 0.0, 1.0) * static_cast<double>(samples.size() - 1));
    return samples[at];
}

// How often a signal crosses zero, as a frequency.
double zeroCrossingFrequency(const std::vector<float>& samples)
{
    int crossings = 0;
    for (size_t i = 1; i < samples.size(); ++i)
        if ((samples[i - 1] < 0.0f) != (samples[i] < 0.0f))
            ++crossings;
    return crossings * deviceRate / (2.0 * static_cast<double>(std::max<size_t>(1, samples.size())));
}

// Counts the runs of loud and of silent 256-frame windows.
void countRuns(const std::vector<float>& samples, int& silentRuns, int& loudRuns)
{
    silentRuns = loudRuns = 0;
    bool loud = rms(samples, 0, 256) > 0.05f;
    for (size_t at = 256; at + 256 <= samples.size(); at += 256)
    {
        const auto now = rms(samples, at, at + 256) > 0.05f;
        if (now != loud)
        {
            if (now) ++loudRuns; else ++silentRuns;
            loud = now;
        }
    }
}

// Beats into a span, as a phase in [0, span).
double phaseOf(double beat, double span)
{
    return beat - std::floor(beat / span) * span;
}

// How far apart two phases of a span are, the short way round.
double phaseDistance(double a, double b, double span)
{
    auto d = std::abs(a - b);
    return std::min(d, span - d);
}
}

// One deck driven on its own, as the engine drives it: commands at a block
// boundary, then the plan and the read, and what the block let go of handed
// back at its end. A friend of DjDeck, so its checks call what the engine
// calls.
class DjDeckTest
{
public:
    static void transport();
    static void swapCrossfade();
    static void brake();
    static void plainPlaybackUnchanged();
    static void wholeLoopsAndWraps();
    static void leavingLoops();
    static void keepBeatLoopingWhole();
    static void declick();
    static void fadesUnderFades();
    static void crawlingWrap();

private:
    static void prepare(DjDeck& deck, int frames = block, DjReleaseQueue* released = nullptr)
    {
        deck.releases = released;
        deck.prepare(frames, deviceRate);
    }
    // What the engine does to a start with quantise off: lands it at once.
    static void start(DjDeck& deck)
    {
        deck.play();
        deck.landPending(0);
    }
    static Output render(DjDeck& deck, int blocks, int frames = block)
    {
        Output out;
        std::vector<float> l(static_cast<size_t>(frames)), r(static_cast<size_t>(frames));
        for (int b = 0; b < blocks; ++b)
        {
            deck.plan(frames, deviceRate);
            deck.read(l.data(), r.data(), frames);
            if (deck.releases != nullptr)
                deck.releases->flush();
            out.left.insert(out.left.end(), l.begin(), l.end());
            out.right.insert(out.right.end(), r.begin(), r.end());
        }
        return out;
    }
    // How many objects have come back through a queue.
    static int taken(DjReleaseQueue& released)
    {
        int count = 0;
        DjReleaseQueue::Entry entry;
        while (released.take(entry))
            ++count;
        return count;
    }
};

void DjDeckTest::transport()
{
    DjDeck deck;
    prepare(deck);
    auto ramp = rampTrack(44100.0, 10.0);
    deck.setSource(ramp.get(), false);
    require(deck.currentState() == DjDeck::State::stopped, "a deck given material is stopped, not empty");
    const auto silent = render(deck, 2);
    require(rms(silent.left) == 0.0f, "a stopped deck is silent");

    start(deck);
    const auto played = render(deck, 20);
    require(deck.currentState() == DjDeck::State::playing, "play plays");
    // The ramp's value says where the deck is: frames of a 44.1 kHz track
    // read at 48 kHz, so a block advances 512 * 44100 / 48000 frames.
    const auto step = 44100.0 / 48000.0;
    const auto expected = 20.0 * block * step;
    const auto at = deck.position.load();
    require(std::abs(at - expected) < 2.0,
            "a deck at its own rate advances by the rate ratio (" + juce::String(at) + " vs " + juce::String(expected) + ")");
    // The last sample out was read one step before the position now.
    const auto lastValue = static_cast<double>(played.left.back()) * 441000.0;
    require(std::abs(lastValue - (at - step)) < 1.0, "what came out is what the position says it played ("
            + juce::String(lastValue) + " vs " + juce::String(at - step) + ")");

    // Pause fades out and stops where it is.
    deck.pause();
    render(deck, 2);
    require(deck.currentState() == DjDeck::State::stopped, "pause stops");
    const auto paused = deck.position.load();
    render(deck, 5);
    require(deck.position.load() == paused, "a stopped deck stays put");

    // Tempo: +6% plays 6% more frames once the rate has settled. The engine
    // sets a deck's target rate every block; here it is set by hand.
    deck.controls.tempoPercent.store(6.0f);
    deck.targetRate = deck.baseRate();
    start(deck);
    render(deck, 10);   // settle
    const auto before = deck.position.load();
    render(deck, 40);
    const auto moved = deck.position.load() - before;
    const auto wanted = 40.0 * block * step * 1.06;
    require(std::abs(moved / wanted - 1.0) < 0.002,
            "the tempo fader at +6% plays 6% faster (" + juce::String(moved) + " vs " + juce::String(wanted) + ")");
    deck.controls.tempoPercent.store(0.0f);
    deck.targetRate = deck.baseRate();

    // Reverse runs backwards.
    deck.controls.reversed.store(true);
    render(deck, 10);
    const auto forwardEnd = deck.position.load();
    render(deck, 10);
    require(deck.position.load() < forwardEnd, "reverse moves the deck backwards");
    deck.controls.reversed.store(false);

    // Seek, then a hot cue set there and recalled from elsewhere.
    deck.seek(100000.0);
    render(deck, 1);
    require(std::abs(deck.position.load() - 100000.0) < 600.0, "seek lands where it was sent");
    deck.hotCue(2, false);
    require(deck.hotCues[2].load() >= 100000.0 && deck.hotCues[2].load() < 100600.0, "a hot cue pressed empty is set here");
    render(deck, 20);
    deck.hotCue(2, false);
    require(deck.currentPending() == DjDeck::Pending::jump, "a hot cue pressed set while playing asks to jump");
    deck.landPending(0);
    render(deck, 1);
    require(std::abs(deck.position.load() - deck.hotCues[2].load()) < 600.0, "and lands there");
    require(deck.isPlaying(), "still playing");

    // Cue: stop and return.
    deck.pause();
    render(deck, 2);
    deck.seek(50000.0);
    deck.cueDown(false);
    require(std::abs(deck.cuePoint.load() - 50000.0) < 1.0, "cue pressed while stopped sets the cue point");
    start(deck);
    render(deck, 20);
    deck.cueDown(false);
    render(deck, 2);
    require(deck.currentState() == DjDeck::State::stopped && std::abs(deck.position.load() - 50000.0) < 1.0,
            "cue pressed while playing stops and goes back to the cue point");
    deck.cueDown(false);
    const auto held = render(deck, 4);
    require(deck.currentState() == DjDeck::State::cueing && rms(held.right) > 0.1f, "cue held at the cue point plays");
    deck.cueUp();
    render(deck, 2);
    require(deck.currentState() == DjDeck::State::stopped && std::abs(deck.position.load() - 50000.0) < 1.0,
            "and letting go returns to the cue point");

    // A loop holds the deck inside it, wrapping as often as it reaches its
    // end, and the output never steps: past the loop's end the material
    // fades out over five milliseconds while its start fades in, so the
    // largest change from one sample to the next is the loop's drop in the
    // ramp spread over the fade, and the ramp's own slope.
    deck.setLoop(60000.0, 70000.0, true);
    deck.seek(60000.0);
    start(deck);
    render(deck, 2);
    const auto jumpsBefore = deck.jumps.load();
    const auto looped = render(deck, 70);   // some 32,000 frames: three wraps
    require(deck.position.load() >= 60000.0 && deck.position.load() < 70000.0, "a loop keeps the deck inside it");
    const auto wraps = deck.jumps.load() - jumpsBefore;
    require(wraps >= 3, "and wraps each time it reaches the loop's end (" + juce::String(static_cast<int>(wraps)) + " wraps)");
    const auto drop = 10000.0f / 441000.0f;
    const auto crossfaded = drop / static_cast<float>(declickFrames) * 1.1f + 1.0e-5f;
    require(largestStep(looped.left) < crossfaded, "the wrap crossfades rather than steps (" + juce::String(largestStep(looped.left), 6)
                                                    + " a sample at most, a step would be " + juce::String(drop, 4) + ")");
    deck.reloopExit();
    require(!deck.loopActive.load(), "reloop/exit leaves the loop");
    deck.loopHalve();
    require(std::abs(deck.loopEnd.load() - 65000.0) < 1.0, "halve halves the loop");
    deck.loopDouble();
    require(std::abs(deck.loopEnd.load() - 70000.0) < 1.0, "double doubles it back");

    // Beat jump on a grid: a 120 BPM track has 22050 frames to a beat. It
    // moves exactly so many beats from where the deck stands, so a deck off
    // the beat stays off it by the same fraction.
    auto gridded = rampTrack(44100.0, 10.0, 120.0);
    deck.setSource(gridded.get(), false);
    deck.pause();
    render(deck, 2);
    deck.seek(50000.0);   // beat 2.27
    deck.beatJump(4);
    require(std::abs(deck.position.load() - (50000.0 + 4.0 * 22050.0)) < 1.0,
            "a beat jump moves four beats from where the deck stood (" + juce::String(deck.position.load() / 22050.0, 3) + " beats)");
    deck.beatLoop(2.0, true);
    require(std::abs(deck.loopStart.load() - 6.0 * 22050.0) < 1.0 && std::abs(deck.loopEnd.load() - 8.0 * 22050.0) < 1.0
            && deck.loopActive.load(), "a two-beat loop starts on the beat and lasts two beats");

    // The end of the track stops the deck, through its fade, at the end.
    deck.clearLoop();
    deck.seek(441000.0 - 2000.0);
    start(deck);
    render(deck, 10);
    require(deck.currentState() == DjDeck::State::stopped && deck.position.load() >= 441000.0 - 2.0,
            "a deck reaching the end of its track stops there");

    // Material swapped in by beat keeps the beat; by time otherwise.
    deck.seek(22050.0 * 3.0);
    auto faster = rampTrack(48000.0, 10.0, 120.0);
    deck.setSource(faster.get(), true);
    require(std::abs(deck.position.load() - 24000.0 * 3.0) < 1.0, "a swap by beat lands on the same beat of the new material");
    auto plain = rampTrack(96000.0, 10.0);
    deck.setSource(plain.get(), false);
    require(std::abs(deck.position.load() - 48000.0 * 3.0) < 1.0, "a swap by time lands at the same second");
    deck.setSource(nullptr, false);
    render(deck, 1);
    require(deck.currentState() == DjDeck::State::empty && deck.hotCues[2].load() < 0.0, "material taken away empties the deck");

    // A jump the engine timed for later in a block goes with a stop that
    // lands before it: played again, the deck carries on from where it
    // stopped, not from the hot cue.
    deck.setSource(ramp.get(), false);
    deck.seek(100000.0);
    start(deck);
    render(deck, 4);
    deck.setHotCuePoint(5, 300000.0);
    deck.hotCue(5, false);
    deck.landPending(400);
    deck.pause();
    render(deck, 1);
    require(deck.currentState() == DjDeck::State::stopped && deck.position.load() < 110000.0, "a stop lands before the timed jump");
    const auto stoppedAt = deck.position.load();
    start(deck);
    render(deck, 1);
    require(std::abs(deck.position.load() - (stoppedAt + block * step)) < 1.0, "which goes with it");
}

// Material swapped in while the deck plays crossfades from the old over five
// milliseconds whatever the block size, so a bounce made again lands without
// a step; and the old material goes back to the message thread only once its
// fade has ended, which is when nothing reads it any more.
void DjDeckTest::swapCrossfade()
{
    for (const auto frames : {block, 64})
    {
        DjDeck deck;
        DjReleaseQueue released;
        prepare(deck, frames, &released);
        auto old = rampTrack(48000.0, 10.0);
        deck.setSource(old.get(), false);
        deck.targetRate = deck.baseRate();
        start(deck);
        const auto before = render(deck, 20 * block / frames, frames);
        auto fresh = std::make_unique<DjTrack>();
        fresh->sampleRate = 48000.0;
        fresh->name = "flat";
        fresh->left.assign(480000, 0.5f);
        deck.setSource(fresh.get(), false);
        Output swapped;
        int blocksUntilBack = -1;
        for (int b = 0; b < 2 * block / frames; ++b)
        {
            const auto out = render(deck, 1, frames);
            swapped.left.insert(swapped.left.end(), out.left.begin(), out.left.end());
            if (blocksUntilBack < 0 && taken(released) == 1)
                blocksUntilBack = b + 1;
        }
        const auto blockSize = juce::String(frames);
        require(largestStepAcross(before.left, swapped.left) < 0.01f,
                "the swap steps by no more than a crossfade does, in blocks of " + blockSize + " ("
                + juce::String(largestStepAcross(before.left, swapped.left), 4) + ")");
        require(std::abs(swapped.left[static_cast<size_t>(declickFrames) - 2] - 0.5f) > 1.0e-4f
                    && std::abs(swapped.left[static_cast<size_t>(declickFrames)] - 0.5f) < 1.0e-6f,
                "the crossfade lasts five milliseconds, in blocks of " + blockSize);
        require(std::abs(swapped.left.back() - 0.5f) < 1.0e-6f, "and the new material plays alone after it");
        require(blocksUntilBack == (declickFrames + frames - 1) / frames,
                "the old material goes back the block its fade ends, in blocks of " + blockSize + " (after "
                + juce::String(blocksUntilBack) + ")");
        deck.setSource(nullptr, false);
        render(deck, 4, frames);
    }
}

// The brake, on the deck itself: a stop winds down and a start spins up.
void DjDeckTest::brake()
{
    DjDeck deck;
    prepare(deck);
    auto ramp = rampTrack(44100.0, 10.0);
    deck.setSource(ramp.get(), false);
    deck.targetRate = deck.baseRate();
    start(deck);
    render(deck, 20);
    // A braked stop winds the rate down from one to nothing over half a
    // second, which covers a quarter of a second of material.
    deck.controls.brakeSeconds.store(0.5f);
    const auto before = deck.position.load();
    deck.pause();
    render(deck, 10);   // 0.1 s in
    require(deck.isPlaying() && deck.position.load() > before, "a braked stop keeps the platter turning");
    render(deck, 50);   // 0.64 s in all
    require(deck.currentState() == DjDeck::State::stopped, "and stops once the platter has wound down");
    const auto travelled = deck.position.load() - before;
    require(std::abs(travelled - 0.25 * 44100.0) < 700.0,
            "covering a quarter second of material in half a second (" + juce::String(travelled) + " frames)");
    // A braked start spins up the same way.
    const auto restart = deck.position.load();
    start(deck);
    render(deck, 47);   // 0.5 s
    const auto spun = deck.position.load() - restart;
    require(deck.isPlaying() && std::abs(spun - 0.25 * 44100.0) < 700.0,
            "a braked start spins up over the brake time (" + juce::String(spun) + " frames)");
    deck.controls.brakeSeconds.store(0.0f);
    deck.setSource(nullptr, false);
    render(deck, 1);
}

// Plain playback through the plan and the read is the player that was one
// piece before them, to the bit. The reference is that player, cut down to
// what plain playback runs - the rate smoothed toward its target, the fade
// in, Hermite - and the two must agree on every sample: a ramp read at
// another rate and noise at the device's own, each at the material's own
// tempo and at +6 per cent.
void DjDeckTest::plainPlaybackUnchanged()
{
    struct Reference
    {
        const DjTrack& track;
        double pos = 0.0, rate = 1.0, targetRate = 1.0;
        float gain = 0.0f;

        static float read(const std::vector<float>& samples, double at)
        {
            const auto count = static_cast<int>(samples.size());
            if (count == 0) return 0.0f;
            const auto index = static_cast<int>(std::floor(at));
            const auto fraction = static_cast<float>(at - index);
            const auto sample = [&samples, count](int i) { return samples[static_cast<size_t>(std::clamp(i, 0, count - 1))]; };
            const auto x0 = sample(index - 1), x1 = sample(index), x2 = sample(index + 1), x3 = sample(index + 2);
            const auto c1 = 0.5f * (x2 - x0);
            const auto c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
            const auto c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
            return ((c3 * fraction + c2) * fraction + c1) * fraction + x1;
        }

        void render(float* left, float* right, int frames)
        {
            const auto fadeStep = static_cast<float>(1.0 / std::max(1.0, 0.002 * deviceRate));
            const auto smoothing = 1.0 - std::exp(-1.0 / (0.03 * deviceRate));
            const auto rateScale = track.sampleRate / deviceRate;
            const auto& rightSamples = track.stereo() ? track.right : track.left;
            for (int f = 0; f < frames; ++f)
            {
                rate += smoothing * (targetRate - rate);
                const auto step = 1.0 * rate * rateScale;
                const auto l = read(track.left, pos);
                const auto r = read(rightSamples, pos);
                gain = std::min(1.0f, gain + fadeStep);
                left[f] = l * gain;
                right[f] = r * gain;
                pos += step;
            }
        }
    };
    const auto ramp = rampTrack(44100.0, 10.0);
    const auto noise = noiseTrack(48000.0, 10.0);
    for (const auto* track : {ramp.get(), noise.get()})
        for (const auto rate : {1.0, 1.06})
        {
            DjDeck deck;
            prepare(deck);
            deck.setSource(track, false);
            deck.targetRate = rate;
            start(deck);
            Reference reference {*track};
            reference.targetRate = rate;
            std::vector<float> l(block), r(block), referenceLeft(block), referenceRight(block);
            for (int b = 0; b < 300; ++b)
            {
                deck.plan(block, deviceRate);
                deck.read(l.data(), r.data(), block);
                reference.render(referenceLeft.data(), referenceRight.data(), block);
                require(std::memcmp(l.data(), referenceLeft.data(), sizeof(float) * block) == 0
                            && std::memcmp(r.data(), referenceRight.data(), sizeof(float) * block) == 0,
                        "plain playback through the plan and the read is the one-piece player's to the bit ("
                        + track->name + " at " + juce::String(rate, 2) + ", block " + juce::String(b) + ")");
            }
        }
}

// The loop's end and the material's (audit F0, F2). A whole-track loop -
// every fresh bounce lands with one - ends at the material's length and
// wraps for ever, forwards and backwards; the wrap is a modulo, so a loop set
// behind the deck takes it in at once rather than a loop length a frame.
void DjDeckTest::wholeLoopsAndWraps()
{
    for (const auto backwards : {false, true})
    {
        DjDeck deck;
        prepare(deck);
        auto ramp = rampTrack(48000.0, 1.0);
        deck.setSource(ramp.get(), false);
        deck.setLoop(0.0, 48000.0, true);
        require(deck.loopEnd.load() == 48000.0, "a whole-track loop ends at the material's length");
        deck.controls.reversed.store(backwards);
        if (backwards)
            deck.seek(40000.0);
        start(deck);
        const auto jumpsBefore = deck.jumps.load();
        render(deck, 300);   // over three lengths
        const auto direction = juce::String(backwards ? "backwards" : "forwards");
        require(deck.isPlaying() && deck.jumps.load() - jumpsBefore >= 3,
                "a whole-track loop wraps for ever, " + direction + " (" + juce::String(static_cast<int>(deck.jumps.load() - jumpsBefore))
                + " wraps)");
        require(deck.position.load() >= 0.0 && deck.position.load() < 48000.0, "and keeps the deck inside the material, " + direction);
    }

    DjDeck deck;
    prepare(deck);
    auto ramp = rampTrack(48000.0, 10.0);
    deck.setSource(ramp.get(), false);
    deck.seek(300000.0);
    start(deck);
    render(deck, 2);
    const auto jumpsBefore = deck.jumps.load();
    deck.setLoop(60000.0, 70000.0, true);
    render(deck, 1);
    require(deck.jumps.load() - jumpsBefore == 1 && deck.position.load() >= 60000.0 && deck.position.load() < 70000.0,
            "a loop set behind the deck takes it in with one wrap (" + juce::String(static_cast<int>(deck.jumps.load() - jumpsBefore))
            + " wraps)");
}

// Any landing outside an active loop leaves it, as a CDJ's does - a seek, a
// hot cue, the cue's return, a beat jump the loop cannot follow - and one
// inside keeps it.
void DjDeckTest::leavingLoops()
{
    DjDeck deck;
    prepare(deck);
    auto ramp = rampTrack(48000.0, 10.0, 120.0);   // 24000 frames a beat
    deck.setSource(ramp.get(), false);
    deck.setLoop(60000.0, 70000.0, true);
    deck.seek(62000.0);
    start(deck);
    render(deck, 4);
    deck.seek(300000.0);
    const auto landed = render(deck, 1);
    require(!deck.loopActive.load() && std::abs(deck.position.load() - (300000.0 + block)) < 1.0,
            "a seek past an active loop leaves it, and the deck plays on from there");
    // Once the declick has passed, the output is the ramp from 300000 on.
    const auto onward = static_cast<double>(landed.left.back()) * 480000.0;
    require(std::abs(onward - (300000.0 + block - 1.0)) < 0.1, "the ramp carries on from where the seek sent it");

    deck.setLoop(60000.0, 70000.0, true);
    deck.seek(61000.0);
    render(deck, 1);
    require(deck.loopActive.load(), "a landing inside an active loop keeps it");
    deck.setHotCuePoint(0, 200000.0);
    deck.hotCue(0, false);
    deck.landPending(0);
    render(deck, 1);
    require(!deck.loopActive.load() && std::abs(deck.position.load() - (200000.0 + block)) < 1.0, "a hot cue outside it leaves it");

    deck.setCuePoint(10000.0);
    deck.setLoop(60000.0, 70000.0, true);
    deck.seek(61000.0);
    render(deck, 1);
    deck.cueDown(false);
    render(deck, 2);
    require(deck.currentState() == DjDeck::State::stopped && !deck.loopActive.load() && deck.position.load() == 10000.0,
            "the cue's return outside it leaves it");

    // A loop that fits moves with a beat jump; one that would run past the
    // material's end stays, and the deck landing past it leaves it.
    deck.setLoop(48000.0, 72000.0, true);
    deck.seek(50000.0);
    start(deck);
    deck.beatJump(4);
    require(deck.loopActive.load() && deck.loopStart.load() == 48000.0 + 96000.0 && deck.loopEnd.load() == 72000.0 + 96000.0,
            "a beat jump carries an active loop with it");
    deck.setLoop(432000.0, 456000.0, true);
    deck.seek(440000.0);
    deck.beatJump(4);
    require(!deck.loopActive.load() && deck.loopStart.load() == 432000.0,
            "and one it cannot carry, past the material's end, it leaves behind");
    render(deck, 4);
}

// A deck looping its whole material keeps doing so across a swap by beat,
// and a beat past the new material's end comes round by its length: a one-bar
// cell swapped in at beat 21.5 of an eight-bar song comes in at beat 1.5.
void DjDeckTest::keepBeatLoopingWhole()
{
    DjDeck deck;
    prepare(deck);
    auto song = rampTrack(48000.0, 16.0, 120.0);   // 32 beats
    auto cell = rampTrack(48000.0, 2.0, 120.0);    // 4 beats
    deck.setSource(song.get(), false);
    deck.setLoop(0.0, static_cast<double>(song->length()), true);
    deck.seek(21.5 * 24000.0);
    deck.setSource(cell.get(), true);
    require(std::abs(deck.position.load() - 1.5 * 24000.0) < 1.0e-6,
            "a cell swapped in under a song looping whole comes in on the beat (" + juce::String(deck.position.load() / 24000.0, 3)
            + " beats)");
    require(deck.loopActive.load() && deck.loopStart.load() == 0.0 && deck.loopEnd.load() == static_cast<double>(cell->length()),
            "and loops whole itself");
}

// A jump and a loop's wrap crossfade over five milliseconds, whatever the
// block size: on a sine no change from one sample to the next is much larger
// than the sine's own, on a constant level nothing moves at all - the fade is
// one of equal gains, with no dip - and on a ramp the jump is spread evenly
// over exactly the declick.
void DjDeckTest::declick()
{
    const auto sine = toneTrack(220.0, deviceRate, 10.0);
    const auto level = constantTrack(10.0, 0.5f);
    const auto ramp = rampTrack(deviceRate, 10.0);
    for (const auto frames : {block, 64})
        for (const auto wrap : {false, true})
        {
            const auto what = juce::String(wrap ? "a wrap" : "a jump") + " in blocks of " + juce::String(frames);
            const auto playThrough = [&](const DjTrack& material)
            {
                DjDeck deck;
                prepare(deck, frames);
                deck.setSource(&material, false);
                deck.seek(100000.0);
                start(deck);
                const auto steady = render(deck, 4096 / frames, frames);
                // A loop whose length is no whole number of the sine's periods,
                // so the wrap is a step without its fade; or a jump to a frame
                // that is not.
                if (wrap)
                    deck.setLoop(100000.0 + 4096.0, 100000.0 + 4096.0 + 1500.0 + 0.37, true);
                else
                    deck.seek(300000.37);
                const auto after = render(deck, 4096 / frames, frames);
                return std::pair<Output, Output>(steady, after);
            };
            const auto [sineSteady, sineAfter] = playThrough(*sine);
            const auto own = largestStep(sineSteady.left, 256);
            const auto worst = largestStepAcross(sineSteady.left, sineAfter.left);
            require(worst < own * 1.5f, "the sine through " + what + " changes no faster than 1.5 times its own ("
                                        + juce::String(worst, 4) + " against " + juce::String(own, 4) + ")");
            const auto [levelSteady, levelAfter] = playThrough(*level);
            require(largestStepAcross(levelSteady.left, levelAfter.left) < 1.0e-6f, "a constant level through " + what + " does not move");
            if (wrap) continue;
            const auto [rampSteady, rampAfter] = playThrough(*ramp);
            // The jump moves the ramp from about frame 104,096 to 300,000.37:
            // spread over the declick, each sample moves a 240th of that.
            const auto jumpSize = static_cast<float>((300000.37 - (100000.0 + 4096.0)) / 480000.0);
            require(largestStepAcross(rampSteady.left, rampAfter.left) < jumpSize / static_cast<float>(declickFrames) * 1.02f + 1.0e-5f,
                    "a ramp through " + what + " is spread over the declick");
            const auto settled = static_cast<double>(rampAfter.left[static_cast<size_t>(declickFrames)]) * 480000.0;
            require(std::abs(settled - (300000.37 + declickFrames)) < 0.1,
                    "and is the new position's alone once five milliseconds have passed (" + what + ")");
        }

    // A constant level played to the end of its material fades out over the
    // stop's two milliseconds rather than stepping to nothing, and one taken
    // off the deck while heard fades out over the declick.
    {
        DjDeck deck;
        prepare(deck);
        deck.setSource(level.get(), false);
        deck.seek(static_cast<double>(level->length()) - 1000.0);
        start(deck);
        const auto out = render(deck, 4);
        require(deck.currentState() == DjDeck::State::stopped && rms(out.left, out.left.size() - 512) == 0.0f,
                "a deck at the end of its material stops");
        require(largestStep(out.left, 200) < 0.5f / 96.0f * 1.01f, "fading out at its end rather than stepping");
        deck.seek(100000.0);
        start(deck);
        const auto heard = render(deck, 2);
        deck.setSource(nullptr, false);
        const auto ejected = render(deck, 2);
        require(largestStepAcross(heard.left, ejected.left) < 0.5f / static_cast<float>(declickFrames) * 1.01f
                    && rms(ejected.left, 256) == 0.0f,
                "and one emptied while heard fades its material out");
    }
}

// A jump inside another discontinuity's fade fades from what was heard, that
// fade included. A loop wraps at a known frame and a jump lands on the frame
// before (the loop left before it wraps), on the wrap's own frame, a frame on,
// five on and half way through the wrap's fade; and two jumps land fifty
// frames apart. On a ramp no change from one sample to the next is larger than
// the declick spreading the jump and the loop's seam together allows - a fade
// cut short dropped what it faded from and stepped by up to the whole seam -
// and on a constant level nothing moves. In blocks of 512 and of 64.
void DjDeckTest::fadesUnderFades()
{
    const auto ramp = rampTrack(deviceRate, 10.0);   // 480,000 frames, the left channel frame / 480,000
    const auto level = constantTrack(10.0, 0.5f);
    constexpr double loopStart = 60000.0, loopEnd = 70000.0, cue = 300000.0, secondCue = 200000.0;
    // The frame the wrap first reads the loop's start, off the boundary of
    // either block size and well past the start's fade in.
    constexpr int wrapFrame = 4 * block + 37;
    const auto length = static_cast<float>(ramp->length());
    for (const auto frames : {block, 64})
    {
        const auto play = [&](const DjTrack& material, int landing, bool second)
        {
            DjDeck deck;
            prepare(deck, frames);
            deck.setSource(&material, false);
            deck.setLoop(loopStart, loopEnd, true);
            deck.setHotCuePoint(0, cue);
            deck.setHotCuePoint(1, secondCue);
            deck.seek(loopEnd - wrapFrame);
            start(deck);
            Output out;
            for (int b = 0; b * frames < wrapFrame + 3 * block; ++b)
            {
                if (landing / frames == b)
                {
                    deck.hotCue(0, false);
                    deck.landPending(landing % frames);
                }
                // The second jump at the top of the next block, as a
                // command lands, at most a block on.
                if (second && landing / frames + 1 == b)
                    deck.seek(secondCue);
                const auto o = render(deck, 1, frames);
                out.left.insert(out.left.end(), o.left.begin(), o.left.end());
            }
            return out;
        };
        const auto what = [frames](const juce::String& landing) { return landing + ", in blocks of " + juce::String(frames); };
        for (const auto after : {-1, 0, 1, 5, 120})
        {
            const auto landing = wrapFrame + after;
            const auto out = play(*ramp, landing, false);
            // Past the loop's end and its start, as the material reads them at
            // the landing; the jump crossfades from their mix to the cue.
            const auto past = loopEnd + after, into = loopStart + after;
            const auto allowed = static_cast<float>((std::max(std::abs(cue - past), std::abs(cue - into)) + (loopEnd - loopStart)) / length)
                               / static_cast<float>(declickFrames) * 1.05f + 2.0f / length;
            const auto worst = largestStep(out.left, static_cast<size_t>(wrapFrame - 200));
            const auto name = what(after < 0 ? juce::String("a jump the frame before a wrap")
                                             : "a jump " + juce::String(after) + " frames into a wrap's fade");
            require(worst < allowed, name + " steps by no more than the declick allows (" + juce::String(worst, 5) + " against "
                                         + juce::String(allowed, 5) + ")");
            const auto settled = static_cast<double>(out.left[static_cast<size_t>(landing + declickFrames)]) * length;
            require(std::abs(settled - (cue + declickFrames)) < 0.1, name + " plays the cue alone once its fade has passed");
            const auto flat = play(*level, landing, false);
            require(largestStep(flat.left, static_cast<size_t>(wrapFrame - 200)) < 1.0e-6f, name + " moves a constant level not at all");
        }
        // Two jumps: the first fifty frames before a block's end, once the
        // wrap's fade has passed, and the second at the top of the next.
        const auto first = ((wrapFrame + 300) / frames + 1) * frames - 50;
        const auto out = play(*ramp, first, true);
        const auto from = loopStart + (first - wrapFrame);
        const auto allowed = static_cast<float>((std::abs(cue - from) + std::abs(secondCue - cue)) / length)
                           / static_cast<float>(declickFrames) * 1.05f + 2.0f / length;
        const auto worst = largestStep(out.left, static_cast<size_t>(wrapFrame - 200));
        require(worst < allowed, what("two jumps fifty frames apart") + " step by no more than the declick allows ("
                                 + juce::String(worst, 5) + " against " + juce::String(allowed, 5) + ")");
    }

    // Three jumps queued for one block land as one: nothing was heard at the
    // first two targets, so the deck fades from where it was to the last.
    // Under each other, the third had let go of the first's fade.
    DjDeck deck;
    prepare(deck);
    deck.setSource(ramp.get(), false);
    deck.seek(100000.0);
    start(deck);
    const auto before = render(deck, 4);
    for (const auto target : {200000.0, 300000.0, 400000.0})
        deck.seek(target);
    const auto after = render(deck, 2);
    const auto allowed = static_cast<float>((400000.0 - (100000.0 + 4.0 * block)) / length) / static_cast<float>(declickFrames) * 1.05f
                       + 2.0f / length;
    require(largestStepAcross(before.left, after.left) < allowed,
            "three jumps in one block crossfade as one (" + juce::String(largestStepAcross(before.left, after.left), 5) + " against "
            + juce::String(allowed, 5) + ")");
}

// A whole track's loop crossed at a crawl - a hand resting on the platter,
// the end of a brake - still crossfades over the declick. Its length in frames
// at that speed, counted as an int, was past what one holds, and the wrap
// came out a single frame long: a step of the whole seam.
void DjDeckTest::crawlingWrap()
{
    DjDeck deck;
    prepare(deck);
    auto ramp = rampTrack(deviceRate, 1.0);   // 48,000 frames: the seam steps from 1 to 0
    deck.setSource(ramp.get(), false);
    deck.setLoop(0.0, static_cast<double>(ramp->length()), true);
    deck.targetRate = deck.rate = 1.0e-6;
    start(deck);
    // Four hundred frames from the loop's end at this speed.
    deck.pos = static_cast<double>(ramp->length()) - 4.0e-4;
    const auto jumpsBefore = deck.jumps.load();
    const auto out = render(deck, 4);
    require(deck.jumps.load() - jumpsBefore == 1, "a crawling deck wraps its whole-track loop");
    require(largestStep(out.left, 200) < 1.05f / static_cast<float>(declickFrames),
            "and crossfades over the declick (" + juce::String(largestStep(out.left, 200), 4) + " a sample at most)");
}

namespace
{
// The hand on the platter, through the engine that reads it.
void checkScratch()
{
    DjEngine engine;
    engine.prepare(deviceRate, block);
    activate(engine, 1);
    engine.quantise.store(static_cast<int>(DjEngine::Quantise::off));
    engine.setDeckSource(0, rampTrack(44100.0, 10.0), false);
    run(engine, 1);
    engine.push(Command::seek(0, 200000.0));
    engine.push(Command::play(0));
    run(engine, 20);
    auto& scratched = engine.deck(0);
    scratched.controls.scratchRate.store(-1.0);
    scratched.controls.scratching.store(true);
    run(engine, 30);   // the rate smooths over 30 ms
    const auto pulled = scratched.position.load();
    run(engine, 20);
    require(scratched.position.load() < pulled - 20.0 * block * 0.5, "a hand pulling the platter back runs the deck backwards");
    scratched.controls.scratchRate.store(0.0);
    run(engine, 30);
    const auto held = scratched.position.load();
    run(engine, 20);
    require(std::abs(scratched.position.load() - held) < 50.0, "a hand resting on the platter holds it still");
    scratched.controls.scratching.store(false);
    run(engine, 30);
    const auto released = scratched.position.load();
    run(engine, 20);
    require(scratched.position.load() > released + 20.0 * block * 0.5, "let go, the deck runs on");
}

// A deck heard through a track of the song: its slot's port carries the
// deck's audio and notes into the track's chain and the chain's output back
// into the strip, a block later; a file deck is delayed by the same block.
// The track's side is played here as the hidden devices play it.
void checkTrackPort()
{
    {
        DjTrackPort port;
        port.prepare(block);
        std::vector<float> l(block), r(block), outL(block), outR(block);
        for (int f = 0; f < block; ++f)
        {
            l[static_cast<size_t>(f)] = static_cast<float>(f);
            r[static_cast<size_t>(f)] = -static_cast<float>(f);
        }
        DjPortEvent sent[2] {{100, 60, 90, true}, {300, 60, 0, false}};
        port.pushFeed(l.data(), r.data(), block, sent, 2);
        DjPortEvent got[8];
        int count = 0;
        bool resynced = false;
        require(port.popFeed(outL.data(), outR.data(), block, got, 8, count, resynced) == block
                    && outL[511] == 511.0f && outR[511] == -511.0f && count == 2
                    && got[0].frame == 100 && got[0].on && got[0].velocity == 90 && got[1].frame == 300 && !got[1].on,
                "a block pushed into the feed comes out whole, its notes at their frames");
        require(port.popFeed(outL.data(), outR.data(), block, got, 8, count, resynced) == 0 && outL[10] == 0.0f && count == 0,
                "an empty feed pops silence and no notes");
        // Four blocks pushed with no reader: the reader skips to the last,
        // drops the note-ons left behind and keeps the note-offs.
        for (int b = 0; b < 4; ++b)
        {
            DjPortEvent each[2] {{10, 60 + b, 100, true}, {20, 60 + b, 0, false}};
            std::fill(l.begin(), l.end(), static_cast<float>(b + 1));
            port.pushFeed(l.data(), nullptr, block, each, 2);
        }
        require(port.feedFramesWaiting() == 4 * block, "the feed holds four blocks");
        require(port.popFeed(outL.data(), outR.data(), block, got, 8, count, resynced) == block && outL[0] == 4.0f && outR[0] == 4.0f,
                "a reader finding a backlog skips to the freshest block, mono copied to both sides");
        int offs = 0, ons = 0, onAt = -1;
        for (int e = 0; e < count; ++e)
        {
            (got[e].on ? ons : offs)++;
            if (got[e].on) onAt = static_cast<int>(got[e].frame);
        }
        require(ons == 1 && offs == 4 && got[0].frame == 0 && !got[0].on && onAt == 10
                    && !got[count - 1].on && got[count - 1].frame == 20 && !resynced,
                "the skipped blocks' note-offs land at its first frame, their note-ons are dropped, and the freshest block plays whole");
        std::fill(l.begin(), l.end(), 7.0f);
        for (int b = 0; b < 5; ++b)
            port.pushFeed(l.data(), nullptr, block, nullptr, 0);
        require(port.feedFramesWaiting() == 4 * block, "a full feed drops what it has no room for");
        // A dropped block may have held a note-off, so nothing waiting is to
        // be trusted: the reader starts again from what comes next, and is
        // told, so the track lets go of every note it played.
        require(port.popFeed(outL.data(), outR.data(), block, got, 8, count, resynced) == 0 && resynced && count == 0
                    && port.feedFramesWaiting() == 0 && outL[0] == 0.0f,
                "a feed that dropped a block starts again from what comes next, and says so");
        DjPortEvent again[1] {{5, 64, 100, true}};
        port.pushFeed(l.data(), nullptr, block, again, 1);
        require(port.popFeed(outL.data(), outR.data(), block, got, 8, count, resynced) == block && !resynced && count == 1
                    && got[0].note == 64 && got[0].frame == 5,
                "and the next block plays whole");
        // The tap the other way round.
        for (int b = 0; b < 3; ++b)
        {
            std::fill(l.begin(), l.end(), static_cast<float>(b + 1));
            port.pushTap(l.data(), l.data(), block);
        }
        require(port.popTap(outL.data(), outR.data(), block) == block && outL[0] == 3.0f,
                "the tap's reader skips its backlog too");
        require(port.popTap(outL.data(), outR.data(), 100) == 0 && outL[50] == 0.0f, "and an empty tap pops silence");
    }
    {
        DjBlockDelay delay;
        delay.prepare(block);
        std::vector<float> l(block), r(block);
        for (int f = 0; f < block; ++f) l[static_cast<size_t>(f)] = r[static_cast<size_t>(f)] = static_cast<float>(f + 1);
        delay.process(l.data(), r.data(), block);
        require(l[0] == 0.0f && l[511] == 0.0f, "a block delay's first block is silence");
        std::fill(l.begin(), l.end(), 0.0f);
        delay.process(l.data(), r.data(), block);
        require(l[0] == 1.0f && l[511] == 512.0f && r[511] == 512.0f, "and the next block is the one before");
        delay.process(l.data(), r.data(), 64);
        require(l[0] == 0.0f, "a block of another size starts over with silence");
    }

    DjEngine engine;
    engine.prepare(deviceRate, block);
    activate(engine, 1);
    engine.quantise.store(static_cast<int>(DjEngine::Quantise::off));
    auto material = rampTrack(48000.0, 20.0, 120.0);   // 24000 frames a beat
    material->midi = {{1.0, 60, 100, true}, {1.5, 60, 0, false}, {2.0, 62, 90, true}, {3.0, 62, 0, false}};
    engine.setDeckSource(0, std::move(material), false);
    auto& port = engine.port(0);
    port.bound.store(true);
    run(engine, 1);
    engine.push(Command::play(0));
    struct Event { int note = 0, velocity = 0, block = 0, frame = 0; bool on = false; };
    std::vector<Event> events;
    const auto find = [&events](int note, bool on, size_t skip = 0) -> const Event*
    {
        for (const auto& e : events)
            if (e.note == note && e.on == on && skip-- == 0)
                return &e;
        return nullptr;
    };
    std::vector<float> l(block), r(block), tapL(block), tapR(block);
    DjPortEvent popped[64];
    // The track's side, as the hidden devices play it: the feed popped and
    // the chain's output - a constant here - pushed into the tap.
    const auto track = [&](int b)
    {
        int count = 0;
        bool resynced = false;
        port.popFeed(l.data(), r.data(), block, popped, 64, count, resynced);
        for (int e = 0; e < count; ++e)
            events.push_back({popped[e].note, popped[e].velocity, b, static_cast<int>(popped[e].frame), popped[e].on});
        std::fill(tapL.begin(), tapL.end(), 0.25f);
        std::fill(tapR.begin(), tapR.end(), 0.25f);
        port.pushTap(tapL.data(), tapR.data(), block);
    };
    float loudest = 0.0f, quietest = 1.0f;
    for (int b = 0; b < 100; ++b)
    {
        const auto out = run(engine, 1);
        track(b);
        if (b >= 20)
        {
            // The strip's crossovers have settled: the deck is the chain's
            // constant, not its own ramp.
            for (const auto s : out.left)
            {
                loudest = std::max(loudest, s);
                quietest = std::min(quietest, s);
            }
        }
    }
    require(loudest < 0.27f && quietest > 0.23f,
            "a deck on a port is heard as what its track's chain gives back (" + juce::String(quietest, 3) + " to "
            + juce::String(loudest, 3) + ")");
    // A block is 512 frames and a beat 24000, so beat 1 falls in block 46 at
    // frame 448, and beat 1.5 in block 70 at frame 160.
    const auto* on60 = find(60, true);
    const auto* off60 = find(60, false);
    const auto* on62 = find(62, true);
    require(on60 != nullptr && on60->block == 46 && std::abs(on60->frame - 448) <= 2 && on60->velocity == 100,
            "the note at beat 1 reaches the track in the block that passes it, at its frame (block "
            + juce::String(on60 != nullptr ? on60->block : -1) + ", frame " + juce::String(on60 != nullptr ? on60->frame : -1) + ")");
    require(off60 != nullptr && off60->block == 70 && std::abs(off60->frame - 160) <= 2, "and ends at beat 1.5, at its frame");
    require(on62 != nullptr && on62->block == 93, "the note at beat 2 follows");
    require(find(62, false) == nullptr, "and is still held at beat 3 minus a little");
    // A stop lets the held note go.
    engine.push(Command::pause(0));
    run(engine, 1);
    track(100);
    require(find(62, false) != nullptr && find(62, false)->block == 100, "a stop lets the held note go");
    // A loop plays the notes round: two beats long, the note at beat 1 comes
    // again a loop later.
    run(engine, 2);
    engine.push(Command::setLoop(0, 0.0, 48000.0, true));
    engine.push(Command::seek(0, 0.0));
    events.clear();
    engine.push(Command::play(0));
    for (int b = 0; b < 200; ++b)
    {
        run(engine, 1);
        track(b);
    }
    const auto* first = find(60, true);
    const auto* again = find(60, true, 1);
    require(first != nullptr && again != nullptr && std::abs((again->block - first->block) - 94) <= 2,
            "a two-beat loop plays the note again a loop later (" + juce::String(first != nullptr && again != nullptr ? again->block - first->block : -1) + " blocks)");
    // New material while a note is held: the note is let go, since the new
    // material may never end it. Back to the top, and on until the note at
    // beat 1 sounds.
    engine.push(Command::seek(0, 0.0));
    run(engine, 1);
    track(300);
    events.clear();
    int at = 301;
    for (; at < 301 + 80 && find(60, true) == nullptr; ++at)
    {
        run(engine, 1);
        track(at);
    }
    require(find(60, true) != nullptr && find(60, false) == nullptr, "the note at beat 1 sounds");
    auto replaced = rampTrack(48000.0, 20.0, 120.0);
    replaced->midi = {{10.0, 64, 100, true}, {11.0, 64, 0, false}};
    engine.setDeckSource(0, std::move(replaced), true);
    run(engine, 1);
    track(at);
    engine.collect();
    require(find(60, false) != nullptr && find(60, false)->block == at, "and new material lets it go at once");
    port.bound.store(false);
    engine.push(Command::pause(0));
    run(engine, 2);
}

// The hand-over of material: through the queue, and back to the message
// thread only when the audio thread says it is done with it - at the block
// that takes new material up for a stopped deck, when the crossfade ends for
// a playing one, a restart in between losing nothing - and a ring that fills
// keeps the rest for the next block.
void checkEngineHousekeeping()
{
    DjEngine engine;
    const auto generation = engine.generation();
    engine.prepare(deviceRate, block);
    require(engine.sampleRate() == deviceRate && engine.maximumBlockSize() == block && engine.generation() == generation + 1,
            "the engine says what it was prepared for, and counts its preparations");
    activate(engine, 1);
    engine.setDeckSource(0, rampTrack(44100.0, 2.0), false);
    require(engine.trackOf(0) != nullptr && engine.deck(0).currentState() == DjDeck::State::empty,
            "material handed over is the slot's at once for the displays, and the deck's at its next block");
    run(engine, 1);
    require(engine.deck(0).currentState() == DjDeck::State::stopped, "a track given to the engine reaches its deck");
    engine.setDeckSource(0, rampTrack(44100.0, 2.0), false);
    require(engine.collect() == 1, "the material let go of is kept until the audio thread hands it back");
    run(engine, 1);
    require(engine.collect() == 0, "which a stopped deck does at the block that takes the new up");

    engine.prepare(deviceRate, 64);
    engine.push(Command::play(0));
    run(engine, 4, 64);
    engine.setDeckSource(0, rampTrack(44100.0, 2.0), false);
    int blocks = 0;
    while (blocks < 20)
    {
        run(engine, 1, 64);
        ++blocks;
        if (engine.collect() == 0) break;
    }
    require(blocks == (declickFrames + 63) / 64, "a playing deck hands it back the block its crossfade ends, five milliseconds on ("
                                                 + juce::String(blocks) + " blocks of 64)");
    // A device restart prepares the engine again between the swap and the
    // block that takes it up, and that block still crossfades from the old.
    engine.setDeckSource(0, rampTrack(44100.0, 2.0), false);
    engine.prepare(deviceRate, block);
    require(engine.collect() == 1, "a restart keeps the material the next block crossfades from");
    require(engine.generation() == generation + 3, "and counts as a preparation");
    run(engine, 1);
    require(engine.collect() == 0, "and the block after it frees it");

    // Seventy materials handed to a stopped deck in one block let seventy go
    // at once: the ring takes sixty-four, the rest go a block later.
    engine.push(Command::pause(0));
    run(engine, 2);
    for (int i = 0; i < 70; ++i)
        engine.setDeckSource(0, rampTrack(44100.0, 0.1), false);
    run(engine, 1);
    require(engine.collect() == 70 - DjReleaseQueue::ringSize, "a full ring keeps the rest (" + juce::String(engine.collect())
                                                                     + " still out)");
    run(engine, 1);
    require(engine.collect() == 0, "and hands them back at the next block");

    engine.setDeckSource(0, rampTrack(44100.0, 10.0), false);
    engine.push(Command::seek(0, 0.0));
    engine.push(Command::play(0));
    const auto out = run(engine, 10);
    require(rms(out.right) > 0.1f, "the engine plays its deck");
    // The queue refuses when full and nothing is lost silently, material
    // included: refused, it is not held, and it is still the caller's to hand
    // over again rather than make again.
    int pushed = 0;
    while (engine.push(Command::pause(0))) ++pushed;
    require(pushed == DjEngine::queueSize - 1, "the command queue holds a block's worth and says when it is full");
    auto refused = rampTrack(44100.0, 10.0);
    require(!engine.setDeckSource(0, std::move(refused), false) && refused != nullptr && engine.collect() == 0,
            "a source refused is not held, and stays with the caller");
    run(engine, 1);
    require(engine.pendingCommands() == 0, "a block drains the queue");
    require(engine.deck(0).currentState() == DjDeck::State::stopped, "and the commands were applied");
    const auto* kept = refused.get();
    require(engine.setDeckSource(0, std::move(refused), false) && refused == nullptr && engine.trackOf(0) == kept,
            "handed over again once there is room");
    run(engine, 1);
    // A block bigger than the engine was prepared for is worked in pieces.
    std::vector<float> l(block * 3), r(block * 3);
    float* outputs[2] {l.data(), r.data()};
    engine.push(Command::play(0));
    engine.process(outputs, 2, block * 3);
    engine.process(outputs, 2, block * 3);
    require(rms(r) > 0.1f && engine.deck(0).isPlaying(), "an oversized block is rendered whole");

    // A slot whose letting go met a full queue plays on; taken up again, as a
    // new deck takes it, it starts empty rather than with what it played.
    while (engine.push(Command::pause(1))) {}
    require(!engine.setSlotActive(0, false), "letting a slot go is refused while the queue is full");
    run(engine, 1);
    require(engine.deck(0).isPlaying(), "and the deck in it plays on");
    require(engine.setSlotActive(0, true) && engine.trackOf(0) == nullptr, "taken up again, the slot's material is the engine's to hand back");
    run(engine, 1);
    require(engine.deck(0).currentState() == DjDeck::State::empty && engine.collect() == 0,
            "and the slot starts empty, its material handed back");
}

// Every command is applied at the top of the block in the order it was
// queued, material included: a seek queued before new material moves the old
// one, and the swap carries the position across; one queued after moves the
// new. Material used to be taken up ahead of any command for its deck.
void checkCommandOrder()
{
    DjEngine engine;
    engine.prepare(deviceRate, block);
    activate(engine, 1);
    engine.setDeckSource(0, rampTrack(48000.0, 10.0), false);
    run(engine, 1);
    engine.push(Command::seek(0, 100000.0));
    engine.setDeckSource(0, rampTrack(96000.0, 10.0), false);
    run(engine, 1);
    require(std::abs(engine.deck(0).position.load() - 200000.0) < 1.0,
            "a seek queued before new material moves the old, and the swap carries it across by time ("
            + juce::String(engine.deck(0).position.load()) + ")");
    engine.setDeckSource(0, rampTrack(48000.0, 10.0), false);
    engine.push(Command::seek(0, 30000.0));
    run(engine, 1);
    require(std::abs(engine.deck(0).position.load() - 30000.0) < 1.0, "and one queued after moves the new");
}

// A start waits for the master, keeps its position and lands in phase with
// the master's bar; sync follows the master's tempo and phase; and a synced
// deck that takes over as master keeps the tempo it was playing at until its
// fader moves.
void checkQuantisedStartAndSync()
{
    DjEngine engine;
    engine.prepare(deviceRate, block);
    activate(engine, 2);
    engine.quantise.store(static_cast<int>(DjEngine::Quantise::bar));
    engine.setDeckSource(0, rampTrack(48000.0, 40.0, 120.0), false);   // 24000 frames a beat
    engine.setDeckSource(1, rampTrack(48000.0, 40.0, 125.0), false);   // 23040 frames a beat
    run(engine, 1);
    auto& master = engine.deck(0);
    auto& slave = engine.deck(1);
    engine.push(Command::play(0));
    run(engine, 2);
    require(engine.masterDeck.load() == 0, "the first deck to play is the master");
    // Deck 1 asks to start a little way into its second bar: it waits until
    // the master is as far into a bar, and starts exactly where it was asked.
    const auto target = 23040.0 * 4.0 + 5000.0;
    engine.push(Command::seek(1, target));
    engine.push(Command::play(1));
    run(engine, 1);
    require(slave.currentState() == DjDeck::State::waiting, "a quantised start waits for the master");
    int blocks = 0;
    while (slave.currentState() == DjDeck::State::waiting && blocks < 400)
    {
        run(engine, 1);
        ++blocks;
    }
    require(slave.currentState() == DjDeck::State::playing, "and starts within a bar");
    // Both play at the rate of their own material, a frame a frame, so how
    // far the slave got says the frame it started at.
    const auto played = slave.position.load() - target;
    require(played >= 0.0 && played <= block && played == std::floor(played),
            "a quantised start begins exactly where it was asked (" + juce::String(played) + " frames on)");
    const auto masterAtStart = master.position.load() - played;
    const auto masterPhase = phaseOf(masterAtStart / 24000.0, 4.0);
    const auto targetPhase = phaseOf(target / 23040.0, 4.0);
    require(phaseDistance(masterPhase, targetPhase, 4.0) < 1.0e-3,
            "as the master reaches the same place in its bar (" + juce::String(masterPhase, 4) + " beats, the deck's "
            + juce::String(targetPhase, 4) + ")");

    // Sync: the slave follows the master's tempo, then the master's fader.
    slave.controls.synced.store(true);
    engine.push(Command::syncAlign(1));
    run(engine, 60);
    const auto rate = slave.playbackRate.load();
    require(std::abs(rate - 120.0 / 125.0) < 0.003, "a synced deck plays at the master's tempo (" + juce::String(rate, 4) + ")");
    master.controls.tempoPercent.store(5.0f);
    run(engine, 100);
    const auto followed = slave.playbackRate.load();
    require(std::abs(followed - 120.0 * 1.05 / 125.0) < 0.003, "and follows the master's tempo fader (" + juce::String(followed, 4) + ")");
    auto drift = master.beatPosition() - slave.beatPosition();
    drift -= std::round(drift);
    require(std::abs(drift) < 0.01, "phase lock holds the beats together (" + juce::String(drift, 4) + " beats apart)");

    // The master stopping passes the job on, and the deck that takes it keeps
    // the tempo it was synced to - with its fader showing it - until the
    // fader moves.
    engine.push(Command::pause(0));
    run(engine, 3);
    require(engine.masterDeck.load() == 1, "when the master stops the playing deck becomes master");
    run(engine, 40);
    require(std::abs(slave.playbackRate.load() - 1.008) < 0.001 && std::abs(slave.heldTempoRate.load() - 1.008) < 1.0e-9,
            "and keeps the tempo it was synced to (" + juce::String(slave.playbackRate.load(), 4) + ")");
    slave.controls.tempoPercent.store(2.0f);
    engine.push(Command::tempoMoved(1));
    run(engine, 40);
    require(std::abs(slave.playbackRate.load() - 1.02) < 0.001 && slave.heldTempoRate.load() == 0.0,
            "until its fader moves, which it then follows (" + juce::String(slave.playbackRate.load(), 4) + ")");

    // No quantisation: a start is immediate.
    engine.quantise.store(static_cast<int>(DjEngine::Quantise::off));
    engine.push(Command::play(0));
    run(engine, 1);
    require(master.currentState() == DjDeck::State::playing, "with quantise off a start is immediate");
}

// A quantised hot cue keeps its target and lands at the exact frame where
// the master's phase within the bar reaches the target's, inside the block;
// the master's own hot cue lands against its own grid, so its phase runs on
// through the jump.
void checkQuantisedJumps()
{
    DjEngine engine;
    engine.prepare(deviceRate, block);
    activate(engine, 2);
    engine.quantise.store(static_cast<int>(DjEngine::Quantise::bar));
    engine.setDeckSource(0, rampTrack(48000.0, 40.0, 120.0), false);
    engine.setDeckSource(1, rampTrack(48000.0, 40.0, 120.0), false);
    run(engine, 1);
    auto& master = engine.deck(0);
    auto& other = engine.deck(1);
    engine.push(Command::play(0));
    run(engine, 2);
    engine.quantise.store(static_cast<int>(DjEngine::Quantise::off));
    engine.push(Command::seek(1, 30000.0));
    engine.push(Command::play(1));
    run(engine, 30);
    engine.quantise.store(static_cast<int>(DjEngine::Quantise::bar));
    require(engine.masterDeck.load() == 0 && other.isPlaying(), "two decks play, the first master");
    // Beat 33 is the second beat of a bar: no bar line, so no snapping it.
    const auto cue = 33.0 * 24000.0;
    const auto landing = [&](DjDeck& deck, int slot, const char* what)
    {
        engine.push(Command::setHotCue(slot, 0, cue));
        engine.push(Command::hotCue(slot, 0));
        double masterBefore = 0.0, deckBefore = 0.0;
        for (int b = 0; b < 400; ++b)
        {
            masterBefore = master.position.load();
            deckBefore = deck.position.load();
            run(engine, 1);
            if (deck.currentPending() == DjDeck::Pending::none) break;
        }
        require(deck.currentPending() == DjDeck::Pending::none, juce::String(what) + " lands within a bar");
        // Frames since the landing, from how far past the cue the deck got.
        const auto since = deck.position.load() - cue;
        require(since >= 0.0 && since <= block && since == std::floor(since),
                juce::String(what) + " lands exactly on its cue (" + juce::String(since) + " frames on)");
        return std::pair<double, double>(since, &deck == &master ? deckBefore : masterBefore);
    };
    // The other deck: it lands as the master reaches the second beat of a bar.
    {
        const auto [since, masterBefore] = landing(other, 1, "a hot cue on a deck following the master");
        const auto landedAt = block - since;
        const auto masterPhase = phaseOf((masterBefore + landedAt) / 24000.0, 4.0);
        require(phaseDistance(masterPhase, 1.0, 4.0) < 1.0e-3,
                "at the frame the master reaches the cue's place in its bar (" + juce::String(masterPhase, 4) + " beats into it, at frame "
                + juce::String(landedAt) + " of the block)");
    }
    // The master: it lands as its own phase reaches the cue's, and its phase
    // runs on through the jump as though it had not jumped.
    {
        const auto [since, before] = landing(master, 0, "the master's own hot cue");
        const auto expected = phaseOf((before + block) / 24000.0, 4.0);
        const auto actual = phaseOf(master.position.load() / 24000.0, 4.0);
        require(phaseDistance(expected, actual, 4.0) < 1.0e-3,
                "and its phase runs on through the jump (" + juce::String(actual, 4) + " beats into the bar, "
                + juce::String(expected, 4) + " without the jump)");
    }
}

// A master looping a beat off the bar line never passes the bar line's phase,
// so its own hot cue waiting for the bar line lands on the beat's phase - at
// the loop's wrap - within one loop, the master's beat running on through it;
// and another deck's start waiting for the bar line against such a master
// lands within the loop too. Both waited for as long as the loop ran.
void checkLandingsInLoops()
{
    {
        DjEngine engine;
        engine.prepare(deviceRate, block);
        activate(engine, 2);
        engine.quantise.store(static_cast<int>(DjEngine::Quantise::bar));
        engine.setDeckSource(0, rampTrack(48000.0, 60.0, 120.0), false);   // 24000 frames a beat
        engine.setDeckSource(1, rampTrack(48000.0, 60.0, 120.0), false);
        run(engine, 1);
        auto& master = engine.deck(0);
        auto& other = engine.deck(1);
        const auto loopOffTheBarLine = [&](double beat)
        {
            engine.push(Command::seek(0, beat * 24000.0));
            engine.push(Command::play(0));
            engine.push(Command::beatLoop(0, 1.0));
            run(engine, 2);
            const auto start = std::floor(beat) * 24000.0;
            require(master.isPlaying() && master.loopActive.load() && master.loopStart.load() == start
                        && master.loopEnd.load() == start + 24000.0,
                    "the master loops a beat on its bar's third beat");
        };
        // Waits for a deck's pending landing, a block at a time, and says
        // how many blocks it took and where the master stood before the last.
        const auto waitFor = [&](DjDeck& deck, double& masterBefore)
        {
            int blocks = 0;
            do
            {
                masterBefore = master.position.load();
                run(engine, 1);
                ++blocks;
            } while (blocks < 60 && deck.currentPending() != DjDeck::Pending::none);
            return blocks;
        };
        loopOffTheBarLine(34.2);
        engine.push(Command::setHotCue(0, 0, 64.0 * 24000.0));
        engine.push(Command::hotCue(0, 0));
        double before = 0.0;
        auto blocks = waitFor(master, before);
        require(master.currentPending() == DjDeck::Pending::none && blocks <= 24000 / block + 1,
                "the master's own hot cue on a bar line lands within its one-beat loop (" + juce::String(blocks) + " blocks)");
        const auto since = master.position.load() - 64.0 * 24000.0;
        require(since >= 0.0 && since <= block && since == std::floor(since), "exactly on its cue (" + juce::String(since) + " frames on)");
        const auto expected = phaseOf((before + block) / 24000.0, 1.0);
        const auto actual = phaseOf(master.position.load() / 24000.0, 1.0);
        require(phaseDistance(expected, actual, 1.0) < 1.0e-3,
                "on the beat's phase, which runs on through the jump (" + juce::String(actual, 4) + " into the beat, "
                + juce::String(expected, 4) + " without it)");

        loopOffTheBarLine(66.2);
        engine.push(Command::setHotCue(1, 0, 16.0 * 24000.0));
        engine.push(Command::hotCue(1, 0));
        blocks = waitFor(other, before);
        require(other.isPlaying() && blocks <= 24000 / block + 1,
                "another deck's start on a bar line lands within the master's one-beat loop (" + juce::String(blocks) + " blocks)");
        const auto played = other.position.load() - 16.0 * 24000.0;
        require(played >= 0.0 && played <= block && played == std::floor(played), "exactly where it was asked");
        const auto masterPhase = phaseOf((before + (block - played)) / 24000.0, 1.0);
        require(phaseDistance(masterPhase, 0.0, 1.0) < 1.0e-3,
                "as the master's beat comes round (" + juce::String(masterPhase, 4) + " into the beat)");
    }

    // A master looping a bar lands a bar-line hot cue on the loop's seam: on
    // the very frame its wrap reads the loop's start again when the loop's end
    // is a whole number of frames away, as it is for material at the device's
    // rate, and on the frame before for 44.1 kHz material. Either way the jump
    // crossfades from what was heard there - landing on the wrap's frame once
    // cut the wrap's own fade - so a sine whose loop is no whole number of
    // periods, its seam a step, changes no faster through it than 1.5 times
    // its own steepest step.
    for (const auto materialRate : {48000.0, 44100.0})
    {
        DjEngine engine;
        engine.prepare(deviceRate, block);
        activate(engine, 1);
        engine.quantise.store(static_cast<int>(DjEngine::Quantise::bar));
        auto sine = toneTrack(220.0 + 1.0 / 6.0, materialRate, 40.0);
        sine->analysis.bpm = 120.0;
        sine->analysis.firstBeatSeconds = 0.0;
        sine->analysis.beatsPerBar = 4;
        const auto beat = materialRate / 2.0;
        engine.setDeckSource(0, std::move(sine), false);
        run(engine, 1);
        engine.push(Command::setLoop(0, 32.0 * beat, 36.0 * beat, true));
        engine.push(Command::seek(0, 35.5 * beat));
        engine.push(Command::play(0));
        const auto steady = run(engine, 12);
        engine.push(Command::setHotCue(0, 0, 64.0 * beat));
        engine.push(Command::hotCue(0, 0));
        Output after;
        int blocks = 0;
        do
        {
            const auto out = run(engine, 1);
            after.left.insert(after.left.end(), out.left.begin(), out.left.end());
        } while (++blocks < 40 && engine.deck(0).currentPending() != DjDeck::Pending::none);
        const auto rateName = juce::String(materialRate / 1000.0, 1) + " kHz";
        require(engine.deck(0).currentPending() == DjDeck::Pending::none && !engine.deck(0).loopActive.load(),
                "a bar-line hot cue lands out of the master's bar loop, " + rateName);
        const auto tail = run(engine, 2);
        after.left.insert(after.left.end(), tail.left.begin(), tail.left.end());
        const auto own = largestStep(steady.left, static_cast<size_t>(2 * block));
        const auto worst = largestStepAcross(steady.left, after.left);
        require(worst < own * 1.5f, "landing on the loop's seam, " + rateName + ", it changes no faster than 1.5 times its own ("
                                    + juce::String(worst, 4) + " against " + juce::String(own, 4) + ")");
    }
}

// Sync follows the master's tempo, never its platter: a master braked to a
// stop or played backwards drags no synced deck with it, and the phase
// correction waits until both run steadily again. An 87 BPM track synced to
// 174 plays at its own tempo, its beat on every other one of the master's.
void checkSyncFollowsTempo()
{
    const auto synced = [](double masterBpm, double ownBpm, DjEngine& engine)
    {
        engine.prepare(deviceRate, block);
        activate(engine, 2);
        engine.quantise.store(static_cast<int>(DjEngine::Quantise::off));
        engine.setDeckSource(0, rampTrack(48000.0, 40.0, masterBpm), false);
        engine.setDeckSource(1, rampTrack(48000.0, 40.0, ownBpm), false);
        run(engine, 1);
        engine.push(Command::play(0));
        engine.push(Command::play(1));
        run(engine, 2);
        engine.deck(1).controls.synced.store(true);
        engine.push(Command::syncAlign(1));
        run(engine, 200);
    };
    {
        DjEngine engine;
        synced(120.0, 125.0, engine);
        engine.deck(0).controls.brakeSeconds.store(2.0f);
        engine.push(Command::pause(0));
        double worst = 0.0;
        for (int b = 0; b < 170 && engine.deck(0).isPlaying(); ++b)
        {
            run(engine, 1);
            worst = std::max(worst, std::abs(engine.deck(1).playbackRate.load() / 0.96 - 1.0));
        }
        require(worst < 0.005, "a synced deck holds its tempo while the master brakes over two seconds (off by "
                               + juce::String(worst * 100.0, 2) + " per cent at worst)");
    }
    {
        DjEngine engine;
        synced(120.0, 125.0, engine);
        engine.deck(0).controls.reversed.store(true);
        double worst = 0.0;
        for (int b = 0; b < 188; ++b)
        {
            run(engine, 1);
            worst = std::max(worst, std::abs(engine.deck(1).playbackRate.load() / 0.96 - 1.0));
        }
        require(worst < 0.001, "and while the master plays a bar backwards (off by " + juce::String(worst * 100.0, 3) + " per cent at worst)");
    }
    {
        DjEngine engine;
        synced(174.0, 87.0, engine);
        run(engine, 200);
        const auto rate = engine.deck(1).playbackRate.load();
        require(std::abs(rate - 1.0) < 0.003, "87 BPM synced to 174 plays at its own tempo (" + juce::String(rate, 4) + ")");
        const auto lockError = [&engine]
        {
            auto error = engine.deck(0).beatPosition() * 0.5 - engine.deck(1).beatPosition();
            return error - std::round(error);
        };
        require(std::abs(lockError()) < 0.01, "on every other beat of the master's (" + juce::String(lockError(), 4) + " beats off)");
        // Quantised, a hot cue on the second beat of its bar, and a start
        // from the second beat of another, land on the beat the lock holds
        // the deck to: timed by the master's beat folded as the lock folds
        // it, not half a beat off it, which the lock then took seconds to
        // pull back.
        engine.quantise.store(static_cast<int>(DjEngine::Quantise::bar));
        const auto framesPerBeat = 48000.0 * 60.0 / 87.0;
        const auto lands = [&engine](const char* what)
        {
            int blocks = 0;
            do
            {
                run(engine, 1);
            } while (++blocks < 600 && engine.deck(1).currentPending() != DjDeck::Pending::none);
            require(engine.deck(1).isPlaying() && engine.deck(1).currentPending() == DjDeck::Pending::none,
                    juce::String(what) + " on a deck synced an octave down lands");
        };
        engine.push(Command::setHotCue(1, 0, 33.0 * framesPerBeat));
        engine.push(Command::hotCue(1, 0));
        lands("a hot cue");
        require(std::abs(lockError()) < 0.01, "a hot cue lands on the beat the lock holds it to (" + juce::String(lockError(), 4) + " beats off)");
        engine.push(Command::pause(1));
        run(engine, 2);
        engine.push(Command::setHotCue(1, 1, 37.0 * framesPerBeat));
        engine.push(Command::hotCue(1, 1));
        lands("a start");
        require(std::abs(lockError()) < 0.01, "and so does a start (" + juce::String(lockError(), 4) + " beats off)");
    }
}

// Sync pressed on a deck playing in a loop keeps it in the loop and in phase:
// a beat looped while the master stands 1.8 beats into its bar lands 0.8 into
// the beat, and a bar looped just short of its end lands a bar back, on the
// master's place in the bar. Both used to leave their loops.
void checkSyncInLoops()
{
    struct Case { double loopBeats, deckBeat, masterBeat; };
    for (const auto c : {Case {1.0, 32.3, 41.8}, Case {4.0, 35.9, 40.1}})
    {
        DjEngine engine;
        engine.prepare(deviceRate, block);
        activate(engine, 2);
        engine.quantise.store(static_cast<int>(DjEngine::Quantise::off));
        engine.setDeckSource(0, rampTrack(48000.0, 60.0, 120.0), false);   // 24000 frames a beat
        engine.setDeckSource(1, rampTrack(48000.0, 60.0, 120.0), false);
        run(engine, 1);
        const auto loopStart = 32.0 * 24000.0, loopEnd = (32.0 + c.loopBeats) * 24000.0;
        engine.push(Command::seek(0, c.masterBeat * 24000.0));
        engine.push(Command::play(0));
        engine.push(Command::setLoop(1, loopStart, loopEnd, true));
        engine.push(Command::seek(1, c.deckBeat * 24000.0));
        engine.push(Command::play(1));
        run(engine, 2);
        auto& master = engine.deck(0);
        auto& d = engine.deck(1);
        require(engine.masterDeck.load() == 0 && d.isPlaying() && d.loopActive.load(), "a deck plays in a loop beside the master");
        d.controls.synced.store(true);
        engine.push(Command::syncAlign(1));
        run(engine, 1);
        const auto what = juce::String(c.loopBeats, 0) + "-beat loop";
        require(d.loopActive.load() && d.position.load() >= loopStart && d.position.load() < loopEnd,
                "Sync keeps a deck playing in a " + what + " inside it (" + juce::String(d.position.load() / 24000.0, 3) + " beats)");
        const auto span = c.loopBeats >= 4.0 ? 4.0 : 1.0;
        const auto masterPhase = phaseOf(master.beatPosition(), span), deckPhase = phaseOf(d.beatPosition(), span);
        require(phaseDistance(masterPhase, deckPhase, span) < 1.0e-3,
                "and on the master's phase, in a " + what + " (" + juce::String(deckPhase, 4) + " against " + juce::String(masterPhase, 4) + ")");
    }
}

// A tempo a deck holds since it took over as master is a rate of the
// material it was held on: a bounce made again by beat keeps it, and a new
// track plays at its own fader's tempo, as the session means it to.
void checkHeldTempoAcrossSwaps()
{
    DjEngine engine;
    engine.prepare(deviceRate, block);
    activate(engine, 2);
    engine.quantise.store(static_cast<int>(DjEngine::Quantise::off));
    engine.setDeckSource(0, rampTrack(48000.0, 40.0, 128.0), false);
    engine.setDeckSource(1, rampTrack(48000.0, 40.0, 120.0), false);
    run(engine, 1);
    engine.push(Command::play(0));
    engine.push(Command::play(1));
    run(engine, 2);
    auto& d = engine.deck(1);
    d.controls.synced.store(true);
    engine.push(Command::syncAlign(1));
    run(engine, 60);
    engine.push(Command::pause(0));
    run(engine, 40);
    require(engine.masterDeck.load() == 1 && std::abs(d.heldTempoRate.load() - 128.0 / 120.0) < 1.0e-9,
            "a synced deck that takes over holds the tempo it followed");
    // How far the deck moves in ten blocks, once its rate has settled.
    const auto travel = [&engine, &d]
    {
        run(engine, 40);
        const auto from = d.position.load();
        run(engine, 10);
        return d.position.load() - from;
    };
    engine.setDeckSource(1, rampTrack(48000.0, 40.0, 120.0), true);
    const auto kept = travel();
    require(std::abs(kept - 10.0 * block * 128.0 / 120.0) < 1.0,
            "a bounce made again by beat keeps the held tempo (" + juce::String(kept, 1) + " frames in ten blocks)");
    engine.setDeckSource(1, rampTrack(48000.0, 40.0, 100.0), false);
    const auto own = travel();
    require(std::abs(own - 10.0 * block) < 1.0 && d.heldTempoRate.load() == 0.0,
            "a new track plays at its own fader's tempo (" + juce::String(own, 1) + " frames in ten blocks)");
}

// Each deck plays in a slot of its own: taking one off empties that slot and
// leaves the others playing on without a gap, where they stood.
void checkStableSlots()
{
    DjEngine engine;
    engine.prepare(deviceRate, block);
    activate(engine, 4);
    engine.quantise.store(static_cast<int>(DjEngine::Quantise::off));
    for (int slot = 0; slot < 4; ++slot)
        engine.setDeckSource(slot, toneTrack(220.0 * (slot + 1), 48000.0, 10.0), false);
    run(engine, 1);
    for (int slot = 0; slot < 4; ++slot)
        engine.push(Command::play(slot));
    run(engine, 10);
    const auto* third = engine.trackOf(2);
    const auto thirdAt = engine.deck(2).position.load(), fourthAt = engine.deck(3).position.load();
    engine.setSlotActive(1, false);
    require(engine.trackOf(1) == nullptr && engine.trackOf(2) == third, "a slot let go of hands its material back, and only its own");
    float quietest = 1.0f;
    for (int b = 0; b < 20; ++b)
        quietest = std::min(quietest, rms(run(engine, 1).left));
    require(quietest > 0.1f, "the decks left play on with no gap (" + juce::String(quietest, 3) + " the quietest block)");
    require(engine.deck(2).position.load() == thirdAt + 20.0 * block && engine.deck(3).position.load() == fourthAt + 20.0 * block,
            "each from where it stood, a block a block");
    require(engine.deck(1).currentState() == DjDeck::State::empty && engine.collect() == 0,
            "and the slot let go of is empty, its material back with the message thread");
}

// A deck's controls reset and copy as one: whatever a deck was set to, a
// reset puts back what a new deck has.
void checkControls()
{
    DjDeckControls set, copied;
    set.tempoPercent.store(3.0f);
    set.tempoRange.store(16);
    set.nudgePercent.store(1.5f);
    set.synced.store(true);
    set.reversed.store(true);
    set.scratching.store(true);
    set.scratchRate.store(-0.5);
    set.brakeSeconds.store(1.5f);
    set.quantiseSnap.store(false);
    copied.copyFrom(set);
    require(copied.tempoPercent.load() == 3.0f && copied.tempoRange.load() == 16 && copied.nudgePercent.load() == 1.5f
                && copied.synced.load() && copied.reversed.load() && copied.scratching.load() && copied.scratchRate.load() == -0.5
                && copied.brakeSeconds.load() == 1.5f && !copied.quantiseSnap.load(),
            "a deck's controls copy whole");
    const DjDeckControls fresh;
    copied.reset();
    require(copied.tempoPercent.load() == fresh.tempoPercent.load() && copied.tempoRange.load() == fresh.tempoRange.load()
                && copied.nudgePercent.load() == fresh.nudgePercent.load() && copied.synced.load() == fresh.synced.load()
                && copied.reversed.load() == fresh.reversed.load() && copied.scratching.load() == fresh.scratching.load()
                && copied.scratchRate.load() == fresh.scratchRate.load() && copied.brakeSeconds.load() == fresh.brakeSeconds.load()
                && copied.quantiseSnap.load() == fresh.quantiseSnap.load(),
            "and reset to a new deck's");
}

// Nothing in the booth that heads for silence parks on a denormal: a gain
// fading out lands on exactly nothing, and a compressor released into
// silence floors its envelope. Read from the processor's own flag for a
// denormal operand, with flushing off as it is outside the booth's callback.
void checkDenormals()
{
    if (!DenormalWatch::available) return;
    DjSmoothedGain gain;
    gain.prepare(deviceRate, 0.005);
    gain.current = 1.0f;
    gain.target = 0.0f;
    {
        DenormalWatch watch;
        for (int i = 0; i < 48000; ++i)
            gain.next();
        require(gain.current == 0.0f && !watch.touched(), "a gain fading out lands on nothing, and never on a denormal");
    }
    gain.target = 1.0f;
    for (int i = 0; i < 48000; ++i)
        gain.next();
    require(gain.current == 1.0f, "and one fading in lands on one");

    DjCompressor compressor;
    compressor.prepare(deviceRate);
    compressor.set(0.7f);
    std::vector<float> l(block), r(block);
    for (int b = 0; b < 20; ++b)
    {
        std::fill(l.begin(), l.end(), 0.5f);
        std::fill(r.begin(), r.end(), 0.5f);
        compressor.process(l.data(), r.data(), block);
    }
    DenormalWatch watch;
    for (int b = 0; b < static_cast<int>(30.0 * deviceRate / block); ++b)
    {
        std::fill(l.begin(), l.end(), 0.0f);
        std::fill(r.begin(), r.end(), 0.0f);
        compressor.process(l.data(), r.data(), block);
    }
    require(!watch.touched(), "a compressor released into thirty seconds of silence never takes a denormal");
}
void checkMixer()
{
    DjEngine engine;
    engine.prepare(deviceRate, block);
    activate(engine, 2);
    engine.quantise.store(static_cast<int>(DjEngine::Quantise::off));
    const auto level = [&engine](double frequency, float amplitude = 0.5f)
    {
        engine.setDeckSource(0, toneTrack(frequency, deviceRate, 4.0, amplitude), false);
        run(engine, 1);
        engine.push(Command::seek(0, 0.0));
        engine.push(Command::play(0));
        run(engine, 20);   // settle the filters and the fade
        const auto out = run(engine, 40);
        return rms(out.left);
    };
    auto& strip = engine.channel(0);
    // The EQ's crossovers sit at 150 Hz, 600 Hz and 3 kHz and are fourth
    // order, 24 dB an octave. The outer bands are killed where a tone is
    // two octaves clear of the nearest crossover and asked for thirty dB;
    // the inner bands are only two octaves wide, so a tone in the middle of
    // one keeps what leaks from the bands either side.
    const auto flat40 = level(40.0), flat300 = level(300.0), flat1k4 = level(1400.0), flat12k = level(12000.0);
    const auto flat100 = level(100.0), flat1k = level(1000.0);
    require(flat40 > 0.3f && flat300 > 0.3f && flat1k4 > 0.3f && flat12k > 0.3f && flat1k > 0.3f, "a flat strip passes every band");
    strip.eqDb[0].store(DjIsolator::killDb);
    require(decibels(level(40.0) / flat40) < -30.0f, "a low kill removes 40 Hz");
    require(std::abs(decibels(level(1000.0) / flat1k)) < 1.0f, "and leaves 1 kHz");
    strip.eqDb[0].store(0.0f);
    strip.eqDb[1].store(DjIsolator::killDb);
    require(decibels(level(300.0) / flat300) < -15.0f, "a low-mid kill takes 300 Hz down");
    require(std::abs(decibels(level(40.0) / flat40)) < 1.0f && std::abs(decibels(level(12000.0) / flat12k)) < 1.0f,
            "and leaves the lows and the highs");
    strip.eqDb[1].store(0.0f);
    strip.eqDb[2].store(DjIsolator::killDb);
    require(decibels(level(1400.0) / flat1k4) < -15.0f, "a high-mid kill takes 1.4 kHz down");
    require(std::abs(decibels(level(40.0) / flat40)) < 1.0f, "and leaves 40 Hz");
    strip.eqDb[2].store(0.0f);
    strip.eqDb[3].store(DjIsolator::killDb);
    require(decibels(level(12000.0) / flat12k) < -30.0f, "a high kill removes 12 kHz");
    strip.eqDb[3].store(6.0f);
    require(std::abs(decibels(level(12000.0) / flat12k) - 6.0f) < 0.7f, "high at +6 dB lifts 12 kHz by 6 dB");
    strip.eqDb[3].store(0.0f);
    strip.trimDb.store(-6.0f);
    require(std::abs(decibels(level(1000.0) / flat1k) + 6.0f) < 0.3f, "trim at -6 dB is -6 dB");
    strip.trimDb.store(0.0f);
    strip.filter.store(-1.0f);
    require(decibels(level(12000.0) / flat12k) < -30.0f, "the filter turned left low-passes 12 kHz away");
    strip.filter.store(1.0f);
    require(decibels(level(100.0) / flat100) < -30.0f, "the filter turned right high-passes 100 Hz away");
    strip.filter.store(0.0f);
    strip.fader.store(0.0f);
    require(level(1000.0) < 1.0e-4f, "the fader down is silence");
    strip.fader.store(0.5f);
    require(std::abs(decibels(level(1000.0) / flat1k) + 12.0f) < 0.5f, "the fader half way is a quarter of the gain");
    strip.faderCurve.store(2);
    require(decibels(level(1000.0) / flat1k) < -20.0f, "on the steep curve the fader half way is further down");
    strip.faderCurve.store(0);
    require(decibels(level(1000.0) / flat1k) > -4.0f, "and on the gentle one it is nearly up");
    strip.faderCurve.store(1);
    strip.fader.store(1.0f);

    // The compressor: one knob. Full up, a loud tone is held down and a
    // quiet one lifted, the way a thin track is filled out.
    strip.comp.store(1.0f);
    const auto loudCompressed = decibels(level(1000.0) / flat1k);
    require(loudCompressed < -2.0f && loudCompressed > -8.0f,
            "the compressor full up holds a loud tone down (" + juce::String(loudCompressed, 1) + " dB)");
    const auto quietCompressed = level(1000.0, 0.01f);
    strip.comp.store(0.0f);
    const auto quietFlat = level(1000.0, 0.01f);
    require(decibels(quietCompressed / quietFlat) > 8.0f,
            "and lifts a quiet one (" + juce::String(decibels(quietCompressed / quietFlat), 1) + " dB)");

    // The crossfader: a channel on A is gone with the fader on B, and half
    // way down an equal-power curve it is -3 dB.
    strip.crossfaderSide.store(static_cast<int>(DjChannelStrip::CrossfaderSide::a));
    engine.crossfader().position.store(1.0f);
    require(level(1000.0) < 1.0e-4f, "a channel on A is silent with the crossfader on B");
    engine.crossfader().position.store(0.0f);
    require(std::abs(decibels(level(1000.0) / flat1k) + 3.0f) < 0.3f, "and -3 dB with it in the middle");
    engine.crossfader().curve.store(1.0f);
    require(std::abs(decibels(level(1000.0) / flat1k)) < 0.2f, "a sharp curve keeps it at full in the middle");
    engine.crossfader().curve.store(0.0f);
    strip.crossfaderSide.store(static_cast<int>(DjChannelStrip::CrossfaderSide::through));

    // The master: its level, its isolator, and its meter.
    engine.master().levelDb.store(-6.0f);
    require(std::abs(decibels(level(1000.0) / flat1k) + 6.0f) < 0.3f, "the master level at -6 dB is -6 dB");
    engine.master().levelDb.store(0.0f);
    engine.master().lowDb.store(DjIsolator::killDb);
    require(decibels(level(40.0) / flat40) < -30.0f, "the master isolator kills 40 Hz");
    engine.master().lowDb.store(0.0f);
    level(1000.0);
    require(engine.master().meterLeft.load() > 0.3f && strip.meter.load() > 0.3f, "the meters read the tone");

    // The send: a click sent to the short delay comes back once the delay
    // the time knob sets has passed.
    {
        engine.setDeckSource(0, clickTrack(2.0), false);
        auto& send = engine.sendFx();
        send.type.store(static_cast<int>(DjSendFx::Type::shortDelay));
        send.time.store(0.5f);
        send.size.store(0.0f);
        send.tone.store(1.0f);
        send.mix.store(1.0f);
        strip.send.store(1.0f);
        run(engine, 100);   // the delay time glides into place
        const auto seek = Command::seek(0, 0.0);
        engine.push(seek);
        engine.push(Command::play(0));
        const auto out = run(engine, 20);
        size_t first = 0, repeat = 0;
        for (size_t i = 0; i < out.left.size(); ++i)
        {
            if (first == 0 && std::abs(out.left[i]) > 0.2f) first = i;
            else if (first != 0 && i > first + 1500 && std::abs(out.left[i]) > 0.1f)
            {
                repeat = i;
                break;
            }
        }
        const auto expected = DjSendFx::secondsFor(DjSendFx::Type::shortDelay, 0.5f) * deviceRate;
        require(first > 0 && repeat > 0, "the send effect returns the click");
        require(std::abs(static_cast<double>(repeat - first) - expected) < 60.0,
                "after the delay the time knob sets (" + juce::String(static_cast<int>(repeat - first)) + " frames for "
                + juce::String(static_cast<int>(expected)) + ")");
        strip.send.store(0.0f);
    }

    // The cue bus: a channel cued is on outputs 3 and 4 with the fader down.
    strip.fader.store(0.0f);
    strip.cue.store(true);
    engine.setDeckSource(0, toneTrack(1000.0, deviceRate, 4.0), false);
    run(engine, 1);
    engine.push(Command::play(0));
    run(engine, 10);
    std::vector<float> a(block), b(block), c(block), d(block);
    float* four[4] {a.data(), b.data(), c.data(), d.data()};
    for (int i = 0; i < 20; ++i)
        engine.process(four, 4, block);
    require(rms(a) < 1.0e-4f && rms(c) > 0.3f, "a cued channel with its fader down is heard on the cue outputs only");
    strip.cue.store(false);
    strip.fader.store(1.0f);
}

void checkMic()
{
    // The section on its own: talkover ducks the master by 18 dB while the
    // mic is spoken into and lets it back up afterwards; on, it does not.
    DjMicSection mic;
    mic.prepare(deviceRate);
    std::vector<float> input(block), out(block);
    int sample = 0;
    float duck = 1.0f;
    const auto feed = [&](bool loud, int blocks)
    {
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < block; ++i, ++sample)
                input[static_cast<size_t>(i)] = loud ? 0.5f * static_cast<float>(std::sin(2.0 * pi * 300.0 * sample / deviceRate)) : 0.0f;
            mic.process(input.data(), out.data(), block, duck);
        }
    };
    mic.mode.store(static_cast<int>(DjMicSection::Mode::talkover));
    feed(true, 100);
    require(std::abs(decibels(duck) - DjMicSection::talkoverDuckDb) < 2.0f,
            "talkover ducks the master while the mic is spoken into (" + juce::String(decibels(duck), 1) + " dB)");
    require(rms(out) > 0.2f && mic.meter.load() > 0.3f, "and the mic is heard and metered");
    feed(false, 300);
    require(duck > 0.9f, "and lets the master back up once the mic falls quiet (" + juce::String(duck, 2) + ")");
    mic.mode.store(static_cast<int>(DjMicSection::Mode::on));
    feed(true, 100);
    require(duck > 0.99f, "the mic simply on ducks nothing");
    mic.mode.store(static_cast<int>(DjMicSection::Mode::off));
    feed(true, 20);
    require(rms(out) < 1.0e-4f, "and off, it is silent");

    // Through the engine: the first input, on, reaches the master.
    DjEngine engine;
    engine.prepare(deviceRate, block);
    activate(engine, 1);
    std::vector<float> micIn(block), l(block), r(block);
    const float* inputs[1] {micIn.data()};
    float* outputs[2] {l.data(), r.data()};
    const auto speak = [&](int blocks)
    {
        float heard = 0.0f;
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < block; ++i, ++sample)
                micIn[static_cast<size_t>(i)] = 0.5f * static_cast<float>(std::sin(2.0 * pi * 300.0 * sample / deviceRate));
            engine.process(inputs, 1, outputs, 2, block);
            heard = rms(l);
        }
        return heard;
    };
    require(speak(10) < 1.0e-4f, "the mic off is not heard");
    engine.mic().mode.store(static_cast<int>(DjMicSection::Mode::on));
    require(speak(20) > 0.1f, "the mic on is heard on the master");
}

void checkBeatFx()
{
    DjEngine engine;
    engine.prepare(deviceRate, block);
    activate(engine, 1);
    engine.quantise.store(static_cast<int>(DjEngine::Quantise::off));
    // A click, then silence: the echo's repeat is a second click one beat
    // later at the master tempo. With no master playing the effect runs at
    // the tapped tempo, 128 BPM, so a beat is 22500 frames.
    engine.setDeckSource(0, clickTrack(4.0), false);
    run(engine, 1);
    auto& fx = engine.fx();
    fx.type.store(static_cast<int>(DjBeatFx::Type::echo));
    fx.beats.store(1.0f);
    fx.depth.store(0.5f);
    fx.target.store(-1);
    fx.on.store(true);
    engine.push(Command::play(0));
    const auto out = run(engine, 120);
    // The first click lands where the deck reads it, after the start fade;
    // the repeat is a beat later.
    size_t first = 0, repeat = 0;
    for (size_t i = 0; i < out.left.size(); ++i)
    {
        if (first == 0 && out.left[i] > 0.2f) first = i;
        else if (first != 0 && i > first + 1000 && out.left[i] > 0.05f)
        {
            repeat = i;
            break;
        }
    }
    require(first > 0 && repeat > 0, "the echo repeats the click");
    require(std::abs(static_cast<double>(repeat - first) - 22500.0) < 3.0,
            "the repeat is one beat later at the booth's tempo (" + juce::String(static_cast<int>(repeat - first)) + " frames)");
    fx.on.store(false);

    // Trans at full depth: the second half of each beat is silent.
    engine.setDeckSource(0, toneTrack(1000.0, deviceRate, 8.0), false);
    run(engine, 1);
    fx.type.store(static_cast<int>(DjBeatFx::Type::trans));
    fx.beats.store(1.0f);
    fx.depth.store(1.0f);
    fx.on.store(true);
    const auto seek = Command::seek(0, 0.0);
    engine.push(seek);
    engine.push(Command::play(0));
    run(engine, 10);
    const auto gated = run(engine, 100);
    int silentRuns = 0, loudRuns = 0;
    countRuns(gated.left, silentRuns, loudRuns);
    require(silentRuns >= 2 && loudRuns >= 2, "trans chops the tone on and off at the beat");
    // The band keys: with the mid band taken off the effect, the same tone
    // passes dry.
    fx.bandMid.store(false);
    run(engine, 10);
    const auto passed = run(engine, 100);
    countRuns(passed.left, silentRuns, loudRuns);
    require(silentRuns == 0 && rms(passed.left) > 0.2f, "with its band taken off the effect, the tone passes dry");
    fx.bandMid.store(true);
    fx.on.store(false);

    // Pitch at full depth is an octave up: a 440 Hz tone comes out crossing
    // zero as often as an 880 Hz one.
    engine.setDeckSource(0, toneTrack(440.0, deviceRate, 8.0), false);
    run(engine, 1);
    fx.type.store(static_cast<int>(DjBeatFx::Type::pitch));
    fx.depth.store(1.0f);
    fx.on.store(true);
    engine.push(seek);
    engine.push(Command::play(0));
    run(engine, 30);
    const auto shifted = run(engine, 50);
    const auto measured = zeroCrossingFrequency(shifted.left);
    require(std::abs(measured - 880.0) < 60.0, "pitch at full depth is an octave up (" + juce::String(measured, 1) + " Hz)");
    fx.on.store(false);

    // The vinyl brake winds the sound down to nothing within its beat.
    engine.setDeckSource(0, toneTrack(1000.0, deviceRate, 8.0), false);
    run(engine, 1);
    fx.type.store(static_cast<int>(DjBeatFx::Type::vinylBrake));
    fx.beats.store(1.0f);
    engine.push(seek);
    engine.push(Command::play(0));
    run(engine, 10);
    fx.on.store(true);
    const auto braked = run(engine, 100);   // a second; the beat is 0.47 s
    require(rms(braked.left, 0, 2048) > 0.2f, "the brake starts from the tone");
    require(rms(braked.left, braked.left.size() - 10240, braked.left.size()) < 0.02f, "and has stopped it a beat later");
    fx.on.store(false);
}


void checkAnalysis()
{
    // Sixty seconds of a 126.5 BPM house beat: a kick on every beat, louder
    // on the one, a hat off the beat, and a chord pad in A minor, quiet for
    // the first eight bars so a drop falls on bar 8.
    const double rate = 44100.0, bpm = 126.5, firstBeat = 0.137;
    const auto frames = static_cast<int>(rate * 60.0);
    std::vector<float> audio(static_cast<size_t>(frames), 0.0f);
    const auto beatSeconds = 60.0 / bpm;
    const double chord[3] {220.0, 261.63, 329.63};   // A minor
    for (int i = 0; i < frames; ++i)
    {
        const auto t = i / rate;
        const auto beat = (t - firstBeat) / beatSeconds;
        float sample = 0.0f;
        if (beat >= 0.0)
        {
            const auto inBeat = beat - std::floor(beat);
            const auto beatIndex = static_cast<int>(std::floor(beat));
            const auto bar = beatIndex / 4;
            const auto accent = beatIndex % 4 == 0 ? 1.0f : 0.6f;
            // Kick: a 55 Hz sine decaying over 50 ms.
            const auto sinceKick = inBeat * beatSeconds;
            sample += accent * static_cast<float>(std::sin(2.0 * pi * 55.0 * sinceKick) * std::exp(-sinceKick / 0.05));
            // Hat: a noise burst off the beat.
            const auto sinceHat = (inBeat >= 0.5 ? inBeat - 0.5 : inBeat + 0.5) * beatSeconds;
            if (sinceHat < 0.03)
            {
                const auto noise = std::sin(i * 12.9898) * 43758.5453;
                sample += 0.25f * static_cast<float>((noise - std::floor(noise)) - 0.5) * static_cast<float>(std::exp(-sinceHat / 0.01));
            }
            // The pad, in from bar 8, with all three notes and a harmonic each.
            const auto padLevel = bar >= 8 ? 0.25f : 0.03f;
            for (const auto f : chord)
                sample += padLevel * static_cast<float>(std::sin(2.0 * pi * f * t) + 0.3 * std::sin(2.0 * pi * f * 2.0 * t));
        }
        audio[static_cast<size_t>(i)] = 0.5f * sample;
    }
    const auto analysis = analyseDjTrack(audio.data(), nullptr, frames, rate);
    require(analysis.columns.size() == static_cast<size_t>((frames + DjAnalysis::columnFrames - 1) / DjAnalysis::columnFrames),
            "one waveform column per 256 frames");
    require(analysis.hasGrid(), "a beat is found");
    require(std::abs(analysis.bpm - bpm) < 0.05,
            "the tempo is read to a twentieth of a beat per minute (" + juce::String(analysis.bpm, 3) + " for " + juce::String(bpm, 1) + ")");
    // The first beat lands on a beat of the grid - any beat, since the grid
    // extends both ways - and the downbeat on the accented one.
    const auto beatsOff = (analysis.firstBeatSeconds - firstBeat) / beatSeconds;
    const auto nearest = std::round(beatsOff);
    require(std::abs(beatsOff - nearest) * beatSeconds < 0.012,
            "the grid sits on the beats (" + juce::String((beatsOff - nearest) * beatSeconds * 1000.0, 1) + " ms off)");
    require(std::fmod(std::abs(nearest), 4.0) == 0.0,
            "the downbeat is the accented beat (" + juce::String(nearest) + " beats from the first)");
    require(!analysis.drops.empty() && analysis.drops.front() == 8,
            "the drop is heard at bar 8 (" + (analysis.drops.empty() ? juce::String("none") : juce::String(analysis.drops.front())) + ")");
    require(analysis.keyIndex == 12 + 9, "the key is A minor (" + DjAnalysis::keyName(analysis.keyIndex) + ")");
    require(DjAnalysis::keyName(21) == "Am" && DjAnalysis::camelotName(21) == "8A" && DjAnalysis::camelotName(0) == "8B"
            && DjAnalysis::camelotName(7) == "9B" && DjAnalysis::keyName(-1) == "-", "keys are named as a DJ reads them");

    // Told the tempo, the analyser lays the grid from it and finds the drop.
    DjAnalysisOptions known;
    known.knownBpm = bpm;
    known.knownFirstBeatSeconds = firstBeat;
    known.detectKey = false;
    const auto laid = analyseDjTrack(audio.data(), nullptr, frames, rate, known);
    require(laid.bpm == bpm && laid.firstBeatSeconds == firstBeat && laid.keyIndex == -1, "a known tempo is taken as given");
    require(!laid.drops.empty() && laid.drops.front() == 8, "and the drop is found on that grid");
    require(laid.barEnergy.size() >= 30 && laid.barEnergy[9] > laid.barEnergy[3] * 1.5f, "bars after the drop are louder than before it");

    // Nothing at all.
    std::vector<float> silence(static_cast<size_t>(rate * 10.0), 0.0f);
    const auto quiet = analyseDjTrack(silence.data(), nullptr, static_cast<int>(silence.size()), rate);
    require(!quiet.hasGrid() && quiet.keyIndex == -1 && quiet.drops.empty(), "silence has no tempo, key or drops");
}

// What the booth costs a block, measured rather than reasoned from its loops:
// six decks on ramps, two of them playing synced inside a loop, every strip's
// compressor and filter working, the send unit and the beat effect on. The
// blocks are timed one by one and their allocations counted, which must be
// none - in the paths that hand material over and back as well as in steady
// playback, so every hundred blocks a stopped deck takes new material, the
// master takes its bounce again by beat, the synced deck lands a quantised
// hot cue, the master beat-jumps with its loop, and a slot is let go of and
// taken up again with new material. The materials are made, handed over and
// collected outside the counter, as the message thread does all three. Then
// everything stops and ten seconds of silence are timed, which is where a
// gain, an envelope or a filter's state parked at a denormal shows: nothing
// here sets the processor to flush them, as the booth's own callback does,
// and where the processor keeps a flag of it, whether the silence touched a
// denormal at all is logged too. Ten seconds reach the isolators, which take
// denormals within a few blocks, and the send's reverb, within eight; thirty,
// which also reach the beat effect's echo, cost --self-test eight seconds of
// its thirty while the mixer still parks there (2026-10-11).
void checkBoothCost()
{
    DjEngine engine;
    engine.prepare(deviceRate, block);
    activate(engine, DjEngine::maximumDecks);
    engine.quantise.store(static_cast<int>(DjEngine::Quantise::off));
    for (int i = 0; i < DjEngine::maximumDecks; ++i)
    {
        engine.setDeckSource(i, rampTrack(48000.0, 20.0, i == 1 ? 125.0 : 120.0), false);
        auto& strip = engine.channel(i);
        strip.comp.store(0.7f);
        strip.filter.store(i % 2 == 0 ? -0.5f : 0.5f);
        strip.send.store(0.4f);
    }
    engine.sendFx().type.store(static_cast<int>(DjSendFx::Type::reverb));
    engine.sendFx().mix.store(0.6f);
    engine.fx().type.store(static_cast<int>(DjBeatFx::Type::echo));
    engine.fx().on.store(true);
    run(engine, 1);
    engine.deck(1).controls.synced.store(true);
    for (int i = 0; i < 2; ++i)
    {
        engine.push(Command::play(i));
        engine.push(Command::beatLoop(i, 4.0));
    }
    run(engine, 50);
    require(engine.deck(0).isPlaying() && engine.deck(1).isPlaying() && engine.deck(0).loopActive.load()
                && engine.deck(1).loopActive.load() && engine.masterDeck.load() == 0,
            "the cost check plays two decks looped, the second synced to the first");
    // The swaps copy these: the master's bounce made again, and short
    // material for the stopped decks.
    const auto bounce = rampTrack(48000.0, 20.0, 120.0);
    const auto shortMaterial = rampTrack(48000.0, 2.0, 120.0);
    const auto copyOf = [](const DjTrack& material) { return std::make_unique<DjTrack>(material); };
    // The hot cue lands quantised, on the master's next beat.
    engine.quantise.store(static_cast<int>(DjEngine::Quantise::beat));

    std::vector<float> l(block), r(block);
    float* outputs[2] {l.data(), r.data()};
    constexpr int timedBlocks = 2000;
    std::vector<double> playing;
    playing.reserve(timedBlocks);
    long long allocations = 0;
    int handedOver = 0, landed = 0;
    for (int b = 0; b < timedBlocks; ++b)
    {
        switch (b % 100)
        {
            case 10: handedOver += engine.setDeckSource(2, copyOf(*shortMaterial), false) ? 1 : 0; break;
            case 30: handedOver += engine.setDeckSource(0, copyOf(*bounce), true) ? 1 : 0; break;
            case 50:
                engine.push(Command::setHotCue(1, 0, engine.deck(1).loopStart.load()));
                engine.push(Command::hotCue(1, 0));
                break;
            case 70: engine.push(Command::beatJump(0, (b / 100) % 2 == 0 ? 4 : -4)); break;
            case 90:
                engine.setSlotActive(5, false);
                engine.setSlotActive(5, true);
                handedOver += engine.setDeckSource(5, copyOf(*shortMaterial), false) ? 1 : 0;
                break;
            case 99: landed += engine.deck(1).currentPending() == DjDeck::Pending::none ? 1 : 0; break;
            default: break;
        }
        {
            AllocationCounter counter;
            const auto started = juce::Time::getHighResolutionTicks();
            engine.process(outputs, 2, block);
            playing.push_back(juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - started) * 1.0e6);
            allocations += counter.count();
        }
        engine.collect();
    }
    require(engine.deck(0).isPlaying() && engine.deck(1).isPlaying() && engine.deck(0).loopActive.load()
                && engine.deck(1).loopActive.load() && rms(r) > 0.01f,
            "the timed blocks played, both decks still looped");
    require(handedOver == 3 * timedBlocks / 100 && engine.collect() == 0 && landed == timedBlocks / 100,
            "handing material over " + juce::String(handedOver) + " times, every material it replaced back, and landing "
            + juce::String(landed) + " hot cues");
    require(allocations == 0, "the booth allocates nothing in a block (" + juce::String(allocations) + " allocations in "
                                  + juce::String(timedBlocks) + " blocks)");

    engine.push(Command::stopAll());
    run(engine, 20);
    require(!engine.deck(0).isPlaying() && !engine.deck(1).isPlaying(), "and everything stops");
    constexpr double silentSeconds = 10.0;
    const auto silentBlocks = static_cast<int>(silentSeconds * deviceRate / block);
    std::vector<double> silent;
    silent.reserve(static_cast<size_t>(silentBlocks));
    auto denormals = juce::String("not watched on this processor");
    {
        DenormalWatch watch;
        for (int b = 0; b < silentBlocks; ++b)
        {
            const auto started = juce::Time::getHighResolutionTicks();
            engine.process(outputs, 2, block);
            silent.push_back(juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - started) * 1.0e6);
        }
        if (DenormalWatch::available)
            denormals = watch.touched() ? "touched" : "never touched";
    }
    const std::vector<double> lastSeconds(silent.end() - static_cast<std::ptrdiff_t>(2.0 * deviceRate / block), silent.end());
    juce::Logger::writeToLog("Rhino: the DJ booth costs " + juce::String(percentile(playing, 0.5), 1) + " us a block of "
                             + juce::String(block) + " (median), " + juce::String(percentile(playing, 0.99), 1)
                             + " us at the 99th percentile, playing two synced, looped decks of six with every strip's "
                               "compressor and filter, the send unit and the beat effect on, and material handed over, "
                               "a hot cue, a beat jump and a slot taken up again every hundred blocks; "
                             + juce::String(allocations) + " allocations in " + juce::String(timedBlocks) + " blocks; "
                             + juce::String(percentile(silent, 0.5), 1) + " us a block (median) over "
                             + juce::String(silentSeconds, 0) + " s of silence after a stop, "
                             + juce::String(percentile(lastSeconds, 0.5), 1) + " us over its last 2 s, denormals "
                             + denormals);
}
}

void checkDjCore(Session&)
{
    const auto started = juce::Time::getMillisecondCounter();
    DjDeckTest::transport();
    DjDeckTest::swapCrossfade();
    DjDeckTest::brake();
    DjDeckTest::plainPlaybackUnchanged();
    DjDeckTest::wholeLoopsAndWraps();
    DjDeckTest::leavingLoops();
    DjDeckTest::keepBeatLoopingWhole();
    DjDeckTest::declick();
    DjDeckTest::fadesUnderFades();
    DjDeckTest::crawlingWrap();
    checkScratch();
    checkTrackPort();
    checkEngineHousekeeping();
    checkCommandOrder();
    checkQuantisedStartAndSync();
    checkQuantisedJumps();
    checkLandingsInLoops();
    checkSyncFollowsTempo();
    checkSyncInLoops();
    checkHeldTempoAcrossSwaps();
    checkStableSlots();
    checkControls();
    checkDenormals();
    checkMixer();
    checkMic();
    checkBeatFx();
    checkBoothCost();
    const auto analysisStarted = juce::Time::getMillisecondCounter();
    checkAnalysis();
    juce::Logger::writeToLog("Rhino: DJ booth checks passed in " + juce::String(juce::Time::getMillisecondCounter() - started)
                             + " ms, a minute of beat analysed in "
                             + juce::String(juce::Time::getMillisecondCounter() - analysisStarted) + " ms");
}
}
