#include "DuckProcessor.h"

// Every parameter Rhino Duck has, and what each reads as. Formatting belongs
// to the parameter so a host's automation lane and the panel show the same
// text.
namespace rhino::duck
{
namespace
{
juce::String asPercent(float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + "%"; }

juce::String asHertz(float value, int)
{
    return value >= 1000.0f ? juce::String(value / 1000.0f, 2) + " kHz" : juce::String(juce::roundToInt(value)) + " Hz";
}

std::unique_ptr<juce::RangedAudioParameter> number(const char* id, const char* name, juce::NormalisableRange<float> range,
                                                   float initial, juce::String (*format)(float, int))
{
    return std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, name, range, initial,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(format));
}

std::unique_ptr<juce::RangedAudioParameter> choice(const char* id, const char* name, juce::StringArray names, int initial)
{
    return std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { id, 1 }, name, std::move(names), initial);
}

std::unique_ptr<juce::RangedAudioParameter> toggle(const char* id, const char* name, bool initial)
{
    return std::make_unique<juce::AudioParameterBool>(juce::ParameterID { id, 1 }, name, initial);
}
}

juce::AudioProcessorValueTreeState::ParameterLayout Processor::parameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> result;

    result.push_back(number(id::mix, "Mix", { 0.0f, 1.0f, 0.0f }, 1.0f, asPercent));

    juce::StringArray curveNames;
    for (int curve = 0; curve < curveCount; ++curve)
        curveNames.add(juce::String(curve + 1) + " " + curveName(curve));
    result.push_back(choice(id::curve, "Curve", curveNames, 0));

    juce::StringArray lengths;
    for (const auto* name : lengthNames) lengths.add(name);
    result.push_back(choice(id::length, "Length", lengths, 1));

    result.push_back(choice(id::trigger, "Trigger", { "Sync", "MIDI", "Audio" }, 0));
    result.push_back(toggle(id::loop, "Loop", true));
    result.push_back(number(id::end, "End", { 0.0f, 1.0f, 0.0f }, 1.0f, asPercent));
    result.push_back(toggle(id::band, "Band split", false));

    juce::NormalisableRange<float> cutoffRange(cutoffMinHz, cutoffMaxHz, 0.0f);
    cutoffRange.setSkewForCentre(320.0f);
    result.push_back(number(id::cutoff, "Band cutoff", cutoffRange, 200.0f, asHertz));
    result.push_back(toggle(id::bandSolo, "Band solo", false));
    result.push_back(toggle(id::antiClick, "Anti-click smoothing", true));

    juce::StringArray channels { "All" };
    for (int channel = 1; channel <= 16; ++channel) channels.add(juce::String(channel));
    result.push_back(choice(id::midiChannel, "MIDI channel", channels, 0));

    juce::StringArray notes { "All" };
    for (int note = 0; note < 128; ++note) notes.add(juce::MidiMessage::getMidiNoteName(note, true, true, 3));
    result.push_back(choice(id::midiNote, "MIDI note", notes, 0));

    return { result.begin(), result.end() };
}
}
