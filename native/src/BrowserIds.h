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

inline std::optional<Session::AudioEffect> audioEffectFromId(const juce::String& id)
{
    if (id == "Equaliser")  return Session::AudioEffect::Equaliser;
    if (id == "Reverb")     return Session::AudioEffect::Reverb;
    if (id == "Delay")      return Session::AudioEffect::Delay;
    if (id == "Compressor") return Session::AudioEffect::Compressor;
    if (id == "RhinoSpace") return Session::AudioEffect::RhinoSpace;
    if (id == "RhinoBloom") return Session::AudioEffect::RhinoBloom;
    return std::nullopt;
}

inline std::optional<Session::Instrument> instrumentFromId(const juce::String& id)
{
    if (id == "FourOsc")   return Session::Instrument::FourOsc;
    if (id == "RhinoWave") return Session::Instrument::RhinoWave;
    if (id == "RhinoForge") return Session::Instrument::RhinoForge;
    if (id == "Drums")     return Session::Instrument::Drums;
    if (id == "Utility")   return Session::Instrument::Utility;
    return std::nullopt;
}

inline std::optional<DrumDevice::Kit> drumKitFromId(const juce::String& id)
{
    if (id == "Rhino808")   return DrumDevice::Kit::Rhino808;
    if (id == "HouseKit")   return DrumDevice::Kit::House;
    if (id == "BreakKit")   return DrumDevice::Kit::Break;
    if (id == "MinimalKit") return DrumDevice::Kit::Minimal;
    if (id == "ClapKit")    return DrumDevice::Kit::Clap;
    return std::nullopt;
}

inline std::optional<Session::MidiEffect> midiEffectFromId(const juce::String& id)
{
    if (id == "RhinoArp") return Session::MidiEffect::RhinoArp;
    return std::nullopt;
}

inline std::optional<Session::BuiltInSample> builtInSampleFromId(const juce::String& id)
{
    if (id == "Whistle") return Session::BuiltInSample::Whistle;
    if (id == "Siren")   return Session::BuiltInSample::Siren;
    return std::nullopt;
}

}
