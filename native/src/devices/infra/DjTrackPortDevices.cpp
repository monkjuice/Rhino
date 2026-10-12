#include "infra/DjTrackPortDevices.h"
#include <algorithm>
#include <utility>

// The DJ booth's two hidden plugins: see DjTrackPortDevices.h.

namespace rhino
{
namespace
{
// A block arriving larger than initialise was told is worked in pieces of
// this, so the plugin never allocates on the audio thread.
constexpr int minimumChunk = 16;
}

DjTrackInjectorDevice::DjTrackInjectorDevice(te::PluginCreationInfo info) : te::Plugin(info) {}

void DjTrackInjectorDevice::initialise(const te::PluginInitialisationInfo& info)
{
    sampleRate = info.sampleRate > 0.0 ? info.sampleRate : 48000.0;
    const auto block = static_cast<size_t>(std::max(minimumChunk, info.blockSizeSamples));
    left.assign(block, 0.0f);
    right.assign(block, 0.0f);
    events.assign(static_cast<size_t>(DjTrackPort::maximumEvents), {});
    // Built into one of these and swapped into the chain's array, as Rhino Arp
    // does: both already own room, so nothing is allocated in a block.
    firstOutput.reserve(2048);
    steadyOutput.reserve(2048);
    firstRender = true;
    delivered.fill(false);
    anyDelivered = false;
}

void DjTrackInjectorDevice::releaseDelivered(te::MidiMessageArray& output, double atSeconds)
{
    if (!anyDelivered) return;
    for (int note = 0; note < 128; ++note)
        if (std::exchange(delivered[static_cast<size_t>(note)], false))
            output.addMidiMessage(juce::MidiMessage::noteOff(1, note), atSeconds, te::MidiMessageArray::notMPE);
    anyDelivered = false;
}

void DjTrackInjectorDevice::applyToBuffer(const te::PluginRenderContext& context)
{
    auto* p = port.load(std::memory_order_acquire);
    auto* midi = context.bufferForMidiMessages;
    // The song owns its tracks while its transport rolls and in every render
    // - a merge, an export - so a deck never reaches either.
    const auto playing = p != nullptr && p->bound.load(std::memory_order_acquire) && !context.isPlaying
                      && !context.isRendering;
    if (!playing)
    {
        // Every note it started goes, as it stops playing the deck, before the
        // block reaches the instrument.
        if (anyDelivered && midi != nullptr)
        {
            auto& output = firstRender ? firstOutput : steadyOutput;
            firstRender = false;
            output.clear();
            output.isAllNotesOff = midi->isAllNotesOff;
            for (const auto& message : *midi)
                output.add(message);
            releaseDelivered(output, 0.0);
            midi->swapWith(output);
            midi->sortByTimestamp();
        }
        if (anyDelivered)
        {
            delivered.fill(false);
            anyDelivered = false;
        }
        return;
    }
    if (context.destBuffer == nullptr || context.bufferNumSamples <= 0 || left.empty()) return;

    auto& buffer = *context.destBuffer;
    const auto channels = buffer.getNumChannels();
    const auto total = context.bufferNumSamples;
    const auto chunk = static_cast<int>(left.size());
    auto& output = firstRender ? firstOutput : steadyOutput;
    firstRender = false;
    output.clear();
    if (midi != nullptr)
    {
        // What the chain already carries goes on: the deck's notes join it
        // rather than replace it, so a monitored input still plays.
        output.isAllNotesOff = midi->isAllNotesOff;
        for (const auto& message : *midi)
            output.add(message);
    }

    for (int done = 0; done < total;)
    {
        const auto n = std::min(chunk, total - done);
        int count = 0;
        bool resynced = false;
        p->popFeed(left.data(), right.data(), n, events.data(), static_cast<int>(events.size()), count, resynced);
        if (resynced)
            releaseDelivered(output, static_cast<double>(done) / sampleRate);
        // The deck's sound joins the block: what the track plays now is the
        // deck, and a monitored input with it. A MIDI track's instrument
        // writes over this with its own sound, so on one it is of no
        // consequence; a mono chain takes both sides of the deck as one.
        if (channels >= 2)
        {
            juce::FloatVectorOperations::add(buffer.getWritePointer(0, context.bufferStartSample + done), left.data(), n);
            juce::FloatVectorOperations::add(buffer.getWritePointer(1, context.bufferStartSample + done), right.data(), n);
        }
        else if (channels == 1)
        {
            auto* mono = buffer.getWritePointer(0, context.bufferStartSample + done);
            for (int f = 0; f < n; ++f)
                mono[f] += 0.5f * (left[static_cast<size_t>(f)] + right[static_cast<size_t>(f)]);
        }
        if (midi != nullptr)
            for (int e = 0; e < count; ++e)
            {
                const auto& event = events[static_cast<size_t>(e)];
                if (!juce::isPositiveAndBelow(event.note, 128)) continue;
                const auto seconds = static_cast<double>(done + event.frame) / sampleRate;
                const auto message = event.on
                    ? juce::MidiMessage::noteOn(1, event.note, static_cast<juce::uint8>(juce::jlimit(1, 127, event.velocity)))
                    : juce::MidiMessage::noteOff(1, event.note);
                output.addMidiMessage(message, seconds, te::MidiMessageArray::notMPE);
                delivered[static_cast<size_t>(event.note)] = event.on;
                anyDelivered = anyDelivered || event.on;
            }
        done += n;
    }

    if (midi != nullptr)
    {
        midi->swapWith(output);
        midi->sortByTimestamp();
    }
}

DjTrackTapDevice::DjTrackTapDevice(te::PluginCreationInfo info) : te::Plugin(info) {}

void DjTrackTapDevice::initialise(const te::PluginInitialisationInfo& info)
{
    const auto block = static_cast<size_t>(std::max(minimumChunk, info.blockSizeSamples));
    left.assign(block, 0.0f);
    right.assign(block, 0.0f);
}

void DjTrackTapDevice::applyToBuffer(const te::PluginRenderContext& context)
{
    auto* p = port.load(std::memory_order_acquire);
    if (p == nullptr || !p->bound.load(std::memory_order_acquire)) return;
    if (context.isPlaying || context.isRendering) return;
    if (context.destBuffer == nullptr || context.bufferNumSamples <= 0 || left.empty()) return;

    auto& buffer = *context.destBuffer;
    const auto channels = buffer.getNumChannels();
    const auto total = context.bufferNumSamples;
    const auto chunk = static_cast<int>(left.size());
    for (int done = 0; done < total;)
    {
        const auto n = std::min(chunk, total - done);
        // A mono chain is heard in both ears, as the track's own fader would
        // have spread it; nothing at all is silence.
        const auto* l = channels > 0 ? buffer.getReadPointer(0, context.bufferStartSample + done) : nullptr;
        const auto* r = channels > 1 ? buffer.getReadPointer(1, context.bufferStartSample + done) : l;
        if (l != nullptr)
        {
            std::copy(l, l + n, left.data());
            std::copy(r, r + n, right.data());
        }
        else
        {
            std::fill(left.begin(), left.begin() + n, 0.0f);
            std::fill(right.begin(), right.begin() + n, 0.0f);
        }
        p->pushTap(left.data(), right.data(), n);
        done += n;
    }
    // The deck has the track now; the song's output carries none of it.
    for (int c = 0; c < channels; ++c)
        juce::FloatVectorOperations::clear(buffer.getWritePointer(c, context.bufferStartSample), total);
    if (auto* midi = context.bufferForMidiMessages)
        midi->clear();
}
}
