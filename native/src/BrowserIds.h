#pragma once
#include "Session.h"
#include <optional>

// Browser drag-and-drop descriptions have the form "theta-browser:<kind>:<id>".
// Both the arrangement and Device View accept these drops, so the id tables
// live here rather than once per drop target.

namespace theta
{

inline juce::String browserDropKind(const juce::String& description)
{
    if (!description.startsWith("theta-browser:")) return {};
    return description.fromFirstOccurrenceOf("theta-browser:", false, false)
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
    if (id == "ThetaSpace") return Session::AudioEffect::ThetaSpace;
    if (id == "ThetaBloom") return Session::AudioEffect::ThetaBloom;
    return std::nullopt;
}

inline std::optional<Session::Instrument> instrumentFromId(const juce::String& id)
{
    if (id == "FourOsc")   return Session::Instrument::FourOsc;
    if (id == "ThetaWave") return Session::Instrument::ThetaWave;
    if (id == "ThetaForge") return Session::Instrument::ThetaForge;
    if (id == "Drums")     return Session::Instrument::Drums;
    if (id == "Utility")   return Session::Instrument::Utility;
    return std::nullopt;
}

inline std::optional<DrumDevice::Kit> drumKitFromId(const juce::String& id)
{
    if (id == "Theta808")   return DrumDevice::Kit::Theta808;
    if (id == "HouseKit")   return DrumDevice::Kit::House;
    if (id == "BreakKit")   return DrumDevice::Kit::Break;
    if (id == "MinimalKit") return DrumDevice::Kit::Minimal;
    if (id == "ClapKit")    return DrumDevice::Kit::Clap;
    return std::nullopt;
}

inline std::optional<Session::MidiEffect> midiEffectFromId(const juce::String& id)
{
    if (id == "ThetaArp") return Session::MidiEffect::ThetaArp;
    return std::nullopt;
}

inline std::optional<Session::BuiltInSample> builtInSampleFromId(const juce::String& id)
{
    if (id == "Whistle") return Session::BuiltInSample::Whistle;
    if (id == "Siren")   return Session::BuiltInSample::Siren;
    return std::nullopt;
}

}
