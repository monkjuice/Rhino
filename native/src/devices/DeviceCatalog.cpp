#include "DeviceCatalog.h"
#include "instruments/DrumDevice.h"
#include "instruments/RhinoWaveDevice.h"
#include "audio/UtilityDevice.h"
#include "audio/RhinoSpaceDevice.h"
#include "audio/RhinoBloomDevice.h"
#include "audio/RhinoEqDevice.h"
#include "audio/AutoTuneDevice.h"
#include "audio/VocoderDevice.h"
#include "midi/RhinoArpDevice.h"

namespace rhino
{
namespace
{
// Order is the order the browser shows, so keep a new device beside the ones
// it belongs with rather than appending to the end.
std::vector<DeviceDescriptor> buildCatalog()
{
    std::vector<DeviceDescriptor> catalog;

    auto add = [&catalog](DeviceDescriptor descriptor) { catalog.push_back(std::move(descriptor)); };

    // ---- Instruments -------------------------------------------------------
    add({"FourOsc", te::FourOscPlugin::xmlTypeName, "4OSC", "4OSC synth",
         DeviceKind::Instrument, "Synths", "Subtractive synth",
         0xff3d6f8b, "synth", true, false, false});

    add({"RhinoWave", RhinoWaveDevice::xmlTypeName, "Rhino Wave", {},
         DeviceKind::Instrument, "Synths", "Morphing wavetable-style synth",
         0xff574ec8, "wave", true, false, false});

    // Forge is a VST3 discovered by scanning, so it has no type name to
    // create from -- see SessionExternalPlugins.cpp.
    add({"RhinoForge", {}, "Rhino Forge", {},
         DeviceKind::Instrument, "Synths", "Two-oscillator Forge synth",
         0xff3a9aa9, "forge", true, true, false});

    // The drum rack is reached through its kits, which the browser lists
    // instead of the bare device, so it is not browsable itself.
    add({"Drums", DrumDevice::xmlTypeName, "Rhino Drums", {},
         DeviceKind::Instrument, "Drum Rack", "Sample drum rack",
         0xff738044, "drums", false, false, false});

    // ---- Audio FX ----------------------------------------------------------
    // Keeps the id the Tracktion four-band EQ had: presets, browser drops
    // and Session::AudioEffect all name a device by id, and this is the same
    // slot in the chain with eight bands and a display behind them.
    add({"Equaliser", RhinoEqDevice::xmlTypeName, "Rhino EQ", "EQ",
         DeviceKind::AudioEffect, "EQ and Filters", "Eight bands over a live spectrum",
         0xff6f9ec4, {}, true, false, false});

    add({"Compressor", te::CompressorPlugin::xmlTypeName, "Compressor", {},
         DeviceKind::AudioEffect, "Dynamics", "Tracktion compressor",
         0, {}, true, false, false});

    // The engine keeps a gain stage in every channel strip, so this type also
    // appears in the graph without the user having added one.
    add({"Utility", UtilityDevice::xmlTypeName, "Utility", "Utility gain",
         DeviceKind::AudioEffect, "Dynamics", "Level trim inside a chain",
         0xff56636c, {}, true, false, true});

    add({"Reverb", te::ReverbPlugin::xmlTypeName, "Reverb", {},
         DeviceKind::AudioEffect, "Delay and Reverb", "Tracktion reverb",
         0, {}, true, false, false});

    add({"Delay", te::DelayPlugin::xmlTypeName, "Delay", {},
         DeviceKind::AudioEffect, "Delay and Reverb", "Tracktion delay",
         0, {}, true, false, false});

    add({"RhinoSpace", RhinoSpaceDevice::xmlTypeName, "Rhino Space", {},
         DeviceKind::AudioEffect, "Rhino", "Floating multi FX: smear, drive, width",
         0, {}, true, false, false});

    add({"RhinoBloom", RhinoBloomDevice::xmlTypeName, "Rhino Bloom", {},
         DeviceKind::AudioEffect, "Rhino", "Chorus, clouds, plate, colour",
         0, {}, true, false, false});

    add({"RhinoTune", AutoTuneDevice::xmlTypeName, "Rhino Tune", "Rhino Tune (auto-tune)",
         DeviceKind::AudioEffect, "Rhino", "Vocal pitch correction, formants and vibrato",
         0xffb2739c, {}, true, false, false});

    add({"RhinoVocoder", VocoderDevice::xmlTypeName, "Rhino Vocoder", {},
         DeviceKind::AudioEffect, "Rhino", "Plays this track's voice with another track's synth",
         0xff7d8fc4, {}, true, false, false});

    // ---- MIDI FX -----------------------------------------------------------
    add({"RhinoArp", RhinoArpDevice::xmlTypeName, "Rhino Arp", {},
         DeviceKind::MidiEffect, {}, "Drop before an instrument to arpeggiate it",
         0, {}, true, false, false});

    return catalog;
}
}

const std::vector<DeviceDescriptor>& DeviceCatalog::all()
{
    static const std::vector<DeviceDescriptor> catalog = buildCatalog();
    return catalog;
}

std::vector<const DeviceDescriptor*> DeviceCatalog::ofKind(DeviceKind kind)
{
    std::vector<const DeviceDescriptor*> matches;
    for (const auto& descriptor : all())
        if (descriptor.kind == kind)
            matches.push_back(&descriptor);
    return matches;
}

const DeviceDescriptor* DeviceCatalog::byId(const juce::String& id)
{
    if (id.isEmpty())
        return nullptr;
    for (const auto& descriptor : all())
        if (descriptor.id == id)
            return &descriptor;
    return nullptr;
}

const DeviceDescriptor* DeviceCatalog::byTypeName(const juce::String& typeName)
{
    // An external device has no type name, so an empty query must not match it.
    if (typeName.isEmpty())
        return nullptr;
    for (const auto& descriptor : all())
        if (descriptor.typeName == typeName)
            return &descriptor;
    return nullptr;
}

juce::String DeviceCatalog::labelFor(const DeviceDescriptor& descriptor)
{
    return descriptor.browserLabel.isNotEmpty() ? descriptor.browserLabel : descriptor.displayName;
}

void DeviceCatalog::registerBuiltInTypes(te::Engine& engine)
{
    // Only Rhino's own devices need teaching to the engine; Tracktion's
    // built-ins are already known to it, and Forge arrives as a VST3.
    auto& plugins = engine.getPluginManager();
    plugins.createBuiltInType<UtilityDevice>();
    plugins.createBuiltInType<DrumDevice>();
    plugins.createBuiltInType<RhinoSpaceDevice>();
    plugins.createBuiltInType<RhinoBloomDevice>();
    plugins.createBuiltInType<RhinoEqDevice>();
    plugins.createBuiltInType<AutoTuneDevice>();
    plugins.createBuiltInType<VocoderDevice>();
    plugins.createBuiltInType<RhinoArpDevice>();
    plugins.createBuiltInType<RhinoWaveDevice>();
}
}
