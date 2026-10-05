#include "sdk/NativeDevice.h"
#include <algorithm>
#include <cmath>

namespace rhino
{
namespace
{
juce::NormalisableRange<float> rangeFor(const ParamSpec& spec)
{
    juce::NormalisableRange<float> range(spec.minimum, spec.maximum, spec.interval);
    if (spec.skewCentre > spec.minimum && spec.skewCentre < spec.maximum)
        range.setSkewForCentre(spec.skewCentre);
    return range;
}

// The engine parameter for a declared control. It answers the engine's
// questions -- is it discrete, what are its states and labels, what is its
// default -- from the declaration, so anything that only knows about
// te::AutomatableParameter still sees a chooser as a chooser.
//
// It keeps its own copy of the declaration. An engine parameter is reference
// counted, and an automation lane or the engine itself can still hold one
// after the device that declared it is gone.
class DeclaredParameter final : public te::AutomatableParameter
{
public:
    DeclaredParameter(const ParamSpec& declared, te::AutomatableEditItem& owner)
        : AutomatableParameter(declared.id, declared.name, owner, rangeFor(declared)), spec(declared)
    {
        valueToStringFunction = [this] (float value) { return spec.text(value); };
        stringToValueFunction = [this] (const juce::String& reading) { return spec.parse(reading); };
    }

    juce::String valueToString(float value) override { return spec.text(value); }
    float stringToValue(const juce::String& reading) override { return spec.parse(reading); }
    bool isDiscrete() const override { return spec.isDiscrete(); }
    int getNumberOfStates() const override
    {
        return spec.isDiscrete() ? juce::roundToInt((spec.maximum - spec.minimum) / step()) + 1 : 0;
    }
    float getValueForState(int state) const override { return spec.clamp(spec.minimum + static_cast<float>(state) * step()); }
    int getStateForValue(float value) const override { return juce::roundToInt((spec.clamp(value) - spec.minimum) / step()); }
    std::optional<float> getDefaultValue() const override { return spec.defaultValue; }
    bool hasLabels() const override { return spec.isChoice() || spec.toggle; }
    juce::String getLabelForValue(float value) const override { return hasLabels() ? spec.text(value) : juce::String(); }
    float snapToState(float value) const override { return spec.isDiscrete() ? spec.clamp(value) : value; }
    juce::StringArray getAllLabels() const override
    {
        if (spec.isChoice())
            return spec.choices;
        return spec.toggle ? juce::StringArray { "Off", "On" } : juce::StringArray();
    }

private:
    float step() const { return spec.interval > 0.0f ? spec.interval : 1.0f; }
    const ParamSpec spec;
};
}

NativeDevice::NativeDevice(te::PluginCreationInfo info, const char* type, DeviceKind deviceKind)
    : Plugin(std::move(info)), kind(deviceKind), typeName(type)
{
    // A device in the catalog takes the name the catalog gives it, so the
    // browser row and the device on the track cannot disagree.
    const auto* entry = DeviceCatalog::byTypeName(typeName);
    displayName = entry != nullptr ? entry->displayName : typeName;
}

NativeDevice::~NativeDevice()
{
    notifyListenersOfDeletion();
    // An engine parameter is reference counted and can outlive the device,
    // so it lets go of the value it was attached to before that value goes.
    for (auto& slot : slots)
        slot->parameter->detachFromCurrentValue();
}

ParamBuilder NativeDevice::param(const juce::String& id, const juce::String& name)
{
    return ParamBuilder(*this, id, name);
}

Param NativeDevice::add(ParamSpec spec)
{
    jassert(spec.id.isNotEmpty() && spec.maximum > spec.minimum);
    jassert(std::none_of(slots.begin(), slots.end(),
                         [&spec] (const auto& slot) { return slot->spec.id == spec.id; }));
    spec.defaultValue = spec.clamp(spec.defaultValue);
    auto slot = std::make_unique<ParamSlot>();
    slot->spec = std::move(spec);
    slot->stored.referTo(state, slot->spec.id, getUndoManager(), slot->spec.defaultValue);
    slot->parameter = new DeclaredParameter(slot->spec, *this);
    addAutomatableParameter(slot->parameter);
    slot->parameter->attachToCurrentValue(slot->stored);
    return Param(*slots.emplace_back(std::move(slot)));
}

te::Plugin::BusLayout NativeDevice::getBusses() const
{
    return BusLayout::singleStereoInOut();
}

void NativeDevice::initialise(const te::PluginInitialisationInfo& info)
{
    preparedRate = info.sampleRate > 0.0 ? info.sampleRate : 48000.0;
    preparedBlock = std::max(1, info.blockSizeSamples);
    for (auto& slot : slots)
        if (slot->spec.smoothingSeconds > 0.0f)
        {
            slot->smoother.reset(preparedRate, slot->spec.smoothingSeconds);
            slot->smoother.setCurrentAndTargetValue(slot->parameter->getCurrentValue());
        }
    prepare(preparedRate, preparedBlock);
    clear();
    clearPending.store(false);
    panicPending.store(false);
    peak.store(0.0f);
}

// The engine and the message thread both call these, and neither may touch
// state the audio thread is using. Each leaves a note for the next block.
void NativeDevice::reset()
{
    clearPending.store(true);
}

void NativeDevice::midiPanic()
{
    panicPending.store(true);
}

double NativeDevice::getLatencySeconds()
{
    return static_cast<double>(latencySamples()) / preparedRate;
}

double NativeDevice::getTailLength() const
{
    return std::max(0.0, tailSeconds());
}

void NativeDevice::restorePluginStateFromValueTree(const juce::ValueTree& source)
{
    for (auto& slot : slots)
    {
        if (source.hasProperty(slot->spec.id))
            slot->stored = slot->spec.clamp(static_cast<float>(source.getProperty(slot->spec.id)));
        else
            slot->stored.resetToDefault();
        slot->parameter->updateFromAttachedValue();
    }
    loadData(source);
}

void NativeDevice::dispatch(const juce::MidiMessage& message)
{
    if (message.isNoteOn())
        noteOn(message.getNoteNumber(), message.getFloatVelocity(), message.getChannel());
    else if (message.isNoteOff())
        noteOff(message.getNoteNumber(), message.getFloatVelocity(), message.getChannel());
    else if (message.isAllNotesOff() || message.isAllSoundOff())
        allNotesOff();
    else if (message.isController())
        controller(message.getControllerNumber(), message.getControllerValue(), message.getChannel());
    else if (message.isPitchWheel())
        pitchWheel(message.getPitchWheelValue(), message.getChannel());
}

void NativeDevice::startStretch()
{
    for (auto& slot : slots)
        if (slot->spec.smoothingSeconds > 0.0f)
            slot->smoother.setTargetValue(slot->parameter->getCurrentValue());
}

void NativeDevice::applyToBuffer(const te::PluginRenderContext& context)
{
    if (context.destBuffer == nullptr || context.bufferNumSamples <= 0)
        return;

    SCOPED_REALTIME_CHECK
    if (clearPending.exchange(false))
        clear();
    auto* midi = kind == DeviceKind::Instrument ? context.bufferForMidiMessages : nullptr;
    if (panicPending.exchange(false) || (midi != nullptr && midi->isAllNotesOff))
        allNotesOff();

    auto& buffer = *context.destBuffer;
    const auto channels = std::min(buffer.getNumChannels(), maximumChannels);
    const auto total = context.bufferNumSamples;
    // Which frame of this block an event plays on. A timestamp that lands a
    // hair past the block's end plays on its last frame rather than not at
    // all; see DrumDevice::applyToBuffer for how that happens.
    const auto frameOf = [this, total] (const juce::MidiMessage& message)
    {
        return std::clamp(juce::roundToInt(message.getTimeStamp() * preparedRate), 0, total - 1);
    };
    const te::MidiMessageWithSource* event = nullptr;
    const te::MidiMessageWithSource* lastEvent = nullptr;
    if (midi != nullptr && !midi->isEmpty())
    {
        event = midi->begin();
        lastEvent = midi->end();
    }

    float* pointers[maximumChannels] {};
    for (int done = 0; done < total;)
    {
        // Every event due by now is applied before the stretch it starts. An
        // event out of order is applied late rather than lost.
        while (event != lastEvent && frameOf(*event) <= done)
            dispatch(*event++);
        auto end = std::min(total, done + preparedBlock);
        if (event != lastEvent)
            end = std::min(end, frameOf(*event));

        for (int channel = 0; channel < channels; ++channel)
            pointers[channel] = buffer.getWritePointer(channel, context.bufferStartSample + done);
        RenderBlock block { pointers, channels, end - done };
        if (kind == DeviceKind::Instrument)
            for (int channel = 0; channel < channels; ++channel)
                juce::FloatVectorOperations::clear(pointers[channel], block.numSamples);
        startStretch();
        process(block);
        done = end;
    }

    // A device that has gone non-finite would poison everything after it in
    // the chain, the track's meter and the main output. Its block becomes
    // silence and its state is cleared, so the next block starts clean. A
    // finite signal is passed on untouched, however loud: the chain is
    // floating point and the fader after this decides the level.
    auto loudest = 0.0f;
    auto broken = false;
    for (int channel = 0; channel < channels && !broken; ++channel)
    {
        const auto* samples = buffer.getReadPointer(channel, context.bufferStartSample);
        for (int i = 0; i < total; ++i)
        {
            if (!std::isfinite(samples[i]))
            {
                broken = true;
                break;
            }
            loudest = std::max(loudest, std::abs(samples[i]));
        }
    }
    if (broken)
    {
        for (int channel = 0; channel < channels; ++channel)
            juce::FloatVectorOperations::clear(buffer.getWritePointer(channel, context.bufferStartSample), total);
        clear();
        loudest = 0.0f;
    }
    peak.store(loudest, std::memory_order_relaxed);
}
}
