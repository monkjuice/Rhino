#include "DjEngine.h"
#include <algorithm>
#include <cmath>

// The DJ engine: see DjEngine.h.

namespace rhino
{
int DjEngine::boundaryBeats(Quantise q, int perBar)
{
    const auto bar = std::max(1, perBar);
    switch (q)
    {
        case Quantise::off: return 0;
        case Quantise::beat: return 1;
        case Quantise::bar: return bar;
        case Quantise::fourBars: return bar * 4;
    }
    return 0;
}

DjEngine::DjEngine()
{
    prepare(48000.0, 512);
}

DjEngine::~DjEngine() = default;

void DjEngine::prepare(double sampleRate, int maximumBlockSize)
{
    rate = sampleRate > 1000.0 ? sampleRate : 48000.0;
    maximumBlock = std::max(16, maximumBlockSize);
    for (auto& deck : scratch)
        for (auto& channel : deck)
            channel.assign(static_cast<size_t>(maximumBlock), 0.0f);
    for (auto& channel : masterBus) channel.assign(static_cast<size_t>(maximumBlock), 0.0f);
    for (auto& channel : cueBus) channel.assign(static_cast<size_t>(maximumBlock), 0.0f);
    for (auto& channel : sendBus) channel.assign(static_cast<size_t>(maximumBlock), 0.0f);
    micBuffer.assign(static_cast<size_t>(maximumBlock), 0.0f);
    for (auto& strip : channels) strip.prepare(rate);
    masterSection.prepare(rate);
    micSection.prepare(rate);
    sendEffect.prepare(rate);
    effect.prepare(rate, maximumBlock);
    // Nothing renders while this runs, so everything let go of can go.
    retired.clear();
}

void DjEngine::setDeckCount(int count)
{
    decks.store(std::clamp(count, 0, maximumDecks), std::memory_order_relaxed);
}

void DjEngine::setTrack(int index, std::unique_ptr<DjTrack> track, bool keepBeat)
{
    if (index < 0 || index >= maximumDecks) return;
    auto& d = deckList[static_cast<size_t>(index)];
    d.keepBeatOnSwap.store(keepBeat, std::memory_order_relaxed);
    auto previous = std::move(owned[static_cast<size_t>(index)]);
    owned[static_cast<size_t>(index)] = std::move(track);
    d.track.store(owned[static_cast<size_t>(index)].get(), std::memory_order_release);
    if (previous != nullptr)
        retired.push_back({std::move(previous), begun.load(std::memory_order_acquire)});
}

const DjTrack* DjEngine::trackOf(int index) const noexcept
{
    if (index < 0 || index >= maximumDecks) return nullptr;
    return owned[static_cast<size_t>(index)].get();
}

// A block that had begun when a track was let go of may still be reading
// it; one that begins afterwards loads the new pointer. So a track is free
// once as many blocks have ended as had begun when it was retired.
int DjEngine::collect()
{
    const auto finished = ended.load(std::memory_order_acquire);
    retired.erase(std::remove_if(retired.begin(), retired.end(),
                                 [finished](const Retired& r) { return finished >= r.after; }),
                  retired.end());
    return static_cast<int>(retired.size());
}

bool DjEngine::push(const Command& command) noexcept
{
    const auto t = tail.load(std::memory_order_relaxed);
    const auto next = (t + 1) % queueSize;
    if (next == head.load(std::memory_order_acquire))
        return false;
    queue[static_cast<size_t>(t)] = command;
    tail.store(next, std::memory_order_release);
    return true;
}

int DjEngine::pendingCommands() const noexcept
{
    const auto h = head.load(std::memory_order_acquire), t = tail.load(std::memory_order_acquire);
    return (t - h + queueSize) % queueSize;
}

void DjEngine::applyCommands()
{
    auto h = head.load(std::memory_order_relaxed);
    const auto t = tail.load(std::memory_order_acquire);
    while (h != t)
    {
        apply(queue[static_cast<size_t>(h)]);
        h = (h + 1) % queueSize;
    }
    head.store(h, std::memory_order_release);
}

void DjEngine::apply(const Command& command)
{
    using Type = Command::Type;
    const auto count = deckCount();
    if (command.type == Type::stopAll)
    {
        for (int i = 0; i < count; ++i)
            deckList[static_cast<size_t>(i)].pause();
        return;
    }
    if (command.deck < 0 || command.deck >= count) return;
    auto& d = deckList[static_cast<size_t>(command.deck)];
    // Material swapped in since the last block is taken up first, so a
    // command queued behind the swap addresses the new track.
    d.adoptPending();
    // Cue and loop points snap to the deck's grid while its own Quantize
    // key is lit, as a CDJ's do; the launch quantisation is the booth's.
    const auto snap = d.quantiseSnap.load(std::memory_order_relaxed);
    switch (command.type)
    {
        case Type::play: d.play(); break;
        case Type::pause: d.pause(); break;
        case Type::togglePlay: d.togglePlay(); break;
        case Type::cueDown: d.cueDown(snap); break;
        case Type::cueUp: d.cueUp(); break;
        case Type::hotCue: d.hotCue(command.count, snap); break;
        case Type::clearHotCue: d.clearHotCue(command.count); break;
        case Type::setCue: d.setCuePoint(command.value); break;
        case Type::setHotCue: d.setHotCuePoint(command.count, command.value); break;
        case Type::loopIn: d.loopIn(snap); break;
        case Type::loopOut: d.loopOut(snap); break;
        case Type::beatLoop: d.beatLoop(command.value, snap); break;
        case Type::reloopExit: d.reloopExit(); break;
        case Type::loopHalve: d.loopHalve(); break;
        case Type::loopDouble: d.loopDouble(); break;
        case Type::clearLoop: d.clearLoop(); break;
        case Type::setLoop: d.setLoop(command.value, command.value2, command.flag); break;
        case Type::beatJump: d.beatJump(command.count); break;
        case Type::seek: d.seek(command.value); break;
        case Type::syncAlign:
        {
            const auto m = masterDeck.load(std::memory_order_relaxed);
            if (m >= 0 && m < count && m != command.deck)
            {
                const auto& master = deckList[static_cast<size_t>(m)];
                const auto span = std::max(1, boundaryBeats(Quantise::bar, beatsPerBar.load(std::memory_order_relaxed)));
                const auto beat = master.beatPosition();
                const auto phase = beat - std::floor(beat / span) * span;
                d.alignPhase(phase, span);
            }
            break;
        }
        case Type::setMaster: requestedMaster.store(command.deck, std::memory_order_relaxed); break;
        case Type::stopAll: break;
    }
}

// The master is the deck the others follow: the one asked for while it
// plays, else the one already master while it plays, else the first that
// does. Nothing playing means no master, and the next deck to start takes
// the job.
void DjEngine::chooseMaster()
{
    const auto count = deckCount();
    const auto playing = [this, count](int i)
    {
        return i >= 0 && i < count && deckList[static_cast<size_t>(i)].isPlaying()
            && deckList[static_cast<size_t>(i)].nativeBpm() > 0.0;
    };
    const auto requested = requestedMaster.load(std::memory_order_relaxed);
    auto current = masterDeck.load(std::memory_order_relaxed);
    if (playing(requested))
        current = requested;
    else if (!playing(current))
    {
        current = -1;
        for (int i = 0; i < count; ++i)
            if (playing(i))
            {
                current = i;
                break;
            }
    }
    masterDeck.store(current, std::memory_order_relaxed);
    masterBpm.store(current >= 0 ? std::abs(deckList[static_cast<size_t>(current)].effectiveBpm()) : 0.0,
                    std::memory_order_relaxed);
}

// Each deck's rate for the block: its own fader, or the master's tempo when
// synced, with a gentle correction back onto the master's beat; or the hand
// on the platter, which overrides both while it is there.
void DjEngine::setRates()
{
    const auto count = deckCount();
    const auto m = masterDeck.load(std::memory_order_relaxed);
    const auto lock = phaseLock.load(std::memory_order_relaxed);
    for (int i = 0; i < count; ++i)
    {
        auto& d = deckList[static_cast<size_t>(i)];
        if (d.scratching.load(std::memory_order_relaxed))
        {
            d.targetRate = std::clamp(d.scratchRate.load(std::memory_order_relaxed), -10.0, 10.0);
            continue;
        }
        auto target = d.baseRate();
        if (d.synced.load(std::memory_order_relaxed) && m >= 0 && m != i)
        {
            const auto& master = deckList[static_cast<size_t>(m)];
            const auto own = d.nativeBpm();
            const auto masterTempo = std::abs(master.effectiveBpm());
            if (own > 0.0 && masterTempo > 0.0)
            {
                target = masterTempo / own;
                if (lock && d.isPlaying())
                {
                    // The error in beats, wrapped to the nearest, closed over
                    // about half a second and never by more than two per cent.
                    auto error = master.beatPosition() - d.beatPosition();
                    error -= std::round(error);
                    const auto errorSeconds = error * 60.0 / masterTempo;
                    if (std::abs(errorSeconds) > 0.001)
                        target += std::clamp(errorSeconds / 0.5, -0.02, 0.02);
                }
            }
        }
        d.targetRate = std::max(0.0, target);
    }
}

// A deck waiting for its start, or a playing deck waiting to jump, lands at
// once when nothing is quantised or nothing else is playing, and otherwise
// on the master's next boundary - which may fall inside this block.
void DjEngine::landPendings(int frames, double deviceRate)
{
    const auto count = deckCount();
    const auto q = static_cast<Quantise>(quantise.load(std::memory_order_relaxed));
    const auto span = boundaryBeats(q, beatsPerBar.load(std::memory_order_relaxed));
    const auto m = masterDeck.load(std::memory_order_relaxed);
    for (int i = 0; i < count; ++i)
    {
        auto& d = deckList[static_cast<size_t>(i)];
        if (d.currentPending() == DjDeck::Pending::none) continue;
        if (span <= 0 || m < 0 || m == i)
        {
            d.landPending(0, 0);
            continue;
        }
        const auto& master = deckList[static_cast<size_t>(m)];
        const auto* masterTrack = master.track.load(std::memory_order_acquire);
        if (masterTrack == nullptr || masterTrack->framesPerBeat() <= 0.0)
        {
            d.landPending(0, 0);
            continue;
        }
        const auto masterPosition = master.position.load(std::memory_order_relaxed);
        const auto beat = masterTrack->beatAtFrame(masterPosition);
        // A hair short of a boundary counts as on it.
        auto next = std::ceil((beat - 0.002) / span) * span;
        if (next < beat) next += span;
        const auto boundaryFrame = masterTrack->frameAtBeat(next);
        const auto masterStep = std::max(1.0e-6, std::abs(master.playbackRate.load(std::memory_order_relaxed)))
                              * masterTrack->sampleRate / deviceRate;
        const auto distance = (boundaryFrame - masterPosition) / masterStep;
        if (distance < frames)
        {
            // The landing snaps to this deck's own boundary of the same size,
            // a bar at most, so bars line up with bars.
            const auto snap = std::min(span, boundaryBeats(Quantise::bar, beatsPerBar.load(std::memory_order_relaxed)));
            d.landPending(std::max(0, static_cast<int>(distance)), snap);
        }
    }
}

void DjEngine::process(const float* const* inputs, int inputChannels, float* const* outputs, int outputChannels, int frames)
{
    begun.fetch_add(1, std::memory_order_acq_rel);
    for (int c = 0; c < outputChannels; ++c)
        if (outputs[c] != nullptr)
            std::fill(outputs[c], outputs[c] + frames, 0.0f);
    applyCommands();
    const float* mic = inputs != nullptr && inputChannels > 0 ? inputs[0] : nullptr;
    for (int offset = 0; offset < frames; offset += maximumBlock)
    {
        const auto chunk = std::min(maximumBlock, frames - offset);
        float* chunkOutputs[8] {};
        const auto used = std::min(outputChannels, 8);
        for (int c = 0; c < used; ++c)
            chunkOutputs[c] = outputs[c] != nullptr ? outputs[c] + offset : nullptr;
        renderChunk(mic != nullptr ? mic + offset : nullptr, chunkOutputs, used, chunk);
    }
    ended.fetch_add(1, std::memory_order_acq_rel);
}

void DjEngine::releaseSounding(int index)
{
    auto& seq = sequencers[static_cast<size_t>(index)];
    for (int note = 0; note < 128; ++note)
        if (seq.sounding[static_cast<size_t>(note)])
        {
            seq.sounding[static_cast<size_t>(note)] = false;
            if (liveSink != nullptr)
                liveSink->noteOff(index, note);
        }
}

// The live preview's sequencer: the material's notes go out as the deck
// passes their beats, looped as the deck loops, and every note still held
// is let go when the preview ends, the deck stops, or it lands somewhere it
// was not travelling to. Notes before where the deck lands are never
// replayed, so a jump makes no burst of them.
void DjEngine::sequence(int index, bool active, int frames)
{
    auto& seq = sequencers[static_cast<size_t>(index)];
    auto& d = deckList[static_cast<size_t>(index)];
    const auto* t = d.track.load(std::memory_order_acquire);
    if (!active || liveSink == nullptr || t == nullptr || t->midi.empty() || !d.isPlaying())
    {
        releaseSounding(index);
        seq.wasActive = false;
        return;
    }
    const auto beatNow = d.beatPosition();
    const auto blockBeats = std::abs(d.effectiveBpm()) / 60.0 * static_cast<double>(frames) / rate;
    const auto emit = [this, index, &seq, t](double from, double to)
    {
        auto event = std::upper_bound(t->midi.begin(), t->midi.end(), from,
                                      [](double beat, const DjMidiEvent& e) { return beat < e.beat; });
        for (; event != t->midi.end() && event->beat <= to; ++event)
        {
            if (!juce::isPositiveAndBelow(event->note, 128)) continue;
            auto& held = seq.sounding[static_cast<size_t>(event->note)];
            if (event->on)
            {
                liveSink->noteOn(index, event->note, event->velocity);
                held = true;
            }
            else if (held)
            {
                liveSink->noteOff(index, event->note);
                held = false;
            }
        }
    };
    const auto jumps = d.jumps.load(std::memory_order_relaxed);
    const auto jumped = jumps != seq.lastJumps;
    seq.lastJumps = jumps;
    if (!seq.wasActive || (jumped && beatNow >= seq.lastBeat))
    {
        // A start, or a landing ahead: play from the block the deck began
        // this stretch in, and nothing from before it.
        releaseSounding(index);
        seq.lastBeat = beatNow - blockBeats - 1.0e-9;
        seq.wasActive = true;
    }
    if (beatNow + 1.0e-9 < seq.lastBeat)
    {
        // Backwards. A loop's wrap plays the loop out to its end and starts
        // it again; a landing behind, or a reverse, holds nothing over.
        const auto loopStart = t->beatAtFrame(d.loopStart.load(std::memory_order_relaxed));
        const auto loopEnd = t->beatAtFrame(d.loopEnd.load(std::memory_order_relaxed));
        const auto margin = blockBeats * 2.0 + 1.0e-6;
        const auto wrapped = d.loopActive.load(std::memory_order_relaxed)
                          && seq.lastBeat > loopEnd - margin && beatNow < loopStart + margin;
        if (wrapped)
        {
            emit(seq.lastBeat, loopEnd);
            releaseSounding(index);
            emit(loopStart - 1.0e-9, beatNow);
        }
        else
            releaseSounding(index);
    }
    else
        emit(seq.lastBeat, beatNow);
    seq.lastBeat = beatNow;
}

void DjEngine::renderChunk(const float* micInput, float* const* outputs, int outputChannels, int frames)
{
    const auto count = deckCount();
    chooseMaster();
    setRates();
    landPendings(frames, rate);

    for (auto& channel : masterBus) std::fill(channel.begin(), channel.begin() + frames, 0.0f);
    for (auto& channel : cueBus) std::fill(channel.begin(), channel.begin() + frames, 0.0f);
    for (auto& channel : sendBus) std::fill(channel.begin(), channel.begin() + frames, 0.0f);
    float gainA = 1.0f, gainB = 1.0f;
    fader.gains(gainA, gainB);
    const auto fxTarget = effect.target.load(std::memory_order_relaxed);
    const auto m = masterDeck.load(std::memory_order_relaxed);
    const auto tempo = masterBpm.load(std::memory_order_relaxed);
    const auto fallback = std::clamp(tapBpm.load(std::memory_order_relaxed), 40.0, 240.0);
    const auto beatSeconds = tempo > 0.0 ? 60.0 / tempo : 60.0 / fallback;
    // The effect's beat count follows the master while one plays and keeps
    // walking at the tapped tempo when none does.
    if (m >= 0)
        effectBeat = deckList[static_cast<size_t>(m)].beatPosition();
    else
        effectBeat += frames / (beatSeconds * rate);

    for (int i = 0; i < count; ++i)
    {
        auto& d = deckList[static_cast<size_t>(i)];
        auto& strip = channels[static_cast<size_t>(i)];
        float* left = scratch[static_cast<size_t>(i)][0].data();
        float* right = scratch[static_cast<size_t>(i)][1].data();
        d.render(left, right, frames, rate);
        // The live preview: the bounce is silenced and the material's notes
        // go out to the track's own instrument as the deck passes them.
        const auto previewing = d.livePreview.load(std::memory_order_relaxed);
        if (previewing)
        {
            std::fill(left, left + frames, 0.0f);
            std::fill(right, right + frames, 0.0f);
        }
        sequence(i, previewing, frames);
        strip.processPreFader(left, right, frames);
        if (strip.cue.load(std::memory_order_relaxed))
            for (int f = 0; f < frames; ++f)
            {
                cueBus[0][static_cast<size_t>(f)] += left[f];
                cueBus[1][static_cast<size_t>(f)] += right[f];
            }
        strip.applyFader(left, right, frames);
        const auto sendGain = strip.sendGain();
        if (sendGain > 0.0001f)
            for (int f = 0; f < frames; ++f)
            {
                sendBus[0][static_cast<size_t>(f)] += left[f] * sendGain;
                sendBus[1][static_cast<size_t>(f)] += right[f] * sendGain;
            }
        if (fxTarget == i && strip.fxOn.load(std::memory_order_relaxed))
            effect.process(left, right, frames, beatSeconds, effectBeat);
        const auto side = static_cast<DjChannelStrip::CrossfaderSide>(strip.crossfaderSide.load(std::memory_order_relaxed));
        const auto gain = side == DjChannelStrip::CrossfaderSide::a ? gainA
                        : side == DjChannelStrip::CrossfaderSide::b ? gainB : 1.0f;
        for (int f = 0; f < frames; ++f)
        {
            masterBus[0][static_cast<size_t>(f)] += left[f] * gain;
            masterBus[1][static_cast<size_t>(f)] += right[f] * gain;
        }
    }
    // The send/return comes back onto the master at its own level.
    sendEffect.process(sendBus[0].data(), sendBus[1].data(), frames);
    for (int f = 0; f < frames; ++f)
    {
        masterBus[0][static_cast<size_t>(f)] += sendBus[0][static_cast<size_t>(f)];
        masterBus[1][static_cast<size_t>(f)] += sendBus[1][static_cast<size_t>(f)];
    }
    if (fxTarget < 0)
        effect.process(masterBus[0].data(), masterBus[1].data(), frames, beatSeconds, effectBeat);
    masterSection.process(masterBus[0].data(), masterBus[1].data(), frames);
    // The mic joins after the master's own processing, ducking it while
    // spoken into with talkover on.
    float duck = 1.0f;
    micSection.process(micInput, micBuffer.data(), frames, duck);
    if (duck < 0.999f || micSection.mode.load(std::memory_order_relaxed) != static_cast<int>(DjMicSection::Mode::off))
        for (int f = 0; f < frames; ++f)
        {
            const auto voice = micBuffer[static_cast<size_t>(f)];
            masterBus[0][static_cast<size_t>(f)] = softClip(masterBus[0][static_cast<size_t>(f)] * duck + voice);
            masterBus[1][static_cast<size_t>(f)] = softClip(masterBus[1][static_cast<size_t>(f)] * duck + voice);
        }

    if (outputChannels > 0 && outputs[0] != nullptr)
        std::copy(masterBus[0].begin(), masterBus[0].begin() + frames, outputs[0]);
    if (outputChannels > 1 && outputs[1] != nullptr)
        std::copy(masterBus[1].begin(), masterBus[1].begin() + frames, outputs[1]);
    if (outputChannels > 3 && outputs[2] != nullptr && outputs[3] != nullptr)
    {
        // The headphones: the cue bus against the master, at their own
        // level, or with mono split the cue on the left and the master on
        // the right.
        const auto mix = std::clamp(masterSection.cueMix.load(std::memory_order_relaxed), 0.0f, 1.0f);
        const auto level = decibelsToGain(std::clamp(masterSection.cueLevelDb.load(std::memory_order_relaxed),
                                                     DjMasterSection::minimumLevelDb, DjMasterSection::maximumLevelDb));
        const auto split = masterSection.monoSplit.load(std::memory_order_relaxed);
        for (int f = 0; f < frames; ++f)
        {
            const auto cueL = cueBus[0][static_cast<size_t>(f)], cueR = cueBus[1][static_cast<size_t>(f)];
            const auto mainL = masterBus[0][static_cast<size_t>(f)], mainR = masterBus[1][static_cast<size_t>(f)];
            if (split)
            {
                outputs[2][f] = softClip(0.5f * (cueL + cueR) * level);
                outputs[3][f] = softClip(0.5f * (mainL + mainR) * level);
            }
            else
            {
                outputs[2][f] = softClip((cueL * (1.0f - mix) + mainL * mix) * level);
                outputs[3][f] = softClip((cueR * (1.0f - mix) + mainR * mix) * level);
            }
        }
    }
}
}
