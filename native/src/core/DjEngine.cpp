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
    for (auto& strip : channels) strip.prepare(rate);
    masterSection.prepare(rate);
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
    const auto snap = quantised();
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
    masterBpm.store(current >= 0 ? deckList[static_cast<size_t>(current)].effectiveBpm() : 0.0,
                    std::memory_order_relaxed);
}

// Each deck's rate for the block: its own fader, or the master's tempo when
// synced, with a gentle correction back onto the master's beat.
void DjEngine::setRates()
{
    const auto count = deckCount();
    const auto m = masterDeck.load(std::memory_order_relaxed);
    const auto lock = phaseLock.load(std::memory_order_relaxed);
    for (int i = 0; i < count; ++i)
    {
        auto& d = deckList[static_cast<size_t>(i)];
        auto target = d.baseRate();
        if (d.synced.load(std::memory_order_relaxed) && m >= 0 && m != i)
        {
            const auto& master = deckList[static_cast<size_t>(m)];
            const auto own = d.nativeBpm();
            const auto masterTempo = master.effectiveBpm();
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
        const auto masterStep = std::max(1.0e-6, master.playbackRate.load(std::memory_order_relaxed))
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

void DjEngine::process(float* const* outputs, int outputChannels, int frames)
{
    begun.fetch_add(1, std::memory_order_acq_rel);
    for (int c = 0; c < outputChannels; ++c)
        if (outputs[c] != nullptr)
            std::fill(outputs[c], outputs[c] + frames, 0.0f);
    applyCommands();
    for (int offset = 0; offset < frames; offset += maximumBlock)
    {
        const auto chunk = std::min(maximumBlock, frames - offset);
        float* chunkOutputs[8] {};
        const auto used = std::min(outputChannels, 8);
        for (int c = 0; c < used; ++c)
            chunkOutputs[c] = outputs[c] != nullptr ? outputs[c] + offset : nullptr;
        renderChunk(chunkOutputs, used, chunk);
    }
    ended.fetch_add(1, std::memory_order_acq_rel);
}

void DjEngine::renderChunk(float* const* outputs, int outputChannels, int frames)
{
    const auto count = deckCount();
    chooseMaster();
    setRates();
    landPendings(frames, rate);

    for (auto& channel : masterBus) std::fill(channel.begin(), channel.begin() + frames, 0.0f);
    for (auto& channel : cueBus) std::fill(channel.begin(), channel.begin() + frames, 0.0f);
    float gainA = 1.0f, gainB = 1.0f;
    fader.gains(gainA, gainB);
    const auto fxTarget = effect.target.load(std::memory_order_relaxed);
    const auto m = masterDeck.load(std::memory_order_relaxed);
    const auto tempo = masterBpm.load(std::memory_order_relaxed);
    const auto beatSeconds = tempo > 0.0 ? 60.0 / tempo : 60.0 / 128.0;
    // The effect's beat count follows the master while one plays and keeps
    // walking at the last tempo when none does.
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
        strip.processPreFader(left, right, frames);
        if (strip.cue.load(std::memory_order_relaxed))
            for (int f = 0; f < frames; ++f)
            {
                cueBus[0][static_cast<size_t>(f)] += left[f];
                cueBus[1][static_cast<size_t>(f)] += right[f];
            }
        strip.applyFader(left, right, frames);
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
    // Decks past the count stay silent but still read their track so a deck
    // taken away and brought back is where it was left.
    if (fxTarget < 0)
        effect.process(masterBus[0].data(), masterBus[1].data(), frames, beatSeconds, effectBeat);
    masterSection.process(masterBus[0].data(), masterBus[1].data(), frames);

    if (outputChannels > 0 && outputs[0] != nullptr)
        std::copy(masterBus[0].begin(), masterBus[0].begin() + frames, outputs[0]);
    if (outputChannels > 1 && outputs[1] != nullptr)
        std::copy(masterBus[1].begin(), masterBus[1].begin() + frames, outputs[1]);
    if (outputChannels > 3 && outputs[2] != nullptr && outputs[3] != nullptr)
    {
        const auto mix = std::clamp(masterSection.cueMix.load(std::memory_order_relaxed), 0.0f, 1.0f);
        for (int f = 0; f < frames; ++f)
        {
            outputs[2][f] = softClip(cueBus[0][static_cast<size_t>(f)] * (1.0f - mix) + masterBus[0][static_cast<size_t>(f)] * mix);
            outputs[3][f] = softClip(cueBus[1][static_cast<size_t>(f)] * (1.0f - mix) + masterBus[1][static_cast<size_t>(f)] * mix);
        }
    }
}
}
