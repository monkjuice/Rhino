#include "DjEngine.h"
#include <algorithm>
#include <cmath>

// The booth's block: the commands for every slot, then each deck planned and
// read, sent through its track's port when it plays a track of the song or
// delayed a block when it does not, and on into its strip, the cue, send,
// effect, master and headphones, and what the block let go of handed back
// last. The sequencer that turns a deck's notes into the port's events is
// here too. See DjEngine.h.

namespace rhino
{
void DjEngine::process(const float* const* inputs, int inputChannels, float* const* outputs, int outputChannels, int frames)
{
    for (int c = 0; c < outputChannels; ++c)
        if (outputs[c] != nullptr)
            std::fill(outputs[c], outputs[c] + frames, 0.0f);
    // Every command for every slot first, material included, so the block
    // sees one consistent material and position before anything is decided.
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
    // What the block let go of goes back now that its last read is done.
    released.flush();
}

void DjEngine::addEvent(int slot, int frame, int note, int velocity, bool on) noexcept
{
    auto& count = blockEventCount[static_cast<size_t>(slot)];
    if (count >= maximumBlockEvents) return;
    blockEvents[static_cast<size_t>(slot)][static_cast<size_t>(count++)] = {frame, note, velocity, on};
}

void DjEngine::releaseSounding(int slot, int frame) noexcept
{
    auto& seq = sequencers[static_cast<size_t>(slot)];
    for (int note = 0; note < 128; ++note)
        if (seq.sounding[static_cast<size_t>(note)])
        {
            seq.sounding[static_cast<size_t>(note)] = false;
            addEvent(slot, frame, note, 0, false);
        }
}

// The sequencer: the material's notes go out as the deck passes their beats,
// each at the frame of the block it falls on, looped as the deck loops, and
// every note still held is let go when the deck stops, when it lands
// somewhere it was not travelling to, and when its material changes - a note
// the new material no longer ends would otherwise sound for good. Notes
// before where the deck lands are never replayed, so a jump makes no burst
// of them.
void DjEngine::sequence(int slot, int frames)
{
    auto& seq = sequencers[static_cast<size_t>(slot)];
    auto& d = deckList[static_cast<size_t>(slot)];
    const auto* t = d.current;
    if (t != seq.material)
    {
        releaseSounding(slot, 0);
        seq.material = t;
        seq.wasActive = false;
    }
    if (t == nullptr || t->midi.empty() || !d.isPlaying())
    {
        releaseSounding(slot, 0);
        seq.wasActive = false;
        return;
    }
    const auto beatNow = d.beatPosition();
    const auto blockBeats = std::abs(d.effectiveBpm()) / 60.0 * static_cast<double>(frames) / rate;
    // Where in the block a beat between `from` and `to` falls, when the
    // block runs from the one to the other.
    const auto frameOf = [frames](double beat, double from, double to, int firstFrame, int lastFrame)
    {
        const auto span = to - from;
        const auto share = span > 1.0e-12 ? (beat - from) / span : 0.0;
        return std::clamp(firstFrame + static_cast<int>(std::floor(share * (lastFrame - firstFrame + 1))), 0, std::max(0, frames - 1));
    };
    const auto emit = [this, slot, &seq, t, &frameOf](double from, double to, int firstFrame, int lastFrame)
    {
        auto event = std::upper_bound(t->midi.begin(), t->midi.end(), from,
                                      [](double beat, const DjMidiEvent& e) { return beat < e.beat; });
        for (; event != t->midi.end() && event->beat <= to; ++event)
        {
            if (!juce::isPositiveAndBelow(event->note, 128)) continue;
            auto& held = seq.sounding[static_cast<size_t>(event->note)];
            const auto frame = frameOf(event->beat, from, to, firstFrame, lastFrame);
            if (event->on)
            {
                // A note struck again while it sounds ends first, so the
                // instrument hears a new note rather than a doubled one.
                if (held)
                    addEvent(slot, frame, event->note, 0, false);
                addEvent(slot, frame, event->note, event->velocity, true);
                held = true;
            }
            else if (held)
            {
                addEvent(slot, frame, event->note, 0, false);
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
        releaseSounding(slot, 0);
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
            // The end of the loop takes the block's first frames, the start
            // of it the rest, in the proportion of the beats each covers.
            const auto before = std::max(0.0, loopEnd - seq.lastBeat);
            const auto after = std::max(0.0, beatNow - loopStart);
            const auto total = before + after;
            const auto split = total > 1.0e-12 ? std::clamp(static_cast<int>(std::round(before / total * frames)), 0, frames) : 0;
            emit(seq.lastBeat, loopEnd, 0, std::max(0, split - 1));
            releaseSounding(slot, std::max(0, split - 1));
            emit(loopStart - 1.0e-9, beatNow, std::min(split, frames - 1), frames - 1);
        }
        else
            releaseSounding(slot, 0);
    }
    else
        emit(seq.lastBeat, beatNow, 0, frames - 1);
    seq.lastBeat = beatNow;
}

void DjEngine::renderChunk(const float* micInput, float* const* outputs, int outputChannels, int frames)
{
    chooseMaster();
    setRates();
    landPendings(frames);

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

    for (int slot = 0; slot < maximumDecks; ++slot)
    {
        if (!rendering[static_cast<size_t>(slot)]) continue;
        auto& d = deckList[static_cast<size_t>(slot)];
        auto& strip = channels[static_cast<size_t>(slot)];
        float* left = scratch[static_cast<size_t>(slot)][0].data();
        float* right = scratch[static_cast<size_t>(slot)][1].data();
        d.plan(frames, rate);
        d.read(left, right, frames);
        blockEventCount[static_cast<size_t>(slot)] = 0;
        sequence(slot, frames);
        auto& port = ports[static_cast<size_t>(slot)];
        const auto throughPort = port.bound.load(std::memory_order_acquire);
        if (throughPort != wentThroughPort[static_cast<size_t>(slot)])
        {
            delays[static_cast<size_t>(slot)].reset();
            wentThroughPort[static_cast<size_t>(slot)] = throughPort;
        }
        if (throughPort)
        {
            // A track of the song: the deck's audio and notes go into the
            // track's chain, and what comes back from it is what the strip
            // hears - a block later, which is the round trip through the
            // song's graph.
            port.pushFeed(left, right, frames, blockEvents[static_cast<size_t>(slot)].data(),
                          blockEventCount[static_cast<size_t>(slot)]);
            port.popTap(left, right, frames);
        }
        else
        {
            // A file: delayed by the same block, so it lines up with a track
            // deck beside it.
            delays[static_cast<size_t>(slot)].process(left, right, frames);
        }
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
        if (fxTarget == slot && strip.fxOn.load(std::memory_order_relaxed))
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
