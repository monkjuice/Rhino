#include "DeviceCatalog.h"
#include "instruments/DrumDevice.h"
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
template <typename Device>
std::function<te::Plugin::Ptr(te::PluginCreationInfo)> factory()
{
    return [] (te::PluginCreationInfo info) -> te::Plugin::Ptr { return new Device(info); };
}

// Order is the order the browser shows, so keep a new device beside the ones
// it belongs with rather than appending to the end.
std::vector<DeviceDescriptor> buildCatalog()
{
    std::vector<DeviceDescriptor> catalog;

    auto add = [&catalog](DeviceDescriptor descriptor) { catalog.push_back(std::move(descriptor)); };

    // ---- Instruments -------------------------------------------------------
    add({.id = "FourOsc", .typeName = te::FourOscPlugin::xmlTypeName,
         .displayName = "4OSC", .browserLabel = "4OSC synth",
         .kind = DeviceKind::Instrument, .category = "Synths", .description = "Subtractive synth",
         .colour = 0xff3d6f8b, .browsable = true});

    // Forge is a VST3 discovered by scanning, so it has no type name to
    // create from -- see SessionExternalPlugins.cpp.
    add({.id = "RhinoForge", .displayName = "Rhino Forge",
         .kind = DeviceKind::Instrument, .category = "Synths", .description = "Three-oscillator Forge synth",
         .colour = 0xff3a9aa9, .browsable = true, .external = true});

    // The drum rack is reached through its kits, which the browser lists
    // instead of the bare device, so it is not browsable itself.
    add({.id = "Drums", .typeName = DrumDevice::xmlTypeName, .displayName = "Rhino Drums",
         .kind = DeviceKind::Instrument, .category = "Drum Rack", .description = "Sample drum rack",
         .colour = 0xff738044, .create = factory<DrumDevice>()});

    // ---- Audio FX ----------------------------------------------------------
    // Keeps the id the Tracktion four-band EQ had: presets, browser drops
    // and Session::AudioEffect all name a device by id, and this is the same
    // slot in the chain with eight bands and a display behind them.
    add({.id = "Equaliser", .typeName = RhinoEqDevice::xmlTypeName,
         .displayName = "Rhino EQ", .browserLabel = "EQ",
         .kind = DeviceKind::AudioEffect, .category = "EQ and Filters", .description = "Eight bands over a live spectrum",
         .colour = 0xff6f9ec4, .browsable = true, .create = factory<RhinoEqDevice>()});

    add({.id = "Compressor", .typeName = te::CompressorPlugin::xmlTypeName, .displayName = "Compressor",
         .kind = DeviceKind::AudioEffect, .category = "Dynamics", .description = "Tracktion compressor",
         .browsable = true});

    // Every track already carries one as its channel strip, so it is not
    // offered in the browser: a dropped Utility found the track's own and
    // added nothing, and the rack hides the channel strip, so the drop
    // appeared to do nothing at all.
    add({.id = "Utility", .typeName = UtilityDevice::xmlTypeName,
         .displayName = "Utility", .browserLabel = "Utility gain",
         .kind = DeviceKind::AudioEffect, .category = "Dynamics", .description = "Level trim inside a chain",
         .colour = 0xff56636c, .infrastructure = true, .create = factory<UtilityDevice>()});

    add({.id = "Reverb", .typeName = te::ReverbPlugin::xmlTypeName, .displayName = "Reverb",
         .kind = DeviceKind::AudioEffect, .category = "Delay and Reverb", .description = "Tracktion reverb",
         .browsable = true});

    add({.id = "Delay", .typeName = te::DelayPlugin::xmlTypeName, .displayName = "Delay",
         .kind = DeviceKind::AudioEffect, .category = "Delay and Reverb", .description = "Tracktion delay",
         .browsable = true});

    add({.id = "RhinoSpace", .typeName = RhinoSpaceDevice::xmlTypeName, .displayName = "Rhino Space",
         .kind = DeviceKind::AudioEffect, .category = "Rhino", .description = "Floating multi FX: smear, drive, width",
         .browsable = true, .create = factory<RhinoSpaceDevice>()});

    add({.id = "RhinoBloom", .typeName = RhinoBloomDevice::xmlTypeName, .displayName = "Rhino Bloom",
         .kind = DeviceKind::AudioEffect, .category = "Rhino", .description = "Chorus, clouds, plate, colour",
         .browsable = true, .create = factory<RhinoBloomDevice>()});

    add({.id = "RhinoTune", .typeName = AutoTuneDevice::xmlTypeName,
         .displayName = "Rhino Tune", .browserLabel = "Rhino Tune (auto-tune)",
         .kind = DeviceKind::AudioEffect, .category = "Rhino", .description = "Vocal pitch correction, formants and vibrato",
         .colour = 0xffb2739c, .browsable = true, .create = factory<AutoTuneDevice>()});

    add({.id = "RhinoVocoder", .typeName = VocoderDevice::xmlTypeName, .displayName = "Rhino Vocoder",
         .kind = DeviceKind::AudioEffect, .category = "Rhino", .description = "Plays this track's voice with another track's synth",
         .colour = 0xff7d8fc4, .browsable = true, .create = factory<VocoderDevice>()});

    // ---- MIDI FX -----------------------------------------------------------
    add({.id = "RhinoArp", .typeName = RhinoArpDevice::xmlTypeName, .displayName = "Rhino Arp",
         .kind = DeviceKind::MidiEffect, .description = "Bouncing, scale-aware chords before an instrument",
         .colour = 0xff55c7d5, .browsable = true, .create = factory<RhinoArpDevice>()});

    return catalog;
}

// The engine makes a built-in plugin by asking a registered type for its name.
// This one answers with the catalog entry's own factory, so a device's type
// name and the class made for it are written down together, once.
struct CatalogType final : te::PluginManager::BuiltInType
{
    explicit CatalogType(const DeviceDescriptor& entry) : BuiltInType(entry.typeName), descriptor(entry) {}
    te::Plugin::Ptr create(te::PluginCreationInfo info) override { return descriptor.create(info); }
    // The catalog is built once and never changes, so the entry outlives
    // every engine that registers it.
    const DeviceDescriptor& descriptor;
};
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
    for (const auto& descriptor : all())
        if (descriptor.create)
            plugins.registerBuiltInType(std::make_unique<CatalogType>(descriptor));
}
}
