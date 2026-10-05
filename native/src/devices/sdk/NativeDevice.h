#pragma once
#include "DeviceCatalog.h"
#include "sdk/NativeParameter.h"
#include <atomic>
#include <memory>
#include <vector>

namespace rhino
{
// The storage behind one Param. The device owns these, so a Param stays a
// cheap handle and the engine parameter is detached before its value goes.
struct ParamSlot
{
    ParamSpec spec;
    juce::CachedValue<float> stored;
    te::AutomatableParameter::Ptr parameter;
    juce::SmoothedValue<float> smoother;
};

// The base every Rhino device is written on. It owns what each device used to
// write by hand and got wrong in its own way:
//
// - Identity comes from the device's catalog entry.
// - Each control is declared once, as a Param member, and is saved, restored,
//   automated and formatted from that one declaration.
// - A block is split at every MIDI event and at the prepared block size, so a
//   device never sees MIDI late and never meets a block bigger than it
//   prepared for. Notes, controllers, all-notes-off and panic arrive as calls.
// - Output that goes non-finite is replaced by silence and the device is
//   cleared; it is never clipped.
// - Latency and tail are reported from what the device says.
//
// A device derives from NativeInstrument or NativeAudioEffect, declares its
// controls, and implements prepare, clear and process.
class NativeDevice : public te::Plugin
{
public:
    ~NativeDevice() override;

    juce::String getName() const override { return displayName; }
    juce::String getPluginType() override { return typeName; }
    juce::String getVendor() override { return "Rhino"; }
    juce::String getSelectableDescription() override { return displayName; }

    BusLayout getBusses() const override;
    bool isSynth() override { return kind == DeviceKind::Instrument; }
    bool takesMidiInput() override { return kind == DeviceKind::Instrument; }
    // A tail keeps sounding after the input stops, and the engine only goes on
    // calling a device it believes can make sound on its own.
    bool producesAudioWhenNoAudioInput() override
    {
        return kind == DeviceKind::Instrument || getTailLength() > 0.0 || isAutomationNeeded();
    }

    void initialise(const te::PluginInitialisationInfo&) final;
    void deinitialise() override {}
    void reset() final;
    void midiPanic() final;
    void applyToBuffer(const te::PluginRenderContext&) final;
    double getLatencySeconds() final;
    double getTailLength() const final;
    bool noTail() final { return getTailLength() <= 0.0; }
    void restorePluginStateFromValueTree(const juce::ValueTree&) final;

    DeviceKind deviceKind() const noexcept { return kind; }

    // Every control the device declared, in declaration order.
    int parameterCount() const { return static_cast<int>(slots.size()); }
    const ParamSpec& parameterSpec(int index) const { return slots[static_cast<size_t>(index)]->spec; }
    Param parameter(int index) const { return Param(*slots[static_cast<size_t>(index)]); }

    // The loudest sample of the last block, for a meter. Any thread.
    float outputPeak() const noexcept { return peak.load(std::memory_order_relaxed); }

protected:
    NativeDevice(te::PluginCreationInfo, const char* typeName, DeviceKind);

    // Declares a control. Assign the result to a Param member.
    ParamBuilder param(const juce::String& id, const juce::String& name);

    // One stretch of a block: no MIDI event falls inside it, and it is never
    // longer than the block size prepare() was given. Each channel pointer is
    // already at the stretch's first sample.
    struct RenderBlock
    {
        float* const* channels = nullptr;
        int numChannels = 0;
        int numSamples = 0;
    };

    // Message thread, never while processing. Allocate everything here.
    virtual void prepare(double sampleRate, int maximumBlockSize) = 0;
    // Forget all sound: voices, delay lines, filter state. Audio thread.
    virtual void clear() = 0;
    // An instrument adds into silence; an effect works in place.
    virtual void process(RenderBlock&) = 0;

    virtual void noteOn(int /*note*/, float /*velocity*/, int /*channel*/) {}
    virtual void noteOff(int /*note*/, float /*velocity*/, int /*channel*/) {}
    // Release every note. Sent for all-notes-off, all-sound-off, the engine's
    // own all-notes-off flag and a panic.
    virtual void allNotesOff() {}
    virtual void controller(int /*number*/, int /*value*/, int /*channel*/) {}
    virtual void pitchWheel(int /*value*/, int /*channel*/) {}

    // Message thread. Read from the controls as they stand.
    virtual int latencySamples() const { return 0; }
    virtual double tailSeconds() const { return 0.0; }

    // Content a device keeps beyond its controls, restored with them.
    virtual void loadData(const juce::ValueTree&) {}

private:
    friend class ParamBuilder;
    Param add(ParamSpec);
    void dispatch(const juce::MidiMessage&);
    void startStretch();

    static constexpr int maximumChannels = 8;

    const DeviceKind kind;
    juce::String typeName, displayName;
    std::vector<std::unique_ptr<ParamSlot>> slots;
    double preparedRate = 48000.0;
    int preparedBlock = 512;
    // Set from any thread, acted on by the audio thread at its next block.
    std::atomic<bool> clearPending { false }, panicPending { false };
    std::atomic<float> peak { 0.0f };
};

class NativeInstrument : public NativeDevice
{
protected:
    NativeInstrument(te::PluginCreationInfo info, const char* typeName)
        : NativeDevice(std::move(info), typeName, DeviceKind::Instrument) {}
};

class NativeAudioEffect : public NativeDevice
{
protected:
    NativeAudioEffect(te::PluginCreationInfo info, const char* typeName)
        : NativeDevice(std::move(info), typeName, DeviceKind::AudioEffect) {}
};
}
