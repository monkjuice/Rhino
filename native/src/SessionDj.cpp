#include "SessionDjInternal.h"
#include <algorithm>
#include <cmath>

// The DJ booth: decks, their transports, the mixer and what the document
// saves of it. What a deck plays, and how a track's deck is heard through the
// track, is in SessionDjSources.cpp.
//
// The engine is a second audio callback on the engine's own device, the
// arrangement the browser preview and the count-in already use: JUCE sums
// its callbacks, so the booth is simply mixed with whatever the edit plays.
// It is built the first time a deck is added, and comes off the device in
// releaseAudioDevice and the destructor, before the device it reads goes.
//
// Everything public here names a deck by its place on the console, and a
// mixer channel by the same place; the booth turns that into the engine slot
// the deck plays in (DjBooth::slotOfDeck), which stays the deck's while it is
// on the console.

namespace rhino
{
const juce::Identifier djStateID {"rhinoDj"};

namespace
{
const juce::Identifier djDeckID {"DECK"};
const juce::Identifier djChannelID {"CHANNEL"};
const juce::Identifier djHotCueID {"HOTCUE"};

constexpr int quantiseCount = 4;
constexpr int tempoRanges[] {6, 10, 16, 100};

// The tempo fader's range on a deck holding a tempo it took over as master:
// the range it was set to, widened to the narrowest of the four that reaches
// the held tempo, so the fader stands where the deck plays and the first
// touch moves on from there. Synced across more than six per cent - common -
// the held tempo lies past the default range. Zero held is no hold.
int rangeReaching(int range, double held)
{
    if (held <= 0.0) return range;
    const auto percent = std::abs(held - 1.0) * 100.0;
    for (const auto wider : tempoRanges)
        if (wider >= range && percent <= wider + 1.0e-9)
            return wider;
    return tempoRanges[std::size(tempoRanges) - 1];
}

using Command = DjEngine::Command;

Session::DjDeckState::Transport transportOf(DjDeck::State s)
{
    using Transport = Session::DjDeckState::Transport;
    switch (s)
    {
        case DjDeck::State::empty: return Transport::empty;
        case DjDeck::State::stopped: return Transport::stopped;
        case DjDeck::State::playing: return Transport::playing;
        case DjDeck::State::waiting: return Transport::waiting;
        case DjDeck::State::cueing: return Transport::cueing;
    }
    return Transport::empty;
}
}

Session::DjBooth::DjBooth()
{
    formats.registerBasicFormats();
}

// The worker is drained before the jobs go with the booth.
Session::DjBooth::~DjBooth()
{
    for (auto& job : jobs)
        job->cancel.store(true);
    workers.removeAllJobs(true, 10000);
    jobs.clear();
}

void Session::DjBooth::audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
                                                        float* const* outputChannelData, int numOutputChannels, int numSamples,
                                                        const juce::AudioIODeviceCallbackContext&)
{
    // Every gain and filter in the booth decays towards zero, and nothing
    // says the device's thread already flushes denormals; djProcessOffline
    // comes through here too, so the tests run under the same rule.
    const juce::ScopedNoDenormals noDenormals;
    // The scratch buffer a second callback is handed holds this callback's
    // own previous block, as CountInClick explains, so the engine clears it
    // before it writes. The device's first input is the mic.
    engine.process(inputChannelData, numInputChannels, outputChannelData, numOutputChannels, numSamples);
}

void Session::DjBooth::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (device == nullptr) return;
    engine.prepare(device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
}

void Session::DjBooth::audioDeviceStopped() {}

Session::DjBooth& Session::booth()
{
    if (dj == nullptr)
        dj = std::make_unique<DjBooth>();
    return *dj;
}

void Session::ensureDjAttached()
{
    auto& b = booth();
    // The test runners drive the booth themselves, a block at a time through
    // djProcessOffline, and render their bounces inline; a device thread
    // calling the same callback meanwhile would be a second consumer of the
    // engine's single-consumer queue and a second renderer of its decks.
    if (b.attached || isCommandLineTestMode()) return;
    engine.getDeviceManager().deviceManager.addAudioCallback(&b);
    b.attached = true;
}

// Before the device closes and again from the destructor, because either can
// come first: the callback must be off the device manager while both it and
// the engine it drives are alive.
void Session::releaseDj()
{
    if (dj == nullptr || !dj->attached) return;
    engine.getDeviceManager().deviceManager.removeAudioCallback(dj.get());
    dj->attached = false;
}

bool Session::djEngineAttached() const
{
    return dj != nullptr && dj->attached;
}

juce::String Session::djQuantiseName(DjQuantise q)
{
    switch (q)
    {
        case DjQuantise::off: return "Off";
        case DjQuantise::beat: return "1 Beat";
        case DjQuantise::bar: return "1 Bar";
        case DjQuantise::fourBars: return "4 Bars";
    }
    return {};
}

juce::String Session::djFxTypeName(int type)
{
    if (type < 0 || type >= DjBeatFx::typeCount) return {};
    return DjBeatFx::typeName(static_cast<DjBeatFx::Type>(type));
}

int Session::djFxTypeCount()
{
    return DjBeatFx::typeCount;
}

juce::String Session::djSendFxTypeName(int type)
{
    if (type < 0 || type >= DjSendFx::typeCount) return {};
    return DjSendFx::typeName(static_cast<DjSendFx::Type>(type));
}

int Session::djSendFxTypeCount()
{
    return DjSendFx::typeCount;
}

// A DJ change the document saves - a deck added, a source loaded, a hot cue
// set - counts as a change to the document, but not as one that changes what
// a clip plays.
void Session::markDjModified()
{
    markModified(-1, ModelChange::none);
}

int Session::djDeckCount() const
{
    return dj != nullptr ? dj->count : 0;
}

// A new deck takes the first free slot, its controls and its strip back to a
// new deck's - but for the resonance and the curve the console keeps on every
// channel - so it inherits nothing from a deck that stood there before.
juce::Result Session::addDjDeck()
{
    auto& b = booth();
    if (b.count >= maximumDjDecks)
        return juce::Result::fail("Six decks is the booth: there is no room for another.");
    const auto slot = b.slotOfDeck[static_cast<size_t>(b.count)];
    b.engine.deck(slot).controls.reset();
    b.engine.resetChannelControls(slot);
    if (!b.engine.setSlotActive(slot, true))
        return juce::Result::fail("The DJ booth is busy: try again in a moment.");
    b.decks[static_cast<size_t>(slot)] = {};
    ++b.count;
    ensureDjAttached();
    markDjModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

// A deck taken off frees its slot and its track and nothing else. The decks
// after it close up on the console and in the mixer, as channels would, and go
// on playing what they play from where they stand, strips and settings
// untouched: none is read or made again. A beat effect assigned to it stays on the freed
// slot, which is rendered no more, so it falls silent rather than moving on
// to the master; the slot's channel stands past the console, and a deck
// added there later starts with its FX key dark.
juce::Result Session::removeDjDeck(int index)
{
    if (dj == nullptr || !juce::isPositiveAndBelow(index, dj->count))
        return juce::Result::fail("That deck does not exist.");
    auto& b = *dj;
    const auto slot = b.slotOf(index);
    // The engine empties the slot at its next block and hands its material
    // back; the registry lets go of it here.
    if (!b.engine.setSlotActive(slot, false))
        return juce::Result::fail("The DJ booth is busy: try again in a moment.");
    if (b.decks[static_cast<size_t>(slot)].info.live)
        setDjDeckLive(index, false);
    // Its track plays the song again.
    bindDjTrack(slot, -1);
    for (auto& job : b.jobs)
        if (job->slot == slot)
            job->cancel.store(true);
    b.decks[static_cast<size_t>(slot)] = {};
    // The console closes up, and the freed slot stands first among the free.
    std::rotate(b.slotOfDeck.begin() + index, b.slotOfDeck.begin() + index + 1, b.slotOfDeck.begin() + b.count);
    --b.count;
    markDjModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

Session::DjDeckInfo Session::djDeckInfo(int index) const
{
    return dj != nullptr ? djSlotInfo(dj->slotOf(index)) : DjDeckInfo {};
}

Session::DjDeckInfo Session::djSlotInfo(int slot) const
{
    if (dj == nullptr || !juce::isPositiveAndBelow(slot, maximumDjDecks))
        return {};
    auto info = dj->decks[static_cast<size_t>(slot)].info;
    // Where the track is now, found by id every time: tracks move.
    info.track = -1;
    if (info.kind == DjSourceKind::track)
    {
        const auto tracks = te::getAudioTracks(*edit);
        for (int i = 0; i < tracks.size(); ++i)
            if (tracks[i]->itemID == info.trackId)
                info.track = i;
    }
    return info;
}

const DjTrack* Session::djDeckTrack(int index) const
{
    return dj != nullptr ? dj->engine.trackOf(dj->slotOf(index)) : nullptr;
}

Session::DjDeckState Session::djDeckState(int index) const
{
    DjDeckState state;
    const auto slot = dj != nullptr ? dj->slotOf(index) : -1;
    if (slot < 0)
        return state;
    const auto& deck = dj->engine.deck(slot);
    const auto& controls = deck.controls;
    const auto* track = dj->engine.trackOf(slot);
    state.transport = transportOf(deck.currentState());
    state.pendingJump = deck.currentPending() == DjDeck::Pending::jump;
    // A deck that took over as master holds the tempo it was synced to
    // until its fader moves, and the fader shows that tempo meanwhile, its
    // range as wide as it takes to reach it.
    const auto held = deck.heldTempoRate.load(std::memory_order_relaxed);
    state.tempoRange = rangeReaching(controls.tempoRange.load(std::memory_order_relaxed), held);
    state.tempoPercent = held > 0.0 ? static_cast<float>((held - 1.0) * 100.0) : controls.tempoPercent.load(std::memory_order_relaxed);
    state.synced = controls.synced.load(std::memory_order_relaxed);
    state.reversed = controls.reversed.load(std::memory_order_relaxed);
    state.brakeSeconds = controls.brakeSeconds.load(std::memory_order_relaxed);
    state.quantiseSnap = controls.quantiseSnap.load(std::memory_order_relaxed);
    state.scratching = controls.scratching.load(std::memory_order_relaxed);
    state.master = dj->engine.masterDeck.load(std::memory_order_relaxed) == slot;
    state.peak = deck.peak.load(std::memory_order_relaxed);
    state.jumps = deck.jumps.load(std::memory_order_relaxed);
    state.hotCueSeconds.fill(-1.0);
    if (track == nullptr || track->length() == 0)
        return state;
    const auto rate = track->sampleRate;
    const auto seconds = [rate](double frames) { return frames >= 0.0 ? frames / rate : -1.0; };
    state.positionSeconds = seconds(deck.position.load(std::memory_order_relaxed));
    state.lengthSeconds = track->seconds();
    state.bpm = track->analysis.bpm;
    // The tempo, as a CDJ's readout shows it: the fader, sync or the held
    // tempo, never the platter under a hand or a brake.
    state.effectiveBpm = std::abs(track->analysis.bpm * deck.tempoRate.load(std::memory_order_relaxed));
    state.firstBeatSeconds = track->analysis.firstBeatSeconds;
    state.beatsPerBar = track->analysis.beatsPerBar;
    state.beat = track->beatAtFrame(deck.position.load(std::memory_order_relaxed));
    state.cueSeconds = seconds(deck.cuePoint.load(std::memory_order_relaxed));
    for (int i = 0; i < DjDeck::hotCueCount; ++i)
        state.hotCueSeconds[static_cast<size_t>(i)] = seconds(deck.hotCues[static_cast<size_t>(i)].load(std::memory_order_relaxed));
    state.loop.startSeconds = seconds(deck.loopStart.load(std::memory_order_relaxed));
    state.loop.endSeconds = seconds(deck.loopEnd.load(std::memory_order_relaxed));
    state.loop.active = deck.loopActive.load(std::memory_order_relaxed) && state.loop.valid();
    state.keyIndex = track->analysis.keyIndex;
    return state;
}

// ---- transport -------------------------------------------------------------

namespace
{
// A command for the slot a deck of the console plays in, made by one of the
// engine's factories and queued for the audio thread.
template <typename Make>
void enqueue(Session::DjBooth* booth, int deck, Make&& make)
{
    const auto slot = booth != nullptr ? booth->slotOf(deck) : -1;
    if (slot < 0) return;
    if (!booth->engine.push(make(slot)))
        juce::Logger::writeToLog("Rhino: the DJ booth's command queue is full; a command was dropped");
}

// The controls of a deck on the console, or null.
DjDeckControls* controlsOf(Session::DjBooth* booth, int deck)
{
    const auto slot = booth != nullptr ? booth->slotOf(deck) : -1;
    return slot >= 0 ? &booth->engine.deck(slot).controls : nullptr;
}
}

void Session::djPlay(int deck) { enqueue(dj.get(), deck, Command::play); }
void Session::djPause(int deck) { enqueue(dj.get(), deck, Command::pause); }
void Session::djTogglePlay(int deck) { enqueue(dj.get(), deck, Command::togglePlay); }
void Session::djCueDown(int deck) { enqueue(dj.get(), deck, Command::cueDown); }
void Session::djCueUp(int deck) { enqueue(dj.get(), deck, Command::cueUp); }
void Session::djLoopIn(int deck) { enqueue(dj.get(), deck, Command::loopIn); }
void Session::djLoopOut(int deck) { enqueue(dj.get(), deck, Command::loopOut); }
void Session::djReloopExit(int deck) { enqueue(dj.get(), deck, Command::reloopExit); }
void Session::djLoopHalve(int deck) { enqueue(dj.get(), deck, Command::loopHalve); }
void Session::djLoopDouble(int deck) { enqueue(dj.get(), deck, Command::loopDouble); }
void Session::djClearLoop(int deck) { enqueue(dj.get(), deck, Command::clearLoop); }
void Session::djSetMaster(int deck) { enqueue(dj.get(), deck, Command::setMaster); }

void Session::djHotCue(int deck, int index)
{
    enqueue(dj.get(), deck, [index](int slot) { return Command::hotCue(slot, index); });
    // A hot cue set is saved with the document; which of the two this press
    // was is not known until the audio thread has applied it, so the mark is
    // made either way.
    if (dj != nullptr) markDjModified();
}

void Session::djClearHotCue(int deck, int index)
{
    enqueue(dj.get(), deck, [index](int slot) { return Command::clearHotCue(slot, index); });
    if (dj != nullptr) markDjModified();
}

void Session::djBeatLoop(int deck, double beats)
{
    enqueue(dj.get(), deck, [beats](int slot) { return Command::beatLoop(slot, beats); });
}

void Session::djBeatJump(int deck, int beats)
{
    enqueue(dj.get(), deck, [beats](int slot) { return Command::beatJump(slot, beats); });
}

void Session::djSeek(int deck, double seconds)
{
    const auto* track = djDeckTrack(deck);
    if (track == nullptr) return;
    const auto frame = std::max(0.0, seconds) * track->sampleRate;
    enqueue(dj.get(), deck, [frame](int slot) { return Command::seek(slot, frame); });
}

void Session::djSetSynced(int deck, bool synced)
{
    auto* controls = controlsOf(dj.get(), deck);
    if (controls == nullptr) return;
    controls->synced.store(synced, std::memory_order_relaxed);
    if (synced)
        enqueue(dj.get(), deck, Command::syncAlign);
    sendSynchronousChangeMessage();
}

void Session::djSetReversed(int deck, bool reversed)
{
    auto* controls = controlsOf(dj.get(), deck);
    if (controls == nullptr) return;
    controls->reversed.store(reversed, std::memory_order_relaxed);
    sendSynchronousChangeMessage();
}

// The fader moving is also what lets go of a tempo the deck has held since it
// took over as master. It moves from where it was shown, on the range it was
// shown on, which the deck keeps.
void Session::djSetTempoPercent(int deck, float percent)
{
    const auto slot = dj != nullptr ? dj->slotOf(deck) : -1;
    if (slot < 0) return;
    auto& d = dj->engine.deck(slot);
    const auto held = d.heldTempoRate.load(std::memory_order_relaxed);
    const auto range = rangeReaching(d.controls.tempoRange.load(std::memory_order_relaxed), held);
    d.controls.tempoRange.store(range, std::memory_order_relaxed);
    const auto limit = static_cast<float>(range);
    d.controls.tempoPercent.store(juce::jlimit(-limit, limit, percent), std::memory_order_relaxed);
    if (held > 0.0)
        enqueue(dj.get(), deck, Command::tempoMoved);
}

// The range belongs to the fader, so choosing one while the deck holds a
// tempo hands the held tempo to the fader, inside the new range.
void Session::djSetTempoRange(int deck, int range)
{
    const auto slot = dj != nullptr ? dj->slotOf(deck) : -1;
    if (slot < 0) return;
    if (std::find(std::begin(tempoRanges), std::end(tempoRanges), range) == std::end(tempoRanges)) return;
    auto& d = dj->engine.deck(slot);
    auto& controls = d.controls;
    const auto held = d.heldTempoRate.load(std::memory_order_relaxed);
    const auto percent = held > 0.0 ? static_cast<float>((held - 1.0) * 100.0) : controls.tempoPercent.load(std::memory_order_relaxed);
    controls.tempoRange.store(range, std::memory_order_relaxed);
    const auto limit = static_cast<float>(range);
    controls.tempoPercent.store(juce::jlimit(-limit, limit, percent), std::memory_order_relaxed);
    if (held > 0.0)
        enqueue(dj.get(), deck, Command::tempoMoved);
    sendSynchronousChangeMessage();
}

void Session::djNudge(int deck, float percent)
{
    if (auto* controls = controlsOf(dj.get(), deck))
        controls->nudgePercent.store(juce::jlimit(-50.0f, 50.0f, percent), std::memory_order_relaxed);
}

void Session::djScratch(int deck, double rate, bool active)
{
    auto* controls = controlsOf(dj.get(), deck);
    if (controls == nullptr) return;
    controls->scratchRate.store(juce::jlimit(-10.0, 10.0, rate), std::memory_order_relaxed);
    controls->scratching.store(active, std::memory_order_relaxed);
}

void Session::djSetBrake(int deck, float seconds)
{
    if (auto* controls = controlsOf(dj.get(), deck))
        controls->brakeSeconds.store(juce::jlimit(0.0f, 4.0f, seconds), std::memory_order_relaxed);
}

void Session::djSetQuantiseSnap(int deck, bool snap)
{
    auto* controls = controlsOf(dj.get(), deck);
    if (controls == nullptr) return;
    controls->quantiseSnap.store(snap, std::memory_order_relaxed);
    sendSynchronousChangeMessage();
}

void Session::djJumpToCue(int deck, bool forward)
{
    const auto state = djDeckState(deck);
    if (state.transport == DjDeckState::Transport::empty) return;
    const auto here = state.positionSeconds;
    std::vector<double> points;
    points.push_back(state.cueSeconds);
    for (const auto cue : state.hotCueSeconds)
        if (cue >= 0.0) points.push_back(cue);
    auto target = forward ? -1.0 : 0.0;
    for (const auto point : points)
    {
        if (forward && point > here + 0.05 && (target < 0.0 || point < target)) target = point;
        if (!forward && point < here - 0.05 && point > target) target = point;
    }
    if (target >= 0.0)
        djSeek(deck, target);
}

void Session::djStopAll()
{
    if (dj != nullptr && !dj->engine.push(Command::stopAll()))
        juce::Logger::writeToLog("Rhino: the DJ booth's command queue is full; a command was dropped");
}

Session::DjQuantise Session::djQuantise() const
{
    if (dj == nullptr) return DjQuantise::bar;
    return static_cast<DjQuantise>(juce::jlimit(0, quantiseCount - 1, dj->engine.quantise.load(std::memory_order_relaxed)));
}

void Session::setDjQuantise(DjQuantise q)
{
    booth().engine.quantise.store(static_cast<int>(q), std::memory_order_relaxed);
    markDjModified();
    sendSynchronousChangeMessage();
}

bool Session::djPhaseLock() const
{
    return dj == nullptr ? true : dj->engine.phaseLock.load(std::memory_order_relaxed);
}

void Session::setDjPhaseLock(bool lock)
{
    booth().engine.phaseLock.store(lock, std::memory_order_relaxed);
    sendSynchronousChangeMessage();
}

// ---- mixer -----------------------------------------------------------------

Session::DjChannelState Session::djChannel(int index) const
{
    DjChannelState state;
    const auto slot = dj != nullptr ? dj->channelSlot(index) : -1;
    if (slot < 0)
        return state;
    const auto& strip = dj->engine.channel(slot);
    state.trimDb = strip.trimDb.load(std::memory_order_relaxed);
    state.comp = strip.comp.load(std::memory_order_relaxed);
    for (size_t band = 0; band < DjChannelStrip::bandCount; ++band)
        state.eqDb[band] = strip.eqDb[band].load(std::memory_order_relaxed);
    state.filter = strip.filter.load(std::memory_order_relaxed);
    state.resonance = strip.resonance.load(std::memory_order_relaxed);
    state.fader = strip.fader.load(std::memory_order_relaxed);
    state.faderCurve = strip.faderCurve.load(std::memory_order_relaxed);
    state.send = strip.send.load(std::memory_order_relaxed);
    state.cue = strip.cue.load(std::memory_order_relaxed);
    state.fx = strip.fxOn.load(std::memory_order_relaxed);
    state.crossfaderSide = strip.crossfaderSide.load(std::memory_order_relaxed);
    state.meter = strip.meter.load(std::memory_order_relaxed);
    return state;
}

namespace
{
// The strip behind a channel of the mixer: a deck's, or one of the free
// slots', which the console sets the booth-wide controls on too.
DjChannelStrip* stripFor(Session::DjBooth* booth, int index)
{
    const auto slot = booth != nullptr ? booth->channelSlot(index) : -1;
    return slot >= 0 ? &booth->engine.channel(slot) : nullptr;
}

float eqDb(float decibels)
{
    return decibels <= Session::djKillDb ? DjIsolator::killDb : juce::jlimit(Session::djKillDb, DjChannelStrip::maximumEqDb, decibels);
}
}

void Session::setDjChannelTrim(int index, float decibels)
{
    if (auto* strip = stripFor(dj.get(), index))
        strip->trimDb.store(juce::jlimit(DjChannelStrip::minimumTrimDb, DjChannelStrip::maximumTrimDb, decibels), std::memory_order_relaxed);
}

void Session::setDjChannelComp(int index, float amount)
{
    if (auto* strip = stripFor(dj.get(), index))
        strip->comp.store(juce::jlimit(0.0f, 1.0f, amount), std::memory_order_relaxed);
}

void Session::setDjChannelEq(int index, int band, float decibels)
{
    auto* strip = stripFor(dj.get(), index);
    if (strip == nullptr || !juce::isPositiveAndBelow(band, DjChannelStrip::bandCount)) return;
    strip->eqDb[static_cast<size_t>(band)].store(eqDb(decibels), std::memory_order_relaxed);
}

void Session::setDjChannelFaderCurve(int index, int curve)
{
    if (auto* strip = stripFor(dj.get(), index))
    {
        strip->faderCurve.store(juce::jlimit(0, 2, curve), std::memory_order_relaxed);
        sendSynchronousChangeMessage();
    }
}

void Session::setDjChannelSend(int index, float level)
{
    if (auto* strip = stripFor(dj.get(), index))
        strip->send.store(juce::jlimit(0.0f, 1.0f, level), std::memory_order_relaxed);
}

void Session::setDjChannelFilter(int index, float amount)
{
    if (auto* strip = stripFor(dj.get(), index))
        strip->filter.store(juce::jlimit(-1.0f, 1.0f, amount), std::memory_order_relaxed);
}

void Session::setDjChannelResonance(int index, float amount)
{
    if (auto* strip = stripFor(dj.get(), index))
        strip->resonance.store(juce::jlimit(0.0f, 1.0f, amount), std::memory_order_relaxed);
}

void Session::setDjChannelFader(int index, float position)
{
    if (auto* strip = stripFor(dj.get(), index))
        strip->fader.store(juce::jlimit(0.0f, 1.0f, position), std::memory_order_relaxed);
}

void Session::setDjChannelCue(int index, bool on)
{
    if (auto* strip = stripFor(dj.get(), index))
    {
        strip->cue.store(on, std::memory_order_relaxed);
        sendSynchronousChangeMessage();
    }
}

void Session::setDjChannelFx(int index, bool on)
{
    if (auto* strip = stripFor(dj.get(), index))
    {
        strip->fxOn.store(on, std::memory_order_relaxed);
        sendSynchronousChangeMessage();
    }
}

void Session::setDjChannelCrossfaderSide(int index, int side)
{
    if (auto* strip = stripFor(dj.get(), index))
    {
        strip->crossfaderSide.store(juce::jlimit(0, 2, side), std::memory_order_relaxed);
        sendSynchronousChangeMessage();
    }
}

Session::DjMasterState Session::djMaster() const
{
    DjMasterState state;
    if (dj == nullptr) return state;
    const auto& master = dj->engine.master();
    state.crossfader = dj->engine.crossfader().position.load(std::memory_order_relaxed);
    state.crossfaderCurve = dj->engine.crossfader().curve.load(std::memory_order_relaxed);
    state.levelDb = master.levelDb.load(std::memory_order_relaxed);
    state.lowDb = master.lowDb.load(std::memory_order_relaxed);
    state.midDb = master.midDb.load(std::memory_order_relaxed);
    state.highDb = master.highDb.load(std::memory_order_relaxed);
    state.cueMix = master.cueMix.load(std::memory_order_relaxed);
    state.cueLevelDb = master.cueLevelDb.load(std::memory_order_relaxed);
    state.monoSplit = master.monoSplit.load(std::memory_order_relaxed);
    state.meterLeft = master.meterLeft.load(std::memory_order_relaxed);
    state.meterRight = master.meterRight.load(std::memory_order_relaxed);
    state.bpm = dj->engine.masterBpm.load(std::memory_order_relaxed);
    state.tapBpm = dj->engine.tapBpm.load(std::memory_order_relaxed);
    state.masterDeck = dj->deckOfSlot(dj->engine.masterDeck.load(std::memory_order_relaxed));
    return state;
}

void Session::setDjCueLevel(float decibels)
{
    booth().engine.master().cueLevelDb.store(juce::jlimit(DjMasterSection::minimumLevelDb, DjMasterSection::maximumLevelDb, decibels),
                                             std::memory_order_relaxed);
}

void Session::setDjMonoSplit(bool split)
{
    booth().engine.master().monoSplit.store(split, std::memory_order_relaxed);
    sendSynchronousChangeMessage();
}

Session::DjMicState Session::djMic() const
{
    DjMicState state;
    if (dj == nullptr) return state;
    const auto& mic = dj->engine.mic();
    state.levelDb = mic.levelDb.load(std::memory_order_relaxed);
    state.lowDb = mic.lowDb.load(std::memory_order_relaxed);
    state.highDb = mic.highDb.load(std::memory_order_relaxed);
    state.mode = mic.mode.load(std::memory_order_relaxed);
    state.meter = mic.meter.load(std::memory_order_relaxed);
    return state;
}

void Session::setDjMicLevel(float decibels)
{
    booth().engine.mic().levelDb.store(juce::jlimit(DjMicSection::minimumLevelDb, DjMicSection::maximumLevelDb, decibels),
                                       std::memory_order_relaxed);
}

void Session::setDjMicEq(int band, float decibels)
{
    auto& mic = booth().engine.mic();
    (band == 0 ? mic.lowDb : mic.highDb).store(juce::jlimit(-12.0f, 12.0f, decibels), std::memory_order_relaxed);
}

void Session::setDjMicMode(int mode)
{
    booth().engine.mic().mode.store(juce::jlimit(0, 2, mode), std::memory_order_relaxed);
    sendSynchronousChangeMessage();
}

Session::DjSendFxState Session::djSendFx() const
{
    DjSendFxState state;
    if (dj == nullptr) return state;
    const auto& send = dj->engine.sendFx();
    state.type = send.type.load(std::memory_order_relaxed);
    state.size = send.size.load(std::memory_order_relaxed);
    state.time = send.time.load(std::memory_order_relaxed);
    state.tone = send.tone.load(std::memory_order_relaxed);
    state.mix = send.mix.load(std::memory_order_relaxed);
    return state;
}

void Session::setDjSendFxType(int type)
{
    booth().engine.sendFx().type.store(juce::jlimit(0, DjSendFx::typeCount - 1, type), std::memory_order_relaxed);
    sendSynchronousChangeMessage();
}

void Session::setDjSendFxSize(float amount) { booth().engine.sendFx().size.store(juce::jlimit(0.0f, 1.0f, amount), std::memory_order_relaxed); }
void Session::setDjSendFxTime(float amount) { booth().engine.sendFx().time.store(juce::jlimit(0.0f, 1.0f, amount), std::memory_order_relaxed); }
void Session::setDjSendFxTone(float amount) { booth().engine.sendFx().tone.store(juce::jlimit(0.0f, 1.0f, amount), std::memory_order_relaxed); }
void Session::setDjSendFxMix(float amount) { booth().engine.sendFx().mix.store(juce::jlimit(0.0f, 1.0f, amount), std::memory_order_relaxed); }

void Session::setDjCrossfader(float position)
{
    booth().engine.crossfader().position.store(juce::jlimit(-1.0f, 1.0f, position), std::memory_order_relaxed);
}

void Session::setDjCrossfaderCurve(float curve)
{
    booth().engine.crossfader().curve.store(juce::jlimit(0.0f, 1.0f, curve), std::memory_order_relaxed);
}

void Session::setDjMasterLevel(float decibels)
{
    booth().engine.master().levelDb.store(juce::jlimit(DjMasterSection::minimumLevelDb, DjMasterSection::maximumLevelDb, decibels),
                                          std::memory_order_relaxed);
}

void Session::setDjMasterEq(int band, float decibels)
{
    auto& master = booth().engine.master();
    auto& target = band == 0 ? master.lowDb : band == 1 ? master.midDb : master.highDb;
    target.store(eqDb(decibels), std::memory_order_relaxed);
}

void Session::setDjCueMix(float mix)
{
    booth().engine.master().cueMix.store(juce::jlimit(0.0f, 1.0f, mix), std::memory_order_relaxed);
}

Session::DjFxState Session::djFx() const
{
    DjFxState state;
    if (dj == nullptr) return state;
    const auto& fx = dj->engine.fx();
    state.type = fx.type.load(std::memory_order_relaxed);
    state.beats = fx.beats.load(std::memory_order_relaxed);
    state.depth = fx.depth.load(std::memory_order_relaxed);
    state.on = fx.on.load(std::memory_order_relaxed);
    const auto target = fx.target.load(std::memory_order_relaxed);
    state.target = target < 0 ? -1 : dj->channelOfSlot(target);
    state.bands = {fx.bandLow.load(std::memory_order_relaxed), fx.bandMid.load(std::memory_order_relaxed),
                   fx.bandHigh.load(std::memory_order_relaxed)};
    state.autoTime = fx.autoTime.load(std::memory_order_relaxed);
    state.manualSeconds = fx.manualSeconds.load(std::memory_order_relaxed);
    return state;
}

void Session::setDjFxBand(int band, bool on)
{
    auto& fx = booth().engine.fx();
    (band == 0 ? fx.bandLow : band == 1 ? fx.bandMid : fx.bandHigh).store(on, std::memory_order_relaxed);
    sendSynchronousChangeMessage();
}

void Session::setDjFxAutoTime(bool automatic)
{
    booth().engine.fx().autoTime.store(automatic, std::memory_order_relaxed);
    sendSynchronousChangeMessage();
}

void Session::setDjFxTime(float seconds)
{
    booth().engine.fx().manualSeconds.store(juce::jlimit(0.001f, 4.0f, seconds), std::memory_order_relaxed);
}

// Two taps or more within two seconds of each other: the tempo is the
// mean of the last few intervals.
void Session::djTapTempo()
{
    auto& b = booth();
    const auto now = juce::Time::getMillisecondCounter();
    if (!b.taps.empty() && now - b.taps.back() > 2000)
        b.taps.clear();
    b.taps.push_back(now);
    while (b.taps.size() > 5)
        b.taps.erase(b.taps.begin());
    if (b.taps.size() < 2) return;
    double total = 0.0;
    for (size_t i = 1; i < b.taps.size(); ++i)
        total += static_cast<double>(b.taps[i] - b.taps[i - 1]);
    const auto interval = total / static_cast<double>(b.taps.size() - 1);
    if (interval <= 0.0) return;
    b.engine.tapBpm.store(juce::jlimit(40.0, 240.0, 60000.0 / interval), std::memory_order_relaxed);
    sendSynchronousChangeMessage();
}

void Session::setDjFxType(int type)
{
    booth().engine.fx().type.store(juce::jlimit(0, DjBeatFx::typeCount - 1, type), std::memory_order_relaxed);
    sendSynchronousChangeMessage();
}

void Session::setDjFxBeats(float beats)
{
    booth().engine.fx().beats.store(juce::jlimit(1.0f / 16.0f, 4.0f, beats), std::memory_order_relaxed);
    sendSynchronousChangeMessage();
}

void Session::setDjFxDepth(float depth)
{
    booth().engine.fx().depth.store(juce::jlimit(0.0f, 1.0f, depth), std::memory_order_relaxed);
}

void Session::setDjFxOn(bool on)
{
    booth().engine.fx().on.store(on, std::memory_order_relaxed);
    sendSynchronousChangeMessage();
}

// The effect is put on a channel of the mixer, which the engine knows by the
// slot behind it.
void Session::setDjFxTarget(int target)
{
    auto& b = booth();
    const auto slot = target < 0 ? -1 : b.channelSlot(juce::jlimit(0, maximumDjDecks - 1, target));
    b.engine.fx().target.store(slot, std::memory_order_relaxed);
    sendSynchronousChangeMessage();
}

void Session::djProcessOffline(int frames)
{
    djProcessOffline(nullptr, 0, frames);
}

void Session::djProcessOffline(const float* const* inputs, int inputChannels, int frames)
{
    std::vector<float> left(static_cast<size_t>(std::max(0, frames))), right(left.size());
    float* outputs[2] {left.data(), right.data()};
    djProcessOffline(inputs, inputChannels, outputs, 2, frames);
}

// Through the booth's own callback rather than the engine, so that what the
// test hands it is what the device would: the callback once dropped the
// inputs on the floor and the engine's own test could not tell.
void Session::djProcessOffline(const float* const* inputs, int inputChannels, float* const* outputs, int outputChannels, int frames)
{
    if (dj == nullptr || frames <= 0) return;
    // A booth on a device is rendered by the device's thread, which this
    // would race; under the test runners it never goes on one.
    jassert(!dj->attached);
    dj->audioDeviceIOCallbackWithContext(inputs, inputChannels, outputs, outputChannels, frames, juce::AudioIODeviceCallbackContext{});
    dj->engine.collect();
}

// ---- the document ----------------------------------------------------------

// What the document keeps of the booth: the decks' sources and settings,
// the mixer and the effect. Positions are not kept; a reopened set stands
// at the top of each track. Written into the edit's state when a snapshot
// is taken, read back when a document is restored.
void Session::writeDjState()
{
    auto existing = edit->state.getChildWithName(djStateID);
    if (existing.isValid())
        edit->state.removeChild(existing, nullptr);
    if (dj == nullptr || dj->count == 0)
        return;
    auto& b = *dj;
    juce::ValueTree state(djStateID);
    state.setProperty("quantise", static_cast<int>(djQuantise()), nullptr);
    state.setProperty("phaseLock", djPhaseLock(), nullptr);
    const auto master = djMaster();
    state.setProperty("crossfader", master.crossfader, nullptr);
    state.setProperty("crossfaderCurve", master.crossfaderCurve, nullptr);
    state.setProperty("masterLevel", master.levelDb, nullptr);
    state.setProperty("masterLow", master.lowDb, nullptr);
    state.setProperty("masterMid", master.midDb, nullptr);
    state.setProperty("masterHigh", master.highDb, nullptr);
    state.setProperty("cueMix", master.cueMix, nullptr);
    state.setProperty("cueLevel", master.cueLevelDb, nullptr);
    state.setProperty("monoSplit", master.monoSplit, nullptr);
    state.setProperty("tapBpm", master.tapBpm, nullptr);
    const auto fx = djFx();
    state.setProperty("fxType", fx.type, nullptr);
    state.setProperty("fxBeats", fx.beats, nullptr);
    state.setProperty("fxDepth", fx.depth, nullptr);
    state.setProperty("fxOn", fx.on, nullptr);
    state.setProperty("fxTarget", fx.target, nullptr);
    state.setProperty("fxLow", fx.bands[0], nullptr);
    state.setProperty("fxMid", fx.bands[1], nullptr);
    state.setProperty("fxHigh", fx.bands[2], nullptr);
    state.setProperty("fxAuto", fx.autoTime, nullptr);
    state.setProperty("fxSeconds", fx.manualSeconds, nullptr);
    const auto mic = djMic();
    state.setProperty("micLevel", mic.levelDb, nullptr);
    state.setProperty("micLow", mic.lowDb, nullptr);
    state.setProperty("micHigh", mic.highDb, nullptr);
    state.setProperty("micMode", mic.mode, nullptr);
    const auto send = djSendFx();
    state.setProperty("sendType", send.type, nullptr);
    state.setProperty("sendSize", send.size, nullptr);
    state.setProperty("sendTime", send.time, nullptr);
    state.setProperty("sendTone", send.tone, nullptr);
    state.setProperty("sendMix", send.mix, nullptr);
    for (int i = 0; i < b.count; ++i)
    {
        const auto& deck = b.decks[static_cast<size_t>(b.slotOf(i))];
        const auto live = djDeckState(i);
        juce::ValueTree d(djDeckID);
        d.setProperty("kind", static_cast<int>(deck.info.kind), nullptr);
        d.setProperty("name", deck.info.name, nullptr);
        if (deck.info.kind == DjSourceKind::file)
            d.setProperty("file", ContentLibrary::storedPath(deck.info.file), nullptr);
        if (deck.info.kind == DjSourceKind::track)
        {
            d.setProperty("track", deck.info.trackId.toString(), nullptr);
            d.setProperty("slot", deck.info.slot, nullptr);
        }
        d.setProperty("live", deck.info.live, nullptr);
        // The monitoring Live replaced, which switching Live off puts back:
        // the track's own monitoring is saved as On while Live holds it.
        if (deck.info.live && deck.monitoringBeforeLive.has_value())
            d.setProperty("liveRestore", static_cast<int>(*deck.monitoringBeforeLive), nullptr);
        // The settings as the engine holds them now when material is on the
        // deck, else as they were read in, so a document reopened and saved
        // before its decks loaded loses nothing.
        auto settings = deck.settings;
        if (deck.info.loaded)
        {
            settings.tempoPercent = live.tempoPercent;
            settings.tempoRange = live.tempoRange;
            settings.synced = live.synced;
            settings.reversed = live.reversed;
            settings.cueSeconds = live.cueSeconds;
            settings.hotCueSeconds = live.hotCueSeconds;
            settings.loop = live.loop;
            settings.loopWhole = false;
        }
        d.setProperty("tempoPercent", settings.tempoPercent, nullptr);
        d.setProperty("tempoRange", settings.tempoRange, nullptr);
        d.setProperty("synced", settings.synced, nullptr);
        d.setProperty("reversed", settings.reversed, nullptr);
        d.setProperty("brake", live.brakeSeconds, nullptr);
        d.setProperty("quantise", live.quantiseSnap, nullptr);
        d.setProperty("cue", settings.cueSeconds, nullptr);
        d.setProperty("loopStart", settings.loop.startSeconds, nullptr);
        d.setProperty("loopEnd", settings.loop.endSeconds, nullptr);
        d.setProperty("loopActive", settings.loop.active, nullptr);
        d.setProperty("loopWhole", settings.loopWhole, nullptr);
        for (int cue = 0; cue < DjDeck::hotCueCount; ++cue)
            if (settings.hotCueSeconds[static_cast<size_t>(cue)] >= 0.0)
            {
                juce::ValueTree h(djHotCueID);
                h.setProperty("index", cue, nullptr);
                h.setProperty("seconds", settings.hotCueSeconds[static_cast<size_t>(cue)], nullptr);
                d.appendChild(h, nullptr);
            }
        const auto channel = djChannel(i);
        juce::ValueTree c(djChannelID);
        c.setProperty("trim", channel.trimDb, nullptr);
        c.setProperty("comp", channel.comp, nullptr);
        c.setProperty("low", channel.eqDb[0], nullptr);
        c.setProperty("lowMid", channel.eqDb[1], nullptr);
        c.setProperty("highMid", channel.eqDb[2], nullptr);
        c.setProperty("high", channel.eqDb[3], nullptr);
        c.setProperty("filter", channel.filter, nullptr);
        c.setProperty("resonance", channel.resonance, nullptr);
        c.setProperty("fader", channel.fader, nullptr);
        c.setProperty("faderCurve", channel.faderCurve, nullptr);
        c.setProperty("send", channel.send, nullptr);
        c.setProperty("cue", channel.cue, nullptr);
        c.setProperty("fx", channel.fx, nullptr);
        c.setProperty("crossfader", channel.crossfaderSide, nullptr);
        d.appendChild(c, nullptr);
        state.appendChild(d, nullptr);
    }
    edit->state.appendChild(state, nullptr);
}

void Session::readDjState()
{
    djReset();
    const auto state = edit->state.getChildWithName(djStateID);
    if (!state.isValid() || state.getNumChildren() == 0)
        return;
    auto& b = booth();
    b.engine.quantise.store(juce::jlimit(0, quantiseCount - 1, static_cast<int>(state.getProperty("quantise", 2))), std::memory_order_relaxed);
    b.engine.phaseLock.store(static_cast<bool>(state.getProperty("phaseLock", true)), std::memory_order_relaxed);
    setDjCrossfader(static_cast<float>(static_cast<double>(state.getProperty("crossfader", 0.0))));
    setDjCrossfaderCurve(static_cast<float>(static_cast<double>(state.getProperty("crossfaderCurve", 0.0))));
    setDjMasterLevel(static_cast<float>(static_cast<double>(state.getProperty("masterLevel", 0.0))));
    setDjMasterEq(0, static_cast<float>(static_cast<double>(state.getProperty("masterLow", 0.0))));
    setDjMasterEq(1, static_cast<float>(static_cast<double>(state.getProperty("masterMid", 0.0))));
    setDjMasterEq(2, static_cast<float>(static_cast<double>(state.getProperty("masterHigh", 0.0))));
    setDjCueMix(static_cast<float>(static_cast<double>(state.getProperty("cueMix", 0.0))));
    setDjCueLevel(static_cast<float>(static_cast<double>(state.getProperty("cueLevel", 0.0))));
    b.engine.master().monoSplit.store(static_cast<bool>(state.getProperty("monoSplit", false)), std::memory_order_relaxed);
    b.engine.tapBpm.store(juce::jlimit(40.0, 240.0, static_cast<double>(state.getProperty("tapBpm", 128.0))), std::memory_order_relaxed);
    b.engine.fx().bandLow.store(static_cast<bool>(state.getProperty("fxLow", true)), std::memory_order_relaxed);
    b.engine.fx().bandMid.store(static_cast<bool>(state.getProperty("fxMid", true)), std::memory_order_relaxed);
    b.engine.fx().bandHigh.store(static_cast<bool>(state.getProperty("fxHigh", true)), std::memory_order_relaxed);
    b.engine.fx().autoTime.store(static_cast<bool>(state.getProperty("fxAuto", true)), std::memory_order_relaxed);
    setDjFxTime(static_cast<float>(static_cast<double>(state.getProperty("fxSeconds", 0.25))));
    setDjMicLevel(static_cast<float>(static_cast<double>(state.getProperty("micLevel", 0.0))));
    setDjMicEq(0, static_cast<float>(static_cast<double>(state.getProperty("micLow", 0.0))));
    setDjMicEq(1, static_cast<float>(static_cast<double>(state.getProperty("micHigh", 0.0))));
    b.engine.mic().mode.store(juce::jlimit(0, 2, static_cast<int>(state.getProperty("micMode", 0))), std::memory_order_relaxed);
    b.engine.sendFx().type.store(juce::jlimit(0, DjSendFx::typeCount - 1, static_cast<int>(state.getProperty("sendType", 2))), std::memory_order_relaxed);
    setDjSendFxSize(static_cast<float>(static_cast<double>(state.getProperty("sendSize", 0.5))));
    setDjSendFxTime(static_cast<float>(static_cast<double>(state.getProperty("sendTime", 0.5))));
    setDjSendFxTone(static_cast<float>(static_cast<double>(state.getProperty("sendTone", 0.5))));
    setDjSendFxMix(static_cast<float>(static_cast<double>(state.getProperty("sendMix", 0.5))));
    b.engine.fx().type.store(juce::jlimit(0, DjBeatFx::typeCount - 1, static_cast<int>(state.getProperty("fxType", 1))), std::memory_order_relaxed);
    b.engine.fx().beats.store(juce::jlimit(1.0f / 16.0f, 4.0f, static_cast<float>(static_cast<double>(state.getProperty("fxBeats", 0.5)))), std::memory_order_relaxed);
    b.engine.fx().depth.store(juce::jlimit(0.0f, 1.0f, static_cast<float>(static_cast<double>(state.getProperty("fxDepth", 0.5)))), std::memory_order_relaxed);
    b.engine.fx().on.store(static_cast<bool>(state.getProperty("fxOn", false)), std::memory_order_relaxed);
    const auto fxTarget = static_cast<int>(state.getProperty("fxTarget", -1));
    b.engine.fx().target.store(fxTarget < 0 ? -1 : b.channelSlot(juce::jlimit(0, maximumDjDecks - 1, fxTarget)),
                               std::memory_order_relaxed);
    for (int i = 0; i < state.getNumChildren() && b.count < maximumDjDecks; ++i)
    {
        const auto d = state.getChild(i);
        if (!d.hasType(djDeckID)) continue;
        // Decks are read in the console's order, each into the next free
        // slot, which after the reset is the slot of its own number.
        const auto slot = b.slotOfDeck[static_cast<size_t>(b.count)];
        auto& deck = b.decks[static_cast<size_t>(slot)];
        deck = {};
        if (!b.engine.setSlotActive(slot, true))
        {
            juce::Logger::writeToLog("Rhino: the DJ booth's command queue is full; a deck of the document was not set up");
            break;
        }
        ++b.count;
        deck.info.kind = static_cast<DjSourceKind>(juce::jlimit(0, 2, static_cast<int>(d.getProperty("kind", 0))));
        deck.info.name = d.getProperty("name").toString();
        if (deck.info.kind == DjSourceKind::file)
            deck.info.file = ContentLibrary::resolveStoredPath(d.getProperty("file").toString());
        else if (deck.info.kind == DjSourceKind::track)
        {
            deck.info.trackId = te::EditItemID::fromString(d.getProperty("track").toString());
            deck.info.slot = static_cast<int>(d.getProperty("slot", -1));
        }
        auto& settings = deck.settings;
        settings.tempoPercent = static_cast<float>(static_cast<double>(d.getProperty("tempoPercent", 0.0)));
        settings.tempoRange = static_cast<int>(d.getProperty("tempoRange", 6));
        settings.synced = static_cast<bool>(d.getProperty("synced", false));
        settings.reversed = static_cast<bool>(d.getProperty("reversed", false));
        settings.cueSeconds = static_cast<double>(d.getProperty("cue", 0.0));
        settings.loop.startSeconds = static_cast<double>(d.getProperty("loopStart", -1.0));
        settings.loop.endSeconds = static_cast<double>(d.getProperty("loopEnd", -1.0));
        settings.loop.active = static_cast<bool>(d.getProperty("loopActive", false));
        settings.loopWhole = static_cast<bool>(d.getProperty("loopWhole", false));
        for (int child = 0; child < d.getNumChildren(); ++child)
        {
            const auto h = d.getChild(child);
            if (h.hasType(djHotCueID))
            {
                const auto cue = static_cast<int>(h.getProperty("index", -1));
                if (juce::isPositiveAndBelow(cue, DjDeck::hotCueCount))
                    settings.hotCueSeconds[static_cast<size_t>(cue)] = static_cast<double>(h.getProperty("seconds", -1.0));
            }
            else if (h.hasType(djChannelID))
            {
                auto& strip = b.engine.channel(slot);
                strip.trimDb.store(static_cast<float>(static_cast<double>(h.getProperty("trim", 0.0))), std::memory_order_relaxed);
                strip.comp.store(static_cast<float>(static_cast<double>(h.getProperty("comp", 0.0))), std::memory_order_relaxed);
                strip.eqDb[0].store(eqDb(static_cast<float>(static_cast<double>(h.getProperty("low", 0.0)))), std::memory_order_relaxed);
                strip.eqDb[1].store(eqDb(static_cast<float>(static_cast<double>(h.getProperty("lowMid", 0.0)))), std::memory_order_relaxed);
                strip.eqDb[2].store(eqDb(static_cast<float>(static_cast<double>(h.getProperty("highMid", 0.0)))), std::memory_order_relaxed);
                strip.eqDb[3].store(eqDb(static_cast<float>(static_cast<double>(h.getProperty("high", 0.0)))), std::memory_order_relaxed);
                strip.filter.store(static_cast<float>(static_cast<double>(h.getProperty("filter", 0.0))), std::memory_order_relaxed);
                strip.resonance.store(static_cast<float>(static_cast<double>(h.getProperty("resonance", 0.2))), std::memory_order_relaxed);
                strip.fader.store(static_cast<float>(static_cast<double>(h.getProperty("fader", 1.0))), std::memory_order_relaxed);
                strip.faderCurve.store(juce::jlimit(0, 2, static_cast<int>(h.getProperty("faderCurve", 1))), std::memory_order_relaxed);
                strip.send.store(static_cast<float>(static_cast<double>(h.getProperty("send", 0.0))), std::memory_order_relaxed);
                strip.cue.store(static_cast<bool>(h.getProperty("cue", false)), std::memory_order_relaxed);
                strip.fxOn.store(static_cast<bool>(h.getProperty("fx", false)), std::memory_order_relaxed);
                strip.crossfaderSide.store(juce::jlimit(0, 2, static_cast<int>(h.getProperty("crossfader", 1))), std::memory_order_relaxed);
            }
        }
        // The engine's own settings are set now, a new deck's but for what
        // the document says; the rest wait for the material, which the poll
        // brings.
        DjDeckControls controls;
        controls.tempoRange.store(settings.tempoRange, std::memory_order_relaxed);
        controls.tempoPercent.store(settings.tempoPercent, std::memory_order_relaxed);
        controls.synced.store(settings.synced, std::memory_order_relaxed);
        controls.reversed.store(settings.reversed, std::memory_order_relaxed);
        controls.brakeSeconds.store(juce::jlimit(0.0f, 4.0f, static_cast<float>(static_cast<double>(d.getProperty("brake", 0.0)))),
                                    std::memory_order_relaxed);
        controls.quantiseSnap.store(static_cast<bool>(d.getProperty("quantise", true)), std::memory_order_relaxed);
        b.engine.deck(slot).controls.copyFrom(controls);
        // The poll binds a track deck to its track and reads each deck's
        // material again: a file from its file, a cell from its clip.
        deck.materialDirty = deck.info.kind == DjSourceKind::file
                          || (deck.info.kind == DjSourceKind::track && deck.info.slot >= 0);
        if (static_cast<bool>(d.getProperty("live", false)) && deck.info.kind == DjSourceKind::track)
        {
            deck.info.live = true;
            // The track comes back monitoring as Live left it; what it
            // monitored before Live is what Live gives back when it goes off.
            if (d.hasProperty("liveRestore"))
                deck.monitoringBeforeLive = static_cast<InputMonitoring>(juce::jlimit(0, 2, static_cast<int>(d.getProperty("liveRestore"))));
        }
    }
    ensureDjAttached();
}
}
