#pragma once
#include "Session.h"
#include <optional>

// Browser drag-and-drop descriptions have the form "rhino-browser:<kind>:<id>".
// Both the arrangement and Device View accept these drops, so the id tables
// live here rather than once per drop target.

namespace rhino
{

inline juce::String browserDropKind(const juce::String& description)
{
    if (!description.startsWith("rhino-browser:")) return {};
    return description.fromFirstOccurrenceOf("rhino-browser:", false, false)
        .upToFirstOccurrenceOf(":", false, false);
}

inline juce::String browserDropId(const juce::String& description)
{
    return description.fromLastOccurrenceOf(":", false, false);
}

inline std::optional<Session::PatternPreset> patternPresetFromId(const juce::String& id)
{
    if (id == "WarmPulse")  return Session::PatternPreset::WarmPulse;
    if (id == "AcidSteps")  return Session::PatternPreset::AcidSteps;
    if (id == "ArpRun")     return Session::PatternPreset::ArpRun;
    if (id == "ChordPad")   return Session::PatternPreset::ChordPad;
    if (id == "SubBass")    return Session::PatternPreset::SubBass;
    if (id == "ReeseBass")  return Session::PatternPreset::ReeseBass;
    if (id == "SirenLead")  return Session::PatternPreset::SirenLead;
    if (id == "WavePad")    return Session::PatternPreset::WavePad;
    if (id == "WaveBass")   return Session::PatternPreset::WaveBass;
    if (id == "WavePluck")  return Session::PatternPreset::WavePluck;
    if (id == "HouseKit")   return Session::PatternPreset::HouseKit;
    if (id == "BreakKit")   return Session::PatternPreset::BreakKit;
    if (id == "MinimalKit") return Session::PatternPreset::MinimalKit;
    if (id == "ClapKit")    return Session::PatternPreset::ClapKit;
    return std::nullopt;
}

// Devices are resolved through the catalog, so a device added since this was
// written resolves without a line being added here. The kind is checked
// because a drop target accepts one kind and must refuse the others.
inline const DeviceDescriptor* deviceFromId(const juce::String& id, DeviceKind kind)
{
    const auto* device = DeviceCatalog::byId(id);
    return device != nullptr && device->kind == kind ? device : nullptr;
}

inline std::optional<DrumKit> drumKitFromId(const juce::String& id)
{
    if (id == "Rhino808")   return DrumKit::Rhino808;
    if (id == "HouseKit")   return DrumKit::House;
    if (id == "BreakKit")   return DrumKit::Break;
    if (id == "MinimalKit") return DrumKit::Minimal;
    if (id == "ClapKit")    return DrumKit::Clap;
    return std::nullopt;
}

inline std::optional<Session::BuiltInSample> builtInSampleFromId(const juce::String& id)
{
    if (id == "Whistle") return Session::BuiltInSample::Whistle;
    if (id == "Siren")   return Session::BuiltInSample::Siren;
    return std::nullopt;
}

}
