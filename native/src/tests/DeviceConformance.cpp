#include "../Session.h"
#include "ContentLibrary.h"
#include "instruments/DrumRackDevice.h"
#include "sdk/NativeDevice.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
#include <set>
#include <stdexcept>
#include <vector>

// The device standard, checked. Run with `RhinoDAW.exe --device-test`; CTest
// runs it as native_device_conformance. Everything renders offline, so no
// audio device is opened.
//
// Two halves:
//
// - The SDK keeps its promises, shown through two probe devices that exist
//   only here: one declaration makes the engine parameter, its text and its
//   saved value; a block is split at every MIDI event and at the prepared
//   size; all-notes-off and panic arrive; a non-finite block becomes silence.
// - Every device Rhino makes itself is rendered and held to the same rules,
//   whether or not it is written on the SDK yet. What one cannot pass yet is
//   listed in `pending`, with the reason, printed on every run. The list only
//   shrinks: a listed failure that starts passing fails the run until its line
//   is deleted.

namespace rhino
{
namespace
{
constexpr double rate = 48000.0;

void require(bool valid, const juce::String& message)
{
    if (!valid)
        throw std::runtime_error(message.toStdString());
}

// ---- Probe devices ---------------------------------------------------------

class ProbeEffect final : public NativeAudioEffect
{
public:
    inline static const char* xmlTypeName = "rhino.probe-effect.test";
    explicit ProbeEffect(te::PluginCreationInfo info) : NativeAudioEffect(std::move(info), xmlTypeName) {}

    Param gain = param("gain", "Gain").range(-60.0f, 12.0f).defaultValue(-6.0f).unit(ParamUnit::decibels);
    Param mode = param("mode", "Mode").choices({ "Clean", "Warm", "Hot" }, 1);
    Param bypass = param("bypass", "Bypass").toggle();
    Param glide = param("glide", "Glide").range(0.0f, 1.0f).defaultValue(0.5f)
                      .unit(ParamUnit::percent).smoothing(0.01f).section("Tone");
    Param poison = param("poison", "Poison").toggle();
    Param cutoff = param("cutoff", "Cutoff").range(20.0f, 20000.0f).defaultValue(1000.0f)
                       .skewAround(1000.0f).unit(ParamUnit::hertz).section("Tone");

    // What the device was asked to do, read back by the checks.
    std::vector<int> stretches;
    int clears = 0;
    float firstGlide = 0.0f, lastGlide = 0.0f;

protected:
    void prepare(double, int) override { stretches.reserve(4096); }
    void clear() override { ++clears; }
    void process(RenderBlock& block) override
    {
        if (stretches.size() < stretches.capacity())
            stretches.push_back(block.numSamples);
        const auto factor = bypass.on() ? 1.0f : juce::Decibels::decibelsToGain(gain.value());
        for (int i = 0; i < block.numSamples; ++i)
        {
            const auto glideNow = glide.smoothed();
            if (i == 0)
                firstGlide = glideNow;
            lastGlide = glideNow;
            for (int channel = 0; channel < block.numChannels; ++channel)
                block.channels[channel][i] = poison.on() ? std::numeric_limits<float>::quiet_NaN()
                                                         : block.channels[channel][i] * factor;
        }
    }
};

// Sounds a constant 1.0 while any note is held, so where a note starts and
// stops can be read straight off the output.
class ProbeInstrument final : public NativeInstrument
{
public:
    inline static const char* xmlTypeName = "rhino.probe-instrument.test";
    explicit ProbeInstrument(te::PluginCreationInfo info) : NativeInstrument(std::move(info), xmlTypeName) {}

    Param level = param("level", "Level").range(0.0f, 1.0f).defaultValue(1.0f);

    int held = 0;
    int releases = 0;
    int controllers = 0;
    juce::int64 frame = 0;
    juce::int64 firstNoteFrame = -1;

protected:
    void prepare(double, int) override {}
    void clear() override { held = 0; }
    void noteOn(int, float, int) override
    {
        if (firstNoteFrame < 0)
            firstNoteFrame = frame;
        ++held;
    }
    void noteOff(int, float, int) override { held = std::max(0, held - 1); }
    void allNotesOff() override
    {
        held = 0;
        ++releases;
    }
    void controller(int, int, int) override { ++controllers; }
    void process(RenderBlock& block) override
    {
        const auto value = held > 0 ? level.value() : 0.0f;
        for (int channel = 0; channel < block.numChannels; ++channel)
            for (int i = 0; i < block.numSamples; ++i)
                block.channels[channel][i] += value;
        frame += block.numSamples;
    }
};

template <typename Device>
juce::ReferenceCountedObjectPtr<Device> create(te::Edit& edit, const juce::String& type)
{
    auto plugin = edit.getPluginCache().createNewPlugin(type, {});
    auto* device = dynamic_cast<Device*>(plugin.get());
    require(device != nullptr, "The engine creates " + type);
    return device;
}

te::PluginRenderContext contextFor(juce::AudioBuffer<float>& buffer, int start, int count, te::MidiMessageArray* midi)
{
    const auto from = tracktion::core::TimePosition::fromSeconds(start / rate);
    const auto to = tracktion::core::TimePosition::fromSeconds((start + count) / rate);
    return te::PluginRenderContext(&buffer, start, count, midi, 0.0, { from, to }, true, false, true, false);
}

bool near(float a, float b, float tolerance = 1.0e-5f)
{
    return std::abs(a - b) <= tolerance;
}

void checkProbeDeclarations(te::Edit& edit)
{
    auto probe = create<ProbeEffect>(edit, ProbeEffect::xmlTypeName);
    auto& device = *probe;

    // One declaration is the engine parameter, in declaration order.
    const auto parameters = device.getAutomatableParameters();
    require(parameters.size() == 6 && device.parameterCount() == 6, "Six controls declared, six engine parameters");
    const juce::StringArray ids { "gain", "mode", "bypass", "glide", "poison", "cutoff" };
    for (int i = 0; i < ids.size(); ++i)
        require(parameters[i]->paramID == ids[i] && device.parameterSpec(i).id == ids[i],
                "Engine parameters keep declaration order");

    // Its text, defaults and states come from the declaration.
    require(device.gain.value() == -6.0f && device.gain.automatable().getCurrentValueAsString() == "-6.0 dB",
            "A decibel control starts at its default and reads in dB");
    require(device.mode.index() == 1 && device.mode.automatable().getCurrentValueAsString() == "Warm",
            "A chooser starts on its default choice and reads as its label");
    require(device.mode.automatable().isDiscrete() && device.mode.automatable().getNumberOfStates() == 3
                && device.mode.automatable().getAllLabels() == juce::StringArray { "Clean", "Warm", "Hot" },
            "The engine sees a chooser as discrete, with its labels");
    require(!device.bypass.on() && device.bypass.automatable().getCurrentValueAsString() == "Off"
                && device.bypass.automatable().getNumberOfStates() == 2,
            "A toggle is off, two-state and reads Off");
    require(device.glide.automatable().getCurrentValueAsString() == "50%", "A percentage reads as one");
    require(!device.gain.automatable().isDiscrete(), "A continuous control is not discrete");
    require(device.gain.automatable().getDefaultValue() == std::optional<float>(-6.0f),
            "The engine knows a control's default");

    // A knob's travel is linear unless a skew is declared, even for a range
    // that straddles zero; and a declared skew puts its centre mid-travel.
    require(near(device.gain.automatable().valueRange.convertTo0to1(-24.0f), 0.5f),
            "A control that declares no skew moves linearly across zero");
    require(near(device.cutoff.automatable().valueRange.convertTo0to1(1000.0f), 0.5f, 1.0e-4f)
                && device.cutoff.automatable().getCurrentValueAsString() == "1.00 kHz",
            "A skewed control puts its declared centre in the middle of its travel");

    // Typed readings parse back through the unit.
    require(near(device.gain.spec().parse("3 dB"), 3.0f) && near(device.glide.spec().parse("25%"), 0.25f)
                && near(device.mode.spec().parse("Hot"), 2.0f) && near(device.bypass.spec().parse("On"), 1.0f),
            "Readings parse back into values");
    require(near(device.gain.spec().parse("99"), 12.0f) && near(device.mode.spec().parse("1.4"), 1.0f),
            "A parsed value is clamped and snapped to the control");

    // The value is saved under its id, and a device made from that state
    // comes back with it.
    device.gain.automatable().setParameter(-12.0f, juce::sendNotificationSync);
    device.mode.automatable().setParameter(2.0f, juce::sendNotificationSync);
    device.bypass.automatable().setParameter(1.0f, juce::sendNotificationSync);
    device.flushPluginStateToValueTree();
    require(near(static_cast<float>(device.state.getProperty("gain")), -12.0f), "A control is saved under its id");
    auto copy = device.state.createCopy();
    copy.removeProperty(te::IDs::id, nullptr);
    auto reloaded = edit.getPluginCache().createNewPlugin(copy);
    auto* again = dynamic_cast<ProbeEffect*>(reloaded.get());
    require(again != nullptr && again->gain.value() == -12.0f && again->mode.index() == 2 && again->bypass.on(),
            "A device made from saved state has its controls back");

    // Restoring takes absent properties back to their defaults, and clamps a
    // stored value the control could not hold.
    juce::ValueTree restored(te::IDs::PLUGIN);
    restored.setProperty("gain", 99.0f, nullptr);
    restored.setProperty("mode", 1.4f, nullptr);
    device.restorePluginStateFromValueTree(restored);
    require(device.gain.value() == 12.0f && device.mode.index() == 1 && !device.bypass.on()
                && near(device.glide.value(), 0.5f),
            "Restoring clamps what is out of range and defaults what is missing");

    // An undo reaches the engine parameter at once. Tracktion would move it on
    // a later message, and the rack, which reads straight after an undo, would
    // show the value that had just been undone.
    auto& undo = edit.getUndoManager();
    undo.beginNewTransaction();
    device.glide.automatable().setParameter(0.9f, juce::sendNotificationSync);
    undo.beginNewTransaction();
    require(near(device.glide.value(), 0.9f), "A control takes a new value");
    undo.undo();
    require(!device.state.hasProperty("glide") && near(device.glide.value(), 0.5f),
            "Undoing it reaches the engine parameter at once, even by removing the stored value");
    undo.redo();
    require(near(device.glide.value(), 0.9f), "and so does redoing it");
}

void checkProbeProcessing(te::Edit& edit)
{
    // A block is never handed over bigger than the size prepared for.
    {
        auto probe = create<ProbeEffect>(edit, ProbeEffect::xmlTypeName);
        auto& device = *probe;
        device.initialise({ {}, rate, 64 });
        require(device.clears == 1, "Preparing clears the device once");
        juce::AudioBuffer<float> buffer(2, 1000);
        buffer.clear();
        device.applyToBuffer(contextFor(buffer, 0, 1000, nullptr));
        const auto total = std::accumulate(device.stretches.begin(), device.stretches.end(), 0);
        require(total == 1000 && *std::max_element(device.stretches.begin(), device.stretches.end()) == 64
                    && device.stretches.size() == 16,
                "A 1000-sample block arrives as fifteen 64-sample stretches and one of 40");

        // reset() may come from any thread, so it is acted on at the next
        // block rather than at once.
        device.reset();
        require(device.clears == 1, "reset() does not touch the device itself");
        device.applyToBuffer(contextFor(buffer, 0, 64, nullptr));
        require(device.clears == 2, "The next block clears it");

        // A smoothed control glides to a new value instead of jumping.
        device.glide.automatable().setParameter(1.0f, juce::sendNotificationSync);
        device.applyToBuffer(contextFor(buffer, 0, 48, nullptr));
        require(device.firstGlide > 0.5f && device.lastGlide < 0.6f, "A smoothed control starts moving, slowly");
        for (int i = 0; i < 20; ++i)
            device.applyToBuffer(contextFor(buffer, 0, 64, nullptr));
        require(near(device.lastGlide, 1.0f), "and arrives within its smoothing time");

        // A non-finite block becomes silence, and clears the device.
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < 64; ++i)
                buffer.setSample(channel, i, 0.5f);
        device.poison.automatable().setParameter(1.0f, juce::sendNotificationSync);
        const auto clearsBefore = device.clears;
        device.applyToBuffer(contextFor(buffer, 0, 64, nullptr));
        require(buffer.getMagnitude(0, 64) == 0.0f && device.clears == clearsBefore + 1 && device.outputPeak() == 0.0f,
                "A device that goes non-finite is silenced and cleared");
        device.poison.automatable().setParameter(0.0f, juce::sendNotificationSync);
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < 64; ++i)
                buffer.setSample(channel, i, 0.5f);
        device.applyToBuffer(contextFor(buffer, 0, 64, nullptr));
        require(buffer.getMagnitude(0, 64) > 0.0f && device.outputPeak() > 0.0f, "and the next block plays again");
        device.deinitialise();
    }

    // MIDI lands on its own frame, and nothing an instrument held before
    // survives an all-notes-off however it arrives.
    {
        auto probe = create<ProbeInstrument>(edit, ProbeInstrument::xmlTypeName);
        auto& device = *probe;
        device.initialise({ {}, rate, 512 });
        juce::AudioBuffer<float> buffer(2, 512);
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < 512; ++i)
                buffer.setSample(channel, i, 7.0f);
        te::MidiMessageArray midi;
        midi.addMidiMessage(juce::MidiMessage::noteOn(1, 60, 0.8f), 300.0 / rate, {});
        device.applyToBuffer(contextFor(buffer, 0, 512, &midi));
        require(device.firstNoteFrame == 300, "A note-on is applied at its own frame");
        for (int i = 0; i < 512; ++i)
            require(buffer.getSample(0, i) == (i < 300 ? 0.0f : 1.0f) && buffer.getSample(1, i) == buffer.getSample(0, i),
                    "An instrument starts from silence and sounds from the note's frame on, frame " + juce::String(i));

        // An all-notes-off controller ends the note at its own frame.
        midi.clear();
        midi.addMidiMessage(juce::MidiMessage::allNotesOff(1), 100.0 / rate, {});
        device.applyToBuffer(contextFor(buffer, 0, 512, &midi));
        require(buffer.getSample(0, 99) == 1.0f && buffer.getSample(0, 100) == 0.0f && device.releases == 1,
                "All-notes-off releases at its own frame");

        // The engine's own flag releases before the block.
        midi.clear();
        midi.addMidiMessage(juce::MidiMessage::noteOn(1, 64, 0.8f), 0.0, {});
        device.applyToBuffer(contextFor(buffer, 0, 512, &midi));
        midi.clear();
        midi.isAllNotesOff = true;
        device.applyToBuffer(contextFor(buffer, 0, 512, &midi));
        require(device.releases == 2 && buffer.getMagnitude(0, 512) == 0.0f, "The engine's all-notes-off releases at once");

        // A panic may come from the message thread, so it waits for a block.
        midi.clear();
        midi.addMidiMessage(juce::MidiMessage::noteOn(1, 67, 0.8f), 0.0, {});
        device.applyToBuffer(contextFor(buffer, 0, 512, &midi));
        device.midiPanic();
        require(device.releases == 2 && device.held == 1, "A panic does not reach into the audio thread's state");
        midi.clear();
        device.applyToBuffer(contextFor(buffer, 0, 512, &midi));
        require(device.releases == 3 && buffer.getMagnitude(0, 512) == 0.0f, "and releases at the next block");

        midi.addMidiMessage(juce::MidiMessage::controllerEvent(1, 74, 90), 0.0, {});
        device.applyToBuffer(contextFor(buffer, 0, 512, &midi));
        require(device.controllers == 1, "Controllers arrive as calls");
        device.deinitialise();
    }
}

// What the session tells the rack about a native device: that it is one, its
// catalog id, and the shape each control declares. A face is generated from
// exactly this, so it is checked here rather than through a face.
void checkNativeDevicesThroughSession(Session& session)
{
    constexpr auto trackIndex = 1;
    auto* track = te::getAudioTracks(*session.edit)[trackIndex];
    require(track != nullptr, "The starter stack has a second track");
    track->pluginList.insertPlugin(session.edit->getPluginCache().createNewPlugin(ProbeEffect::xmlTypeName, {}), -1, nullptr);
    require(session.addDevice("RhinoSpace", trackIndex).wasOk(), "Rhino Space goes on the second track");

    const Session::DeviceSlot* probe = nullptr;
    const Session::DeviceSlot* space = nullptr;
    const auto slots = session.deviceSlots(trackIndex);
    for (const auto& slot : slots)
    {
        if (slot.type == ProbeEffect::xmlTypeName)
            probe = &slot;
        if (slot.type == "rhino.space.v1")
            space = &slot;
    }
    require(probe != nullptr && probe->native && probe->deviceId.isEmpty(),
            "A native device the catalog lacks is reported native, with no catalog id");
    require(space != nullptr && space->native && space->deviceId == "RhinoSpace",
            "A catalog device on the SDK is reported native, under its catalog id");

    const auto controls = session.deviceParameters(trackIndex, probe->pluginIndex);
    require(controls.size() == 6, "Every declared control is reported");
    require(controls[0].defaultValue == std::optional<float>(-6.0f) && controls[0].choices.isEmpty()
                && !controls[0].toggle && controls[0].section.isEmpty() && controls[0].skew == 1.0,
            "A plain knob reports its default and a linear travel");
    require(controls[1].choices == juce::StringArray { "Clean", "Warm", "Hot" } && controls[1].discrete
                && !controls[1].toggle && controls[1].valueText == "Warm",
            "A chooser reports its choices and reads as one");
    require(controls[2].toggle && controls[2].discrete && controls[2].choices.isEmpty(),
            "A toggle reports itself as one");
    require(controls[3].section == "Tone" && controls[5].section == "Tone",
            "A control reports the section it is declared in");
    require(controls[5].skew != 1.0, "A skewed control reports its travel");
}

// ---- The catalog -------------------------------------------------------------

// A failure known and explained. The table is printed on every run, and a
// line whose check now passes fails the run until it is deleted.
struct Pending
{
    const char* deviceId;
    const char* check;
    const char* reason;
};

const std::vector<Pending> pending = {
};

struct Stimulus
{
    int frame = 0;
    juce::MidiMessage message;
};

// What a device is fed: noise for an effect, a chord for an instrument. The
// notes cover the drum rack's pads as well as a synth's middle octave.
std::vector<Stimulus> chord(int onFrame, int offFrame)
{
    std::vector<Stimulus> events;
    for (const auto note : { 48, 53, 58, 60, 64, 67 })
    {
        events.push_back({ onFrame, juce::MidiMessage::noteOn(1, note, 0.8f) });
        if (offFrame >= 0)
            events.push_back({ offFrame, juce::MidiMessage::noteOff(1, note) });
    }
    return events;
}

struct Render
{
    juce::AudioBuffer<float> audio;
    bool finite = true;
    float peak = 0.0f;
};

// Renders `frames` of a device from a fresh start, in blocks of `blockSize`
// after preparing it for `preparedBlock`. An effect hears seeded noise, so
// two renders hear the same noise; an instrument hears silence and `events`.
Render render(te::Plugin& plugin, DeviceKind kind, int preparedBlock, int blockSize, int frames,
              const std::vector<Stimulus>& events, juce::int64 seed, bool impulseOnly = false)
{
    Render result;
    result.audio.setSize(2, frames);
    result.audio.clear();
    if (kind == DeviceKind::AudioEffect)
    {
        juce::Random noise(seed);
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < frames; ++i)
                result.audio.setSample(channel, i, impulseOnly ? (i == 0 ? 1.0f : 0.0f)
                                                               : (noise.nextFloat() * 2.0f - 1.0f) * 0.25f);
    }
    plugin.initialise({ {}, rate, preparedBlock });
    for (int start = 0; start < frames; start += blockSize)
    {
        const auto count = std::min(blockSize, frames - start);
        te::MidiMessageArray midi;
        for (const auto& event : events)
            if (event.frame >= start && event.frame < start + count)
                midi.addMidiMessage(event.message, (event.frame - start) / rate, {});
        plugin.applyToBuffer(contextFor(result.audio, start, count, &midi));
    }
    plugin.deinitialise();
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < frames; ++i)
        {
            const auto sample = result.audio.getSample(channel, i);
            result.finite = result.finite && std::isfinite(sample);
            if (std::isfinite(sample))
                result.peak = std::max(result.peak, std::abs(sample));
        }
    return result;
}

float loudestFrom(const juce::AudioBuffer<float>& audio, int from)
{
    auto loudest = 0.0f;
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int i = std::max(0, from); i < audio.getNumSamples(); ++i)
            loudest = std::max(loudest, std::abs(audio.getSample(channel, i)));
    return loudest;
}

void setRandomly(te::Plugin& plugin, juce::Random& random)
{
    for (auto* parameter : plugin.getAutomatableParameters())
    {
        const auto range = parameter->valueRange;
        parameter->setParameter(range.snapToLegalValue(range.convertFrom0to1(random.nextFloat())),
                                juce::sendNotificationSync);
    }
}

// A new Drum Rack is blank and plays nothing at all, which would pass the
// checks that listen for trouble and fail every one that listens for a note.
// So it is held to the standard with pads on the notes the chord strikes:
// three synthesised drums and a sample.
void prime(te::Plugin& plugin)
{
    if (auto* drums = dynamic_cast<DrumRackDevice*>(&plugin))
    {
        drums->setPadSynth(0, DrumModel::Kick);
        drums->setPadSynth(5, DrumModel::Snare);
        drums->setPadSynth(10, DrumModel::ClosedHat);
        drums->setPadSample(12, ContentLibrary::file("Samples/TR808/TR808Kick.wav"));
    }
}

// Each check answers an empty string for a pass, or what went wrong.
using Check = std::function<juce::String()>;

void conform(Session& session, const DeviceDescriptor& descriptor, std::vector<juce::String>& unexpected,
             std::set<juce::String>& ran)
{
    auto& edit = *session.edit;
    const auto make = [&edit, &descriptor]
    {
        auto plugin = edit.getPluginCache().createNewPlugin(descriptor.typeName, {});
        require(plugin != nullptr, descriptor.id + " is created from its type name");
        prime(*plugin);
        return plugin;
    };
    const auto audio = descriptor.kind != DeviceKind::MidiEffect;
    const auto instrument = descriptor.kind == DeviceKind::Instrument;
    const auto second = static_cast<int>(rate);
    // The checks below capture by reference and run at the end of this
    // function, so everything they read is declared out here.
    const auto stimulus = instrument ? chord(0, second / 2) : std::vector<Stimulus>();

    std::vector<std::pair<const char*, Check>> checks;
    checks.emplace_back("identity", [&]
    {
        auto plugin = make();
        if (plugin->getPluginType() != descriptor.typeName)
            return "its type is " + plugin->getPluginType();
        if (plugin->getName() != descriptor.displayName)
            return "it calls itself " + plugin->getName() + ", the catalog " + descriptor.displayName;
        // A MIDI effect is told apart by having no audio at all. Rhino Arp
        // never claims takesMidiInput and still receives the chain's MIDI, so
        // that flag is not part of what makes one.
        const auto busses = plugin->getBusses();
        const auto kindMatches = descriptor.kind == DeviceKind::Instrument ? plugin->isSynth() && plugin->takesMidiInput()
            : descriptor.kind == DeviceKind::AudioEffect ? !plugin->isSynth() && !busses.inputs.empty()
            : !plugin->isSynth() && busses.inputs.empty() && busses.outputs.empty();
        return kindMatches ? juce::String() : juce::String("it does not behave as the kind the catalog gives it");
    });
    checks.emplace_back("parameters", [&]
    {
        auto plugin = make();
        std::set<juce::String> ids;
        for (auto* parameter : plugin->getAutomatableParameters())
        {
            const auto range = parameter->getValueRange();
            if (parameter->paramID.isEmpty() || !ids.insert(parameter->paramID).second)
                return "a parameter id is empty or repeated: '" + parameter->paramID + "'";
            if (!std::isfinite(range.getStart()) || !std::isfinite(range.getEnd()) || range.getLength() <= 0.0f)
                return parameter->paramID + " has no usable range";
            const auto value = parameter->getCurrentValue();
            if (value < range.getStart() - 1.0e-6f || value > range.getEnd() + 1.0e-6f)
                return parameter->paramID + " starts outside its own range";
            if (parameter->getCurrentValueAsString().trim().isEmpty())
                return parameter->paramID + " reads as nothing";
        }
        return juce::String();
    });
    checks.emplace_back("state", [&]
    {
        auto plugin = make();
        juce::Random random(17);
        setRandomly(*plugin, random);
        plugin->flushPluginStateToValueTree();
        auto copy = plugin->state.createCopy();
        copy.removeProperty(te::IDs::id, nullptr);
        auto reloaded = edit.getPluginCache().createNewPlugin(copy);
        if (reloaded == nullptr)
            return juce::String("its saved state makes nothing");
        const auto before = plugin->getAutomatableParameters();
        const auto after = reloaded->getAutomatableParameters();
        if (before.size() != after.size())
            return juce::String("a reloaded device has a different number of parameters");
        for (int i = 0; i < before.size(); ++i)
            if (!near(before[i]->getCurrentValue(), after[i]->getCurrentValue(), 1.0e-4f))
                return before[i]->paramID + " comes back as " + juce::String(after[i]->getCurrentValue())
                       + ", not " + juce::String(before[i]->getCurrentValue());
        // Restoring a state that names nothing puts every control back to
        // the default it reports.
        reloaded->restorePluginStateFromValueTree(juce::ValueTree(te::IDs::PLUGIN));
        for (auto* parameter : reloaded->getAutomatableParameters())
            if (const auto fallback = parameter->getDefaultValue();
                fallback.has_value() && !near(parameter->getCurrentValue(), *fallback, 1.0e-4f))
                return parameter->paramID + " restores to " + juce::String(parameter->getCurrentValue())
                       + " rather than its default " + juce::String(*fallback);
        return juce::String();
    });
    if (audio)
    {
        checks.emplace_back("safety", [&]
        {
            // At its defaults and at settings drawn at random, a device stays
            // finite and does not run away.
            auto plugin = make();
            juce::Random random(29);
            for (int round = 0; round < 25; ++round)
            {
                if (round > 0)
                    setRandomly(*plugin, random);
                const auto result = render(*plugin, descriptor.kind, 512, 512, second / 4,
                                           instrument ? chord(0, second / 8) : stimulus, round);
                if (!result.finite)
                    return "it goes non-finite at random setting " + juce::String(round);
                if (result.peak > 64.0f)
                    return "it reaches " + juce::String(result.peak, 1) + " at random setting " + juce::String(round);
            }
            return juce::String();
        });
        checks.emplace_back("invariance", [&]
        {
            // The same input gives the same output whatever the block size,
            // including blocks bigger than the device prepared for.
            const auto reference = render(*make(), descriptor.kind, 512, 512, second, stimulus, 7);
            const std::array<std::pair<int, int>, 3> runs { { { 64, 64 }, { 4096, 4096 }, { 512, 4096 } } };
            for (const auto& [prepared, block] : runs)
            {
                const auto other = render(*make(), descriptor.kind, prepared, block, second, stimulus, 7);
                for (int channel = 0; channel < 2; ++channel)
                    for (int i = 0; i < second; ++i)
                        if (!near(reference.audio.getSample(channel, i), other.audio.getSample(channel, i), 1.0e-5f))
                            return "prepared for " + juce::String(prepared) + " in blocks of " + juce::String(block)
                                   + " it differs at frame " + juce::String(i);
            }
            return juce::String();
        });
        if (instrument)
        {
            checks.emplace_back("midi-timing", [&]
            {
                // Silence until the note's own frame, then sound.
                const auto result = render(*make(), descriptor.kind, 512, 512, 4096, chord(300, -1), 0);
                for (int channel = 0; channel < 2; ++channel)
                    for (int i = 0; i < 300; ++i)
                        if (result.audio.getSample(channel, i) != 0.0f)
                            return "it sounds at frame " + juce::String(i) + ", before the note at 300";
                return loudestFrom(result.audio, 300) > 0.0f ? juce::String() : juce::String("the note never sounds");
            });
            checks.emplace_back("all-notes-off", [&]
            {
                // A chord held, then the engine's all-notes-off: within five
                // seconds the device is quiet.
                auto plugin = make();
                auto events = chord(0, -1);
                Render result;
                result.audio.setSize(2, second * 5);
                result.audio.clear();
                plugin->initialise({ {}, rate, 512 });
                for (int start = 0; start < second * 5; start += 512)
                {
                    te::MidiMessageArray midi;
                    if (start == 0)
                        for (const auto& event : events)
                            midi.addMidiMessage(event.message, 0.0, {});
                    midi.isAllNotesOff = start == second / 2 / 512 * 512;
                    plugin->applyToBuffer(contextFor(result.audio, start, std::min(512, second * 5 - start), &midi));
                }
                plugin->deinitialise();
                const auto tail = loudestFrom(result.audio, second * 4);
                return tail < 1.0e-4f ? juce::String()
                                      : "it is still at " + juce::String(juce::Decibels::gainToDecibels(tail), 1)
                                            + " dB four seconds after all-notes-off";
            });
        }
        else
        {
            checks.emplace_back("tail", [&]
            {
                // A full-scale impulse, then silence. A tail is the time the
                // device takes to fall 60 dB, which is how Space and Bloom
                // compute theirs; one that reports none must be that quiet
                // within a tenth of a second.
                auto plugin = make();
                const auto tail = plugin->getTailLength();
                const auto quietFrom = static_cast<int>((tail > 0.0 ? tail : 0.1) * rate);
                const auto frames = quietFrom + second;
                const auto result = render(*plugin, descriptor.kind, 512, 512, frames, {}, 0, true);
                const auto after = loudestFrom(result.audio, quietFrom);
                return after < 1.0e-3f ? juce::String()
                                       : "it reports a tail of " + juce::String(tail, 2) + " s and is at "
                                             + juce::String(juce::Decibels::gainToDecibels(after), 1) + " dB after it";
            });
        }
    }

    for (const auto& [name, check] : checks)
    {
        juce::String failure;
        // A crash prints nothing, so the log's last line is what places it.
        juce::Logger::writeToLog("  " + descriptor.id + "/" + name);
        try
        {
            failure = check();
        }
        catch (const std::exception& error)
        {
            failure = error.what();
        }
        const auto key = descriptor.id + "/" + name;
        ran.insert(key);
        const auto known = std::find_if(pending.begin(), pending.end(), [&] (const Pending& entry)
        {
            return descriptor.id == entry.deviceId && juce::String(name) == entry.check;
        });
        if (failure.isEmpty())
        {
            if (known != pending.end())
                unexpected.push_back(key + " passes now: delete its line from `pending`");
            continue;
        }
        if (known == pending.end())
            unexpected.push_back(key + ": " + failure);
    }
}
}

int runDeviceConformance()
{
    try
    {
        Session session;
        auto& plugins = session.engine.getPluginManager();
        plugins.createBuiltInType<ProbeEffect>();
        plugins.createBuiltInType<ProbeInstrument>();
        juce::Logger::writeToLog("Rhino: device conformance, the SDK's declarations");
        checkProbeDeclarations(*session.edit);
        juce::Logger::writeToLog("Rhino: device conformance, the SDK's processing");
        checkProbeProcessing(*session.edit);
        juce::Logger::writeToLog("Rhino: device conformance, native devices through the session");
        checkNativeDevicesThroughSession(session);
        juce::Logger::writeToLog("Rhino: device conformance, presets");
        checkDevicePresets(session);

        std::vector<juce::String> unexpected;
        std::set<juce::String> ran;
        auto checked = 0;
        for (const auto& descriptor : DeviceCatalog::all())
        {
            if (!descriptor.create)
                continue;
            ++checked;
            conform(session, descriptor, unexpected, ran);
        }
        for (const auto& entry : pending)
            if (ran.count(juce::String(entry.deviceId) + "/" + entry.check) == 0)
                unexpected.push_back(juce::String(entry.deviceId) + "/" + entry.check
                                     + " names no check that runs: delete its line from `pending`");

        const auto report = [] (const juce::String& line)
        {
            juce::Logger::writeToLog(line);
            std::fprintf(stderr, "%s\n", line.toRawUTF8());
        };
        report("Rhino: device conformance checked " + juce::String(checked) + " devices");
        for (const auto& entry : pending)
            report(juce::String("  pending ") + entry.deviceId + "/" + entry.check + ": " + entry.reason);
        for (const auto& failure : unexpected)
            report("  FAILED " + failure);
        return unexpected.empty() ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        juce::Logger::writeToLog(juce::String("Device conformance failed: ") + error.what());
        std::fprintf(stderr, "Device conformance failed: %s\n", error.what());
        return 1;
    }
}
}
