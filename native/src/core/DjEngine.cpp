#include "DjEngine.h"
#include <algorithm>
#include <cmath>

// The DJ engine's housekeeping and its command protocol: preparation, the
// slots, the hand-over of material and its return, the queue. The clock -
// who is master, sync and the quantised landing - is in DjEngineClock.cpp,
// and the block itself in DjEngineMix.cpp. See DjEngine.h.

namespace rhino
{
namespace
{
void deleteMaterial(const void* material)
{
    delete static_cast<const DjTrack*>(material);
}
}

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

DjEngine::Command DjEngine::Command::make(Type type, int slot) noexcept
{
    Command c;
    c.type = type;
    c.slot = slot;
    return c;
}

DjEngine::Command DjEngine::Command::hotCue(int slot, int index) noexcept
{
    auto c = make(Type::hotCue, slot);
    c.index = index;
    return c;
}

DjEngine::Command DjEngine::Command::clearHotCue(int slot, int index) noexcept
{
    auto c = make(Type::clearHotCue, slot);
    c.index = index;
    return c;
}

DjEngine::Command DjEngine::Command::setCue(int slot, double frame) noexcept
{
    auto c = make(Type::setCue, slot);
    c.frame = frame;
    return c;
}

DjEngine::Command DjEngine::Command::setHotCue(int slot, int index, double frame) noexcept
{
    auto c = make(Type::setHotCue, slot);
    c.index = index;
    c.frame = frame;
    return c;
}

DjEngine::Command DjEngine::Command::beatLoop(int slot, double beats) noexcept
{
    auto c = make(Type::beatLoop, slot);
    c.beats = beats;
    return c;
}

DjEngine::Command DjEngine::Command::setLoop(int slot, double startFrame, double endFrame, bool active) noexcept
{
    auto c = make(Type::setLoop, slot);
    c.frame = startFrame;
    c.endFrame = endFrame;
    c.on = active;
    return c;
}

DjEngine::Command DjEngine::Command::beatJump(int slot, int beats) noexcept
{
    auto c = make(Type::beatJump, slot);
    c.index = beats;
    return c;
}

DjEngine::Command DjEngine::Command::seek(int slot, double frame) noexcept
{
    auto c = make(Type::seek, slot);
    c.frame = frame;
    return c;
}

DjEngine::Command DjEngine::Command::setSource(int slot, DjDeckSource source) noexcept
{
    auto c = make(Type::setSource, slot);
    c.source = source;
    return c;
}

DjEngine::Command DjEngine::Command::setSlotActive(int slot, bool active) noexcept
{
    auto c = make(Type::setSlotActive, slot);
    c.on = active;
    return c;
}

DjEngine::DjEngine()
{
    for (auto& d : deckList)
        d.releases = &released;
    prepare(48000.0, 512);
}

// What is still owned goes with the engine: nothing renders by now.
DjEngine::~DjEngine() = default;

void DjEngine::prepare(double sampleRate, int maximumBlockSize)
{
    rate = sampleRate > 1000.0 ? sampleRate : 48000.0;
    maximumBlock = std::max(16, maximumBlockSize);
    for (auto& d : deckList)
        d.prepare(maximumBlock, rate);
    for (auto& deck : scratch)
        for (auto& channel : deck)
            channel.assign(static_cast<size_t>(maximumBlock), 0.0f);
    for (auto& channel : masterBus) channel.assign(static_cast<size_t>(maximumBlock), 0.0f);
    for (auto& channel : cueBus) channel.assign(static_cast<size_t>(maximumBlock), 0.0f);
    for (auto& channel : sendBus) channel.assign(static_cast<size_t>(maximumBlock), 0.0f);
    micBuffer.assign(static_cast<size_t>(maximumBlock), 0.0f);
    for (auto& strip : channels) strip.prepare(rate);
    for (auto& port : ports) port.prepare(maximumBlock);
    for (auto& delay : delays) delay.prepare(maximumBlock);
    for (auto& count : blockEventCount) count = 0;
    masterSection.prepare(rate);
    micSection.prepare(rate);
    sendEffect.prepare(rate);
    effect.prepare(rate, maximumBlock);
    // Nothing a deck plays or fades from is let go of here: a deck that was
    // playing when the device stopped still holds it, and material handed
    // over meanwhile waits in the queue for the first block after.
    preparedRate.store(rate, std::memory_order_release);
    preparedBlock.store(maximumBlock, std::memory_order_release);
    preparedGeneration.fetch_add(1, std::memory_order_acq_rel);
}

bool DjEngine::setSlotActive(int slot, bool on)
{
    if (!juce::isPositiveAndBelow(slot, maximumDecks)) return false;
    if (!push(Command::setSlotActive(slot, on))) return false;
    active[static_cast<size_t>(slot)] = on;
    // The slot is emptied either way, so its material is the engine's to
    // hand back now; the registry lets go of it in the same call.
    handedOver[static_cast<size_t>(slot)] = nullptr;
    return true;
}

bool DjEngine::slotActive(int slot) const noexcept
{
    return juce::isPositiveAndBelow(slot, maximumDecks) && active[static_cast<size_t>(slot)];
}

bool DjEngine::setDeckSource(int slot, std::unique_ptr<DjTrack>&& material, bool keepBeat)
{
    if (!juce::isPositiveAndBelow(slot, maximumDecks)) return false;
    const auto* raw = material.get();
    // Room for the record first, so that once the command is queued nothing
    // can fail; refused, the material stays with the caller untouched.
    if (owned.size() == owned.capacity())
        owned.reserve(std::max<size_t>(8, owned.capacity() * 2));
    if (!push(Command::setSource(slot, {raw, keepBeat})))
        return false;
    if (raw != nullptr)
        owned.push_back({DjObjectKind::material, {material.release(), &deleteMaterial}});
    handedOver[static_cast<size_t>(slot)] = raw;
    return true;
}

const DjTrack* DjEngine::trackOf(int slot) const noexcept
{
    if (!juce::isPositiveAndBelow(slot, maximumDecks)) return nullptr;
    return handedOver[static_cast<size_t>(slot)];
}

int DjEngine::collect()
{
    DjReleaseQueue::Entry entry;
    while (released.take(entry))
    {
        const auto found = std::find_if(owned.begin(), owned.end(),
                                        [&entry](const Owned& o) { return o.object.get() == entry.object; });
        jassert(found != owned.end());
        if (found != owned.end())
            owned.erase(found);
    }
    if (const auto lost = released.lost(); lost != lostReported)
    {
        juce::Logger::writeToLog("Rhino: the DJ booth had no room to hand back " + juce::String(lost - lostReported)
                                 + " objects; they are kept until the booth goes");
        lostReported = lost;
    }
    return static_cast<int>(std::count_if(owned.begin(), owned.end(), [this](const Owned& o)
    {
        return std::find(handedOver.begin(), handedOver.end(), o.object.get()) == handedOver.end();
    }));
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

void DjEngine::resetChannelControls(int slot)
{
    if (!juce::isPositiveAndBelow(slot, maximumDecks)) return;
    auto& strip = channels[static_cast<size_t>(slot)];
    constexpr auto order = std::memory_order_relaxed;
    const DjChannelStrip fresh;
    strip.trimDb.store(fresh.trimDb.load(order), order);
    strip.comp.store(fresh.comp.load(order), order);
    for (size_t band = 0; band < DjChannelStrip::bandCount; ++band)
        strip.eqDb[band].store(fresh.eqDb[band].load(order), order);
    strip.filter.store(fresh.filter.load(order), order);
    strip.fader.store(fresh.fader.load(order), order);
    strip.send.store(fresh.send.load(order), order);
    strip.cue.store(fresh.cue.load(order), order);
    strip.fxOn.store(fresh.fxOn.load(order), order);
    strip.crossfaderSide.store(fresh.crossfaderSide.load(order), order);
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
    if (command.type == Type::stopAll)
    {
        for (int i = 0; i < maximumDecks; ++i)
            if (rendering[static_cast<size_t>(i)])
            {
                deckList[static_cast<size_t>(i)].pause();
                deckList[static_cast<size_t>(i)].publish();
            }
        return;
    }
    if (!juce::isPositiveAndBelow(command.slot, maximumDecks)) return;
    auto& d = deckList[static_cast<size_t>(command.slot)];
    if (command.type == Type::setSlotActive)
    {
        setSlot(command.slot, command.on);
        return;
    }
    if (command.type == Type::setSource)
    {
        d.setSource(command.source.material, command.source.keepBeat);
        return;
    }
    // A slot no deck holds takes no transport.
    if (!rendering[static_cast<size_t>(command.slot)]) return;
    // Cue and loop points snap to the deck's grid while its own Quantize key
    // is lit, as a CDJ's do; the launch quantisation is the booth's.
    const auto snap = d.controls.quantiseSnap.load(std::memory_order_relaxed);
    switch (command.type)
    {
        case Type::play: d.play(); break;
        case Type::pause: d.pause(); break;
        case Type::togglePlay: d.togglePlay(); break;
        case Type::cueDown: d.cueDown(snap); break;
        case Type::cueUp: d.cueUp(); break;
        case Type::hotCue: d.hotCue(command.index, snap); break;
        case Type::clearHotCue: d.clearHotCue(command.index); break;
        case Type::setCue: d.setCuePoint(command.frame); break;
        case Type::setHotCue: d.setHotCuePoint(command.index, command.frame); break;
        case Type::loopIn: d.loopIn(snap); break;
        case Type::loopOut: d.loopOut(snap); break;
        case Type::beatLoop: d.beatLoop(command.beats, snap); break;
        case Type::reloopExit: d.reloopExit(); break;
        case Type::loopHalve: d.loopHalve(); break;
        case Type::loopDouble: d.loopDouble(); break;
        case Type::clearLoop: d.clearLoop(); break;
        case Type::setLoop: d.setLoop(command.frame, command.endFrame, command.on); break;
        case Type::beatJump: d.beatJump(command.index); break;
        case Type::seek: d.seek(command.frame); break;
        case Type::syncAlign: alignToMaster(command.slot); break;
        case Type::setMaster: requestedMaster.store(command.slot, std::memory_order_relaxed); break;
        case Type::tempoMoved:
            d.heldTempo = 0.0;
            d.heldTempoRate.store(0.0, std::memory_order_relaxed);
            break;
        case Type::stopAll:
        case Type::setSource:
        case Type::setSlotActive:
            break;
    }
    d.publish();
}

// A slot taken off the console is emptied at once, with no fade: what it
// played and faded from goes back, and nothing of the deck that held it -
// its sequencer's notes, the master's role - stays behind for the next deck
// to find. A slot taken up is emptied the same way, so a deck
// starts there as a new one does even when the slot's letting go never
// reached the queue (Session::djReset with the queue full).
void DjEngine::setSlot(int slot, bool on)
{
    const auto index = static_cast<size_t>(slot);
    deckList[index].reset();
    // Its sequencer's notes go with it. The track it played through lets
    // them go itself: its hidden input releases every note it played once
    // its port is let go of.
    sequencers[index] = {};
    blockEventCount[index] = 0;
    delays[index].reset();
    wentThroughPort[index] = false;
    if (requestedMaster.load(std::memory_order_relaxed) == slot)
        requestedMaster.store(-1, std::memory_order_relaxed);
    if (masterDeck.load(std::memory_order_relaxed) == slot)
        masterDeck.store(-1, std::memory_order_relaxed);
    // A strip taken up starts from silence, at the controls it was given.
    channels[index].reset();
    rendering[index] = on;
}
}
