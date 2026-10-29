#include "DeviceCatalog.h"
#include "instruments/DrumRackDevice.h"
#include "instruments/RhinoFmDevice.h"
#include "audio/UtilityDevice.h"
#include "audio/CompressorDevice.h"
#include "audio/RhinoSpaceDevice.h"
#include "audio/RhinoBloomDevice.h"
#include "audio/RhinoEqDevice.h"
#include "audio/AutoTuneDevice.h"
#include "audio/VocoderDevice.h"
#include "audio/AmpDevice.h"
#include "audio/SaturatorDevice.h"
#include "audio/ReduxDevice.h"
#include "audio/ErosionDevice.h"
#include "audio/TunerDevice.h"
#include "midi/RhinoArpDevice.h"
#include "infra/DjTrackPortDevices.h"
#include "infra/ClipEnvelopeDevice.h"
#include "infra/ClipPitchDevice.h"

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

    add({.id = "RhinoFM", .typeName = RhinoFmDevice::xmlTypeName, .displayName = "Rhino FM",
         .kind = DeviceKind::Instrument, .category = "Synths", .description = "Four-operator FM synth",
         .colour = 0xffd08a3c, .browsable = true, .create = factory<RhinoFmDevice>()});

    // Forge is a VST3 discovered by scanning, so it has no type name to
    // create from -- see SessionExternalPlugins.cpp. Its rack shows the
    // controls a synth is played by: the filter, the three oscillators'
    // levels and the amplitude envelope.
    add({.id = "RhinoForge", .displayName = "Rhino Forge",
         .kind = DeviceKind::Instrument, .category = "Synths", .description = "Three-oscillator Forge synth",
         .colour = 0xff3a9aa9, .browsable = true, .external = true,
         .pluginName = "Rhino Forge", .bundle = "Rhino Forge.vst3",
         .developmentArtefacts = "instruments/rhino-forge/build/RhinoForge_artefacts",
         .rackParameters = { "Cutoff", "Resonance", "Osc A Level", "Osc B Level", "Osc C Level",
                             "Env 1 Attack", "Env 1 Decay", "Env 1 Sustain", "Env 1 Release" }});

    // Keeps the id the drum instrument it replaced had, so Instrument::Drums,
    // the drum patterns and the note editor's drum rows still name it. The
    // browser offers it blank at the top of Drums, with its kits below.
    add({.id = "Drums", .typeName = DrumRackDevice::xmlTypeName, .displayName = "Drum Rack",
         .kind = DeviceKind::Instrument, .category = "Drum Rack",
         .description = "A pad on every note: samples, slices and synthesised drums",
         .colour = 0xff9bbf4a, .browsable = true, .create = factory<DrumRackDevice>()});

    // ---- Audio FX ----------------------------------------------------------
    // Keeps the id the Tracktion four-band EQ had: presets, browser drops
    // and Session::AudioEffect all name a device by id, and this is the same
    // slot in the chain with eight bands and a display behind them.
    add({.id = "Equaliser", .typeName = RhinoEqDevice::xmlTypeName,
         .displayName = "Rhino EQ", .browserLabel = "EQ",
         .kind = DeviceKind::AudioEffect, .category = "EQ and Filters", .description = "Eight bands over a live spectrum",
         .colour = 0xff6f9ec4, .browsable = true, .create = factory<RhinoEqDevice>()});

    // Tracktion's compressor, made as Rhino's own so it can say how far it is
    // turning the audio down, which its face shows as a meter.
    add({.id = "Compressor", .typeName = CompressorDevice::xmlTypeName, .displayName = "Compressor",
         .kind = DeviceKind::AudioEffect, .category = "Dynamics", .description = "Tracktion compressor",
         .browsable = true, .create = factory<CompressorDevice>()});

    // Rhino Duck is the second independent plugin project, effects/rhino-duck,
    // a VST3 found the way Forge is. Its rack shows the controls a duck is
    // shaped by, Mix first.
    add({.id = "RhinoDuck", .displayName = "Rhino Duck",
         .kind = DeviceKind::AudioEffect, .category = "Dynamics", .description = "Sidechain ducking on a curve, no compressor",
         .colour = 0xffff8a3d, .browsable = true, .external = true,
         .pluginName = "Rhino Duck", .bundle = "Rhino Duck.vst3",
         .developmentArtefacts = "effects/rhino-duck/build/RhinoDuck_artefacts",
         .rackParameters = { "Mix", "Curve", "Length", "Trigger", "Loop", "End", "Band split", "Band cutoff", "Band solo" }});

    // Every track already carries one as its channel strip, so it is not
    // offered in the browser: a dropped Utility found the track's own and
    // added nothing, and the rack hides the channel strip, so the drop
    // appeared to do nothing at all.
    add({.id = "Utility", .typeName = UtilityDevice::xmlTypeName,
         .displayName = "Utility", .browserLabel = "Utility gain",
         .kind = DeviceKind::AudioEffect, .category = "Dynamics", .description = "Level trim inside a chain",
         .colour = 0xff56636c, .infrastructure = true, .create = factory<UtilityDevice>()});

    // Live's drive devices, after its own: an amp, a waveshaper, a crusher and
    // a delay shaken at audio rate.
    add({.id = "Amp", .typeName = AmpDevice::xmlTypeName, .displayName = "Amp",
         .kind = DeviceKind::AudioEffect, .category = "Distortion", .description = "Seven guitar amplifiers, one set of knobs",
         .colour = 0xffd0643c, .browsable = true, .create = factory<AmpDevice>()});

    add({.id = "Saturator", .typeName = SaturatorDevice::xmlTypeName, .displayName = "Saturator",
         .kind = DeviceKind::AudioEffect, .category = "Distortion", .description = "Waveshaping, from warmth to wavefolding",
         .colour = 0xffe0a040, .browsable = true, .create = factory<SaturatorDevice>()});

    add({.id = "Redux", .typeName = ReduxDevice::xmlTypeName, .displayName = "Redux",
         .kind = DeviceKind::AudioEffect, .category = "Distortion", .description = "Downsampling and bit reduction",
         .colour = 0xff5fb38a, .browsable = true, .create = factory<ReduxDevice>()});

    add({.id = "Erosion", .typeName = ErosionDevice::xmlTypeName, .displayName = "Erosion",
         .kind = DeviceKind::AudioEffect, .category = "Distortion", .description = "Digital degradation from a shaken delay",
         .colour = 0xffb0785a, .browsable = true, .create = factory<ErosionDevice>()});

    add({.id = "Reverb", .typeName = te::ReverbPlugin::xmlTypeName, .displayName = "Reverb",
         .kind = DeviceKind::AudioEffect, .category = "Delay and Reverb", .description = "Tracktion reverb",
         .browsable = true});

    add({.id = "Delay", .typeName = te::DelayPlugin::xmlTypeName, .displayName = "Delay",
         .kind = DeviceKind::AudioEffect, .category = "Delay and Reverb", .description = "Tracktion delay",
         .browsable = true});

    add({.id = "RhinoSpace", .typeName = RhinoSpaceDevice::xmlTypeName, .displayName = "Rhino Space",
         .kind = DeviceKind::AudioEffect, .category = "Rhino", .description = "Floating multi FX: smear, drive, width",
         .colour = 0xff75b9cc, .browsable = true, .create = factory<RhinoSpaceDevice>()});

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

    // A meter rather than an effect: it never changes the audio.
    add({.id = "Tuner", .typeName = TunerDevice::xmlTypeName, .displayName = "Tuner",
         .kind = DeviceKind::AudioEffect, .category = "Utilities", .description = "The pitch of what passes, and how far off it is",
         .colour = 0xff6fc98f, .browsable = true, .create = factory<TunerDevice>()});

    // ---- The DJ booth's hidden ends of a track --------------------------------
    // A deck playing a track of the song is heard through the track itself:
    // these carry the deck's audio and notes in at the chain's head and the
    // chain's output out where the track's own devices end. Infrastructure, as the
    // Utility channel strip is: never offered, never shown in the rack, made
    // with every track and saved with it, and doing nothing until the booth
    // binds a deck to them.
    add({.id = "DjTrackInput", .typeName = DjTrackInjectorDevice::xmlTypeName, .displayName = "DJ Input",
         .kind = DeviceKind::AudioEffect, .description = "Where a DJ deck enters a track",
         .infrastructure = true, .create = factory<DjTrackInjectorDevice>()});
    add({.id = "DjTrackOutput", .typeName = DjTrackTapDevice::xmlTypeName, .displayName = "DJ Output",
         .kind = DeviceKind::AudioEffect, .description = "Where a DJ deck leaves a track",
         .infrastructure = true, .create = factory<DjTrackTapDevice>()});

    // ---- An audio clip's own envelopes ---------------------------------------
    // Play a clip's Gain and Pan envelopes, and its Pitch envelope, from the
    // head of the clip's own plugin list. Infrastructure: the session puts
    // each on a clip while the clip has an envelope for it and takes it off
    // again, and nothing offers them.
    add({.id = "ClipEnvelope", .typeName = ClipEnvelopeDevice::xmlTypeName, .displayName = "Clip Envelope",
         .kind = DeviceKind::AudioEffect, .description = "Plays an audio clip's Gain and Pan envelopes, and its fades",
         .infrastructure = true, .create = factory<ClipEnvelopeDevice>()});
    add({.id = "ClipPitch", .typeName = ClipPitchDevice::xmlTypeName, .displayName = "Clip Pitch",
         .kind = DeviceKind::AudioEffect, .description = "Plays an audio clip's Pitch envelope",
         .infrastructure = true, .create = factory<ClipPitchDevice>()});

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
