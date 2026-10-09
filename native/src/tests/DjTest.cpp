#include "../Session.h"
#include "DjEngine.h"
#include "DjTrack.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>

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

std::unique_ptr<DjTrack> toneTrack(double frequency, double sampleRate, double seconds)
{
    auto track = std::make_unique<DjTrack>();
    track->sampleRate = sampleRate;
    track->name = "tone";
    const auto frames = static_cast<int>(seconds * sampleRate);
    track->left.resize(static_cast<size_t>(frames));
    for (int i = 0; i < frames; ++i)
        track->left[static_cast<size_t>(i)] = 0.5f * static_cast<float>(std::sin(2.0 * pi * frequency * i / sampleRate));
    return track;
}

struct Output
{
    std::vector<float> left, right;
};

// Runs the engine for a number of blocks, keeping the output.
Output run(DjEngine& engine, int blocks)
{
    Output out;
    std::vector<float> l(block), r(block);
    float* outputs[2] {l.data(), r.data()};
    for (int b = 0; b < blocks; ++b)
    {
        engine.process(outputs, 2, block);
        out.left.insert(out.left.end(), l.begin(), l.end());
        out.right.insert(out.right.end(), r.begin(), r.end());
    }
    engine.collect();
    return out;
}

// Renders one deck on its own, as the engine would with nothing in the way.
Output renderDeck(DjDeck& deck, int blocks)
{
    Output out;
    std::vector<float> l(block), r(block);
    for (int b = 0; b < blocks; ++b)
    {
        deck.render(l.data(), r.data(), block, deviceRate);
        out.left.insert(out.left.end(), l.begin(), l.end());
        out.right.insert(out.right.end(), r.begin(), r.end());
    }
    return out;
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

DjEngine::Command command(DjEngine::Command::Type type, int deck)
{
    DjEngine::Command c;
    c.type = type;
    c.deck = deck;
    return c;
}

void checkDeckTransport()
{
    DjDeck deck;
    auto ramp = rampTrack(44100.0, 10.0);
    deck.track.store(ramp.get());
    deck.adoptPending();
    // What the engine does to a start with quantise off: lands it at once.
    const auto start = [&deck]
    {
        deck.play();
        deck.landPending(0, 0);
    };
    require(deck.currentState() == DjDeck::State::stopped, "a deck given material is stopped, not empty");
    const auto silent = renderDeck(deck, 2);
    require(rms(silent.left) == 0.0f, "a stopped deck is silent");

    start();
    const auto played = renderDeck(deck, 20);
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
    renderDeck(deck, 2);
    require(deck.currentState() == DjDeck::State::stopped, "pause stops");
    const auto paused = deck.position.load();
    renderDeck(deck, 5);
    require(deck.position.load() == paused, "a stopped deck stays put");

    // Tempo: +6% plays 6% more frames once the rate has settled. The engine
    // sets a deck's target rate every block; here it is set by hand.
    deck.tempoPercent.store(6.0f);
    deck.targetRate = deck.baseRate();
    start();
    renderDeck(deck, 10);   // settle
    const auto before = deck.position.load();
    renderDeck(deck, 40);
    const auto moved = deck.position.load() - before;
    const auto wanted = 40.0 * block * step * 1.06;
    require(std::abs(moved / wanted - 1.0) < 0.002,
            "the tempo fader at +6% plays 6% faster (" + juce::String(moved) + " vs " + juce::String(wanted) + ")");
    deck.tempoPercent.store(0.0f);
    deck.targetRate = deck.baseRate();

    // Reverse runs backwards.
    deck.reversed.store(true);
    renderDeck(deck, 10);
    const auto forwardEnd = deck.position.load();
    renderDeck(deck, 10);
    require(deck.position.load() < forwardEnd, "reverse moves the deck backwards");
    deck.reversed.store(false);

    // Seek, then a hot cue set there and recalled from elsewhere.
    deck.seek(100000.0);
    renderDeck(deck, 1);
    require(std::abs(deck.position.load() - 100000.0) < 600.0, "seek lands where it was sent");
    deck.hotCue(2, false);
    require(deck.hotCues[2].load() >= 100000.0 && deck.hotCues[2].load() < 100600.0, "a hot cue pressed empty is set here");
    renderDeck(deck, 20);
    deck.hotCue(2, false);
    require(deck.currentPending() == DjDeck::Pending::jump, "a hot cue pressed set while playing asks to jump");
    deck.landPending(0, 0);
    renderDeck(deck, 1);
    require(std::abs(deck.position.load() - deck.hotCues[2].load()) < 600.0, "and lands there");
    require(deck.isPlaying(), "still playing");

    // Cue: stop and return.
    deck.pause();
    renderDeck(deck, 2);
    deck.seek(50000.0);
    deck.cueDown(false);
    require(std::abs(deck.cuePoint.load() - 50000.0) < 1.0, "cue pressed while stopped sets the cue point");
    start();
    renderDeck(deck, 20);
    deck.cueDown(false);
    renderDeck(deck, 2);
    require(deck.currentState() == DjDeck::State::stopped && std::abs(deck.position.load() - 50000.0) < 1.0,
            "cue pressed while playing stops and goes back to the cue point");
    deck.cueDown(false);
    const auto held = renderDeck(deck, 4);
    require(deck.currentState() == DjDeck::State::cueing && rms(held.right) > 0.1f, "cue held at the cue point plays");
    deck.cueUp();
    renderDeck(deck, 2);
    require(deck.currentState() == DjDeck::State::stopped && std::abs(deck.position.load() - 50000.0) < 1.0,
            "and letting go returns to the cue point");

    // A loop holds the deck inside it and the output stays continuous: the
    // ramp's own slope between wraps, and the wrap itself.
    deck.setLoop(60000.0, 70000.0, true);
    deck.seek(60000.0);
    start();
    renderDeck(deck, 2);
    const auto looped = renderDeck(deck, 70);   // some 32,000 frames: three wraps
    require(deck.position.load() >= 60000.0 && deck.position.load() < 70000.0, "a loop keeps the deck inside it");
    float largestStep = 0.0f;
    int wraps = 0;
    for (size_t i = 1; i < looped.left.size(); ++i)
    {
        const auto drop = looped.left[i - 1] - looped.left[i];
        if (drop > 0.01f) ++wraps;
        else largestStep = std::max(largestStep, std::abs(drop));
    }
    require(wraps >= 2 && largestStep < 1.0e-4f, "between wraps the ramp plays smoothly (" + juce::String(wraps) + " wraps)");
    deck.reloopExit();
    require(!deck.loopActive.load(), "reloop/exit leaves the loop");
    deck.loopHalve();
    require(std::abs(deck.loopEnd.load() - 65000.0) < 1.0, "halve halves the loop");
    deck.loopDouble();
    require(std::abs(deck.loopEnd.load() - 70000.0) < 1.0, "double doubles it back");

    // Beat jump on a grid: a 120 BPM track has 22050 frames to a beat.
    auto gridded = rampTrack(44100.0, 10.0, 120.0);
    deck.track.store(gridded.get());
    deck.pause();
    renderDeck(deck, 2);
    deck.seek(50000.0);   // beat 2.27
    deck.beatJump(4);
    require(std::abs(deck.position.load() - 6.0 * 22050.0) < 1.0, "a beat jump lands on the beat four beats on");
    deck.beatLoop(2.0, true);
    require(std::abs(deck.loopStart.load() - 6.0 * 22050.0) < 1.0 && std::abs(deck.loopEnd.load() - 8.0 * 22050.0) < 1.0
            && deck.loopActive.load(), "a two-beat loop starts on the beat and lasts two beats");

    // The end of the track stops the deck.
    deck.clearLoop();
    deck.seek(441000.0 - 2000.0);
    start();
    renderDeck(deck, 10);
    require(deck.currentState() == DjDeck::State::stopped && deck.position.load() >= 441000.0 - 2.0,
            "a deck reaching the end of its track stops there");

    // Material swapped in by beat keeps the beat; by time otherwise.
    deck.seek(22050.0 * 3.0);
    auto faster = rampTrack(48000.0, 10.0, 120.0);
    deck.keepBeatOnSwap.store(true);
    deck.track.store(faster.get());
    renderDeck(deck, 1);
    require(std::abs(deck.position.load() - 24000.0 * 3.0) < 1.0, "a swap by beat lands on the same beat of the new material");
    auto plain = rampTrack(96000.0, 10.0);
    deck.keepBeatOnSwap.store(false);
    deck.track.store(plain.get());
    renderDeck(deck, 1);
    require(std::abs(deck.position.load() - 48000.0 * 3.0) < 1.0, "a swap by time lands at the same second");
    deck.track.store(nullptr);
    renderDeck(deck, 1);
    require(deck.currentState() == DjDeck::State::empty && deck.hotCues[2].load() < 0.0, "material taken away empties the deck");
}

void checkEngineHousekeeping()
{
    DjEngine engine;
    engine.prepare(deviceRate, block);
    engine.setDeckCount(1);
    engine.setTrack(0, rampTrack(44100.0, 2.0), false);
    run(engine, 1);
    require(engine.deck(0).currentState() == DjDeck::State::stopped, "a track given to the engine reaches its deck");
    engine.setTrack(0, rampTrack(44100.0, 2.0), false);
    require(engine.collect() == 0, "with no block in flight the old material is freed at once");
    run(engine, 1);
    engine.push(command(DjEngine::Command::Type::play, 0));
    const auto out = run(engine, 10);
    require(rms(out.right) > 0.1f, "the engine plays its deck");
    // The queue refuses when full and nothing is lost silently.
    int pushed = 0;
    while (engine.push(command(DjEngine::Command::Type::pause, 0))) ++pushed;
    require(pushed == DjEngine::queueSize - 1, "the command queue holds a block's worth and says when it is full");
    run(engine, 1);
    require(engine.pendingCommands() == 0, "a block drains the queue");
    require(engine.deck(0).currentState() == DjDeck::State::stopped, "and the commands were applied");
    // A block bigger than the engine was prepared for is worked in pieces.
    std::vector<float> l(block * 3), r(block * 3);
    float* outputs[2] {l.data(), r.data()};
    engine.push(command(DjEngine::Command::Type::play, 0));
    engine.process(outputs, 2, block * 3);
    engine.process(outputs, 2, block * 3);
    require(rms(r) > 0.1f && engine.deck(0).isPlaying(), "an oversized block is rendered whole");
}

void checkQuantisedStartAndSync()
{
    DjEngine engine;
    engine.prepare(deviceRate, block);
    engine.setDeckCount(2);
    engine.quantise.store(static_cast<int>(DjEngine::Quantise::bar));
    engine.setTrack(0, rampTrack(48000.0, 20.0, 120.0), false);   // 24000 frames a beat
    engine.setTrack(1, rampTrack(48000.0, 20.0, 125.0), false);   // 23040 frames a beat
    run(engine, 1);
    auto& master = engine.deck(0);
    auto& slave = engine.deck(1);
    engine.push(command(DjEngine::Command::Type::play, 0));
    run(engine, 2);
    require(engine.masterDeck.load() == 0, "the first deck to play is the master");
    // Deck 1 asks to start a few frames into a bar; it must wait for the
    // master's next bar and land on its own nearest bar.
    auto seek = command(DjEngine::Command::Type::seek, 1);
    seek.value = 23040.0 * 4.0 + 5000.0;   // just past bar 1
    engine.push(seek);
    engine.push(command(DjEngine::Command::Type::play, 1));
    run(engine, 1);
    require(slave.currentState() == DjDeck::State::waiting, "a quantised start waits for the master's bar");
    int blocks = 0;
    while (slave.currentState() == DjDeck::State::waiting && blocks < 400)
    {
        run(engine, 1);
        ++blocks;
    }
    require(slave.currentState() == DjDeck::State::playing, "and starts within a bar");
    // When it started, the master was on a bar line: a bar is 96000 frames
    // of the master, and a block is 512, so the master is within a block of
    // a multiple of 96000 and the slave within a block of its bar.
    const auto masterAt = master.position.load();
    const auto masterPhase = std::fmod(masterAt, 96000.0);
    require(masterPhase < block * 2.0 || masterPhase > 96000.0 - block * 2.0,
            "the slave started on the master's bar line (master phase " + juce::String(masterPhase) + ")");
    auto barsApart = std::fmod(slave.beatPosition(), 4.0) - std::fmod(master.beatPosition(), 4.0);
    barsApart -= std::round(barsApart / 4.0) * 4.0;
    require(std::abs(barsApart) < 0.05, "and on its own bar line, so the bars line up (" + juce::String(barsApart, 3) + " beats off)");

    // Sync: the slave follows the master's tempo, then the master's fader.
    slave.synced.store(true);
    engine.push(command(DjEngine::Command::Type::syncAlign, 1));
    run(engine, 60);
    const auto rate = slave.playbackRate.load();
    require(std::abs(rate - 120.0 / 125.0) < 0.003, "a synced deck plays at the master's tempo (" + juce::String(rate, 4) + ")");
    master.tempoPercent.store(5.0f);
    run(engine, 100);
    const auto followed = slave.playbackRate.load();
    require(std::abs(followed - 120.0 * 1.05 / 125.0) < 0.003, "and follows the master's tempo fader (" + juce::String(followed, 4) + ")");
    auto drift = master.beatPosition() - slave.beatPosition();
    drift -= std::round(drift);
    require(std::abs(drift) < 0.01, "phase lock holds the beats together (" + juce::String(drift, 4) + " beats apart)");

    // The master stopping passes the job on.
    engine.push(command(DjEngine::Command::Type::pause, 0));
    run(engine, 3);
    require(engine.masterDeck.load() == 1, "when the master stops the playing deck becomes master");
    run(engine, 20);
    require(std::abs(slave.playbackRate.load() - 1.0) < 0.01, "and a master plays at its own tempo");

    // No quantisation: a start is immediate.
    engine.quantise.store(static_cast<int>(DjEngine::Quantise::off));
    engine.push(command(DjEngine::Command::Type::play, 0));
    run(engine, 1);
    require(master.currentState() == DjDeck::State::playing, "with quantise off a start is immediate");
}

void checkMixer()
{
    DjEngine engine;
    engine.prepare(deviceRate, block);
    engine.setDeckCount(2);
    engine.quantise.store(static_cast<int>(DjEngine::Quantise::off));
    const auto level = [&engine](double frequency)
    {
        engine.setTrack(0, toneTrack(frequency, deviceRate, 4.0), false);
        run(engine, 1);
        auto seek = command(DjEngine::Command::Type::seek, 0);
        seek.value = 0.0;
        engine.push(seek);
        engine.push(command(DjEngine::Command::Type::play, 0));
        run(engine, 20);   // settle the filters and the fade
        const auto out = run(engine, 40);
        return rms(out.left);
    };
    auto& strip = engine.channel(0);
    // The isolator's crossovers are fourth order, 24 dB an octave, so a kill
    // is measured two octaves clear of the nearest crossover and asked for
    // thirty of them.
    const auto flat60 = level(60.0), flat1k = level(1000.0), flat12k = level(12000.0), flat100 = level(100.0);
    require(flat60 > 0.3f && flat1k > 0.3f && flat12k > 0.3f, "a flat strip passes every band");
    strip.lowDb.store(DjIsolator::killDb);
    require(decibels(level(60.0) / flat60) < -30.0f, "a low kill removes 60 Hz");
    require(std::abs(decibels(level(1000.0) / flat1k)) < 1.0f, "and leaves 1 kHz");
    strip.lowDb.store(0.0f);
    strip.midDb.store(DjIsolator::killDb);
    require(decibels(level(1000.0) / flat1k) < -30.0f, "a mid kill removes 1 kHz");
    require(std::abs(decibels(level(60.0) / flat60)) < 1.0f, "and leaves 60 Hz");
    strip.midDb.store(0.0f);
    strip.highDb.store(DjIsolator::killDb);
    require(decibels(level(12000.0) / flat12k) < -30.0f, "a high kill removes 12 kHz");
    strip.highDb.store(6.0f);
    require(std::abs(decibels(level(12000.0) / flat12k) - 6.0f) < 0.7f, "high at +6 dB lifts 12 kHz by 6 dB");
    strip.highDb.store(0.0f);
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
    strip.fader.store(1.0f);

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
    require(decibels(level(60.0) / flat60) < -30.0f, "the master isolator kills 60 Hz");
    engine.master().lowDb.store(0.0f);
    level(1000.0);
    require(engine.master().meterLeft.load() > 0.3f && strip.meter.load() > 0.3f, "the meters read the tone");

    // The cue bus: a channel cued is on outputs 3 and 4 with the fader down.
    strip.fader.store(0.0f);
    strip.cue.store(true);
    engine.setTrack(0, toneTrack(1000.0, deviceRate, 4.0), false);
    run(engine, 1);
    engine.push(command(DjEngine::Command::Type::play, 0));
    run(engine, 10);
    std::vector<float> a(block), b(block), c(block), d(block);
    float* four[4] {a.data(), b.data(), c.data(), d.data()};
    for (int i = 0; i < 20; ++i)
        engine.process(four, 4, block);
    require(rms(a) < 1.0e-4f && rms(c) > 0.3f, "a cued channel with its fader down is heard on the cue outputs only");
    strip.cue.store(false);
    strip.fader.store(1.0f);
}

void checkBeatFx()
{
    DjEngine engine;
    engine.prepare(deviceRate, block);
    engine.setDeckCount(1);
    engine.quantise.store(static_cast<int>(DjEngine::Quantise::off));
    // A click, then silence: the echo's repeat is a second click one beat
    // later at the master tempo. With no master playing the effect runs at
    // 128 BPM, so a beat is 22500 frames.
    auto track = std::make_unique<DjTrack>();
    track->sampleRate = deviceRate;
    track->left.assign(static_cast<size_t>(deviceRate * 4.0), 0.0f);
    track->left[1000] = 1.0f;
    engine.setTrack(0, std::move(track), false);
    run(engine, 1);
    auto& fx = engine.fx();
    fx.type.store(static_cast<int>(DjBeatFx::Type::echo));
    fx.beats.store(1.0f);
    fx.depth.store(0.5f);
    fx.target.store(-1);
    fx.on.store(true);
    engine.push(command(DjEngine::Command::Type::play, 0));
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
    engine.setTrack(0, toneTrack(1000.0, deviceRate, 8.0), false);
    run(engine, 1);
    fx.type.store(static_cast<int>(DjBeatFx::Type::trans));
    fx.beats.store(1.0f);
    fx.depth.store(1.0f);
    fx.on.store(true);
    auto seek = command(DjEngine::Command::Type::seek, 0);
    engine.push(seek);
    engine.push(command(DjEngine::Command::Type::play, 0));
    run(engine, 10);
    const auto gated = run(engine, 100);
    int silentRuns = 0, loudRuns = 0;
    bool loud = rms(gated.left, 0, 256) > 0.05f;
    for (size_t at = 256; at + 256 <= gated.left.size(); at += 256)
    {
        const auto now = rms(gated.left, at, at + 256) > 0.05f;
        if (now != loud)
        {
            if (now) ++loudRuns; else ++silentRuns;
            loud = now;
        }
    }
    require(silentRuns >= 2 && loudRuns >= 2, "trans chops the tone on and off at the beat");
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
}

void checkDjCore(Session&)
{
    const auto started = juce::Time::getMillisecondCounter();
    checkDeckTransport();
    checkEngineHousekeeping();
    checkQuantisedStartAndSync();
    checkMixer();
    checkBeatFx();
    const auto analysisStarted = juce::Time::getMillisecondCounter();
    checkAnalysis();
    juce::Logger::writeToLog("Rhino: DJ booth checks passed in " + juce::String(juce::Time::getMillisecondCounter() - started)
                             + " ms, a minute of beat analysed in "
                             + juce::String(juce::Time::getMillisecondCounter() - analysisStarted) + " ms");
}
}
