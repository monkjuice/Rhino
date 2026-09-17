#include "RhinoArpDevice.h"

namespace rhino
{
RhinoArpDevice::RhinoArpDevice(te::PluginCreationInfo info) : Plugin(info)
{
    rateIndex.referTo(state, "rate", getUndoManager(), 1.0f);
    octaves.referTo(state, "octaves", getUndoManager(), 2.0f);
    gatePercent.referTo(state, "gate", getUndoManager(), 45.0f);
    rateParam = addParam("rate", "Rate", {0.0f, 2.0f, 1.0f},
                         [](float value)
                         {
                             const auto index = juce::roundToInt(value);
                             return index == 0 ? "1/8" : index == 2 ? "1/32" : "1/16";
                         },
                         [](const juce::String& text)
                         {
                             if (text.contains("32")) return 2.0f;
                             if (text.contains("8")) return 0.0f;
                             return 1.0f;
                         });
    octavesParam = addParam("octaves", "Octaves", {1.0f, 3.0f, 1.0f});
    gateParam = addParam("gate", "Gate", {10.0f, 90.0f});
    rateParam->attachToCurrentValue(rateIndex);
    octavesParam->attachToCurrentValue(octaves);
    gateParam->attachToCurrentValue(gatePercent);
}

RhinoArpDevice::~RhinoArpDevice()
{
    notifyListenersOfDeletion();
    rateParam->detachFromCurrentValue();
    octavesParam->detachFromCurrentValue();
    gateParam->detachFromCurrentValue();
}

void RhinoArpDevice::initialise(const te::PluginInitialisationInfo& info)
{
    sampleRate = info.sampleRate > 0.0 ? info.sampleRate : 48000.0;
    blockSizeSamples = info.blockSizeSamples;
    reset();
}

void RhinoArpDevice::reset()
{
    for (auto& note : heldNotes)
        note.active = false;
    for (auto& pending : pendingOffs)
        pending.active = false;
    nextTickSeconds = 0.0;
    arpStep = 0;
}

void RhinoArpDevice::midiPanic()
{
    reset();
}

double RhinoArpDevice::stepSeconds() const
{
    const auto bpm = edit.tempoSequence.getTempoAt(tracktion::core::TimePosition::fromSeconds(nextTickSeconds)).getBpm();
    const auto beatSeconds = 60.0 / std::clamp(bpm, 40.0, 240.0);
    switch (juce::roundToInt(rateParam->getCurrentValue()))
    {
        case 0: return beatSeconds * 0.5;
        case 2: return beatSeconds * 0.125;
        default: return beatSeconds * 0.25;
    }
}

int RhinoArpDevice::activeNoteCount() const
{
    int count = 0;
    for (const auto& note : heldNotes)
        if (note.active)
            ++count;
    return count;
}

int RhinoArpDevice::noteAtOrdinal(int ordinal) const
{
    for (int pitch = 0; pitch < static_cast<int>(heldNotes.size()); ++pitch)
        if (heldNotes[static_cast<size_t>(pitch)].active && ordinal-- == 0)
            return pitch;
    return -1;
}

void RhinoArpDevice::addPendingOff(double time, int pitch, int channel, te::MPESourceID source)
{
    for (auto& pending : pendingOffs)
        if (!pending.active)
        {
            pending = {true, time, pitch, channel, source};
            return;
        }
}

void RhinoArpDevice::applyToBuffer(const te::PluginRenderContext& context)
{
    auto* midi = context.bufferForMidiMessages;
    if (midi == nullptr)
        return;

    SCOPED_REALTIME_CHECK
    const auto blockStart = context.editTime.getStart().inSeconds();
    const auto blockEnd = context.editTime.getEnd().inSeconds();
    if (!context.isPlaying || blockStart < nextTickSeconds - 1.0 || blockStart > nextTickSeconds + 1.0)
        nextTickSeconds = blockStart;

    te::MidiMessageArray processed;
    processed.reserve(midi->size() + 128);
    processed.isAllNotesOff = midi->isAllNotesOff;
    if (midi->isAllNotesOff)
        reset();

    for (const auto& message : *midi)
    {
        const auto pitch = message.getNoteNumber();
        if (message.isNoteOn() && juce::isPositiveAndBelow(pitch, static_cast<int>(heldNotes.size())))
        {
            heldNotes[static_cast<size_t>(pitch)] = {true, message.getChannel(), message.getFloatVelocity(), message.mpeSourceID};
            continue;
        }
        if (message.isNoteOff() && juce::isPositiveAndBelow(pitch, static_cast<int>(heldNotes.size())))
        {
            heldNotes[static_cast<size_t>(pitch)].active = false;
            continue;
        }
        processed.add(message);
    }

    const auto flushPendingOffs = [&]
    {
        for (auto& pending : pendingOffs)
            if (pending.active && pending.time < blockEnd)
            {
                processed.addMidiMessage(juce::MidiMessage::noteOff(pending.channel, pending.pitch),
                                         std::max(0.0, pending.time - blockStart), pending.source);
                pending.active = false;
            }
    };

    flushPendingOffs();

    const auto count = activeNoteCount();
    const auto octaveCount = juce::jlimit(1, 3, juce::roundToInt(octavesParam->getCurrentValue()));
    const auto stepLength = stepSeconds();
    const auto gateLength = stepLength * gateParam->getCurrentValue() / 100.0;
    while (count > 0 && nextTickSeconds < blockEnd)
    {
        if (nextTickSeconds >= blockStart)
        {
            const auto ordinal = arpStep % count;
            const auto octave = (arpStep / count) % octaveCount;
            const auto sourcePitch = noteAtOrdinal(ordinal);
            const auto pitch = juce::jlimit(0, 127, sourcePitch + octave * 12);
            const auto& source = heldNotes[static_cast<size_t>(sourcePitch)];
            const auto relative = nextTickSeconds - blockStart;
            processed.addMidiMessage(juce::MidiMessage::noteOn(source.channel, pitch, source.velocity), relative, source.source);
            const auto offTime = nextTickSeconds + gateLength;
            if (offTime < blockEnd)
                processed.addMidiMessage(juce::MidiMessage::noteOff(source.channel, pitch), offTime - blockStart, source.source);
            else
                addPendingOff(offTime, pitch, source.channel, source.source);
        }
        nextTickSeconds += stepLength;
        ++arpStep;
    }

    midi->swapWith(processed);
    midi->sortByTimestamp();
}

void RhinoArpDevice::restorePluginStateFromValueTree(const juce::ValueTree& source)
{
    te::copyPropertiesToCachedValues(source, rateIndex, octaves, gatePercent);
    for (auto* parameter : getAutomatableParameters())
        parameter->updateFromAttachedValue();
}
}
