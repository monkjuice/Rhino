#include "BrowserDrops.h"

// The chain's drops: see BrowserDrops.h.

namespace rhino
{
std::optional<juce::Result> applyChainDrop(Session& session, const juce::String& description, int track)
{
    const auto kind = browserDropKind(description);
    const auto id = browserDropId(description);
    const auto device = [&session, &id, track](DeviceKind deviceKind)
    {
        const auto* found = deviceFromId(id, deviceKind);
        return found == nullptr ? juce::Result::fail("That browser item cannot be inserted here.")
                                : session.addDevice(found->id, track);
    };
    // An instrument changes what the track runs and never its clips.
    if (kind == "instrument") return device(DeviceKind::Instrument);
    if (kind == "effect") return device(DeviceKind::AudioEffect);
    if (kind == "midi-effect") return device(DeviceKind::MidiEffect);
    if (kind == "drumkit") return session.addDrumKit(browserDropKitFile(description), track);
    if (kind == "drum-preset") return session.addDrumSound(browserDropDrumPresetFile(description), track);
    if (kind == "device-preset") return session.addDeviceFromPreset(browserDropPresetFile(description), track);
    if (kind == "file" && session.trackHasDrumRack(track))
    {
        const auto file = browserDropFile(description);
        if (file == juce::File() || !file.existsAsFile())
            return juce::Result::fail("That audio file is missing.");
        return session.addDrumSound(file, track);
    }
    return std::nullopt;
}
}
