#include "SessionInternal.h"
#include <algorithm>

// Maps the rack UI parameter indices onto the parameters each device exposes.

namespace rhino
{

te::AutomatableParameter* activeParameterAt(te::Plugin& plugin, int index)
{
    int active = 0;
    for (auto* parameter : plugin.getAutomatableParameters())
        if (parameter != nullptr && parameter->isParameterActive())
        {
            if (active == index)
                return parameter;
            ++active;
        }
    return nullptr;
}

te::AutomatableParameter* fourOscMacroParameterAt(te::FourOscPlugin& synth, int index)
{
    switch (index)
    {
        case 0: return synth.ampAttack;
        case 1: return synth.ampDecay;
        case 2: return synth.ampSustain;
        case 3: return synth.ampRelease;
        case 4: return synth.filterFreq;
        case 5: return synth.legato;
    }
    return nullptr;
}

te::AutomatableParameter* rhinoWaveMacroParameterAt(RhinoWaveDevice& wave, int index)
{
    const auto id = [index]() -> const char*
    {
        switch (index)
        {
            case 0: return "position";
            case 1: return "shape";
            case 2: return "motion";
            case 3: return "osc2Level";
            case 4: return "osc2Tune";
            case 5: return "cutoff";
            case 6: return "filterEnv";
            case 7: return "driveDb";
            case 8: return "sub";
            case 9: return "resonance";
            case 10: return "attack";
            case 11: return "decay";
            case 12: return "sustain";
            case 13: return "release";
            case 14: return "unison";
            case 15: return "detune";
            case 16: return "width";
            case 17: return "outputDb";
            case 18: return "lfoRate";
            case 19: return "lfoPosition";
            case 20: return "lfoCutoff";
            case 21: return "lfoPitch";
            case 22: return "lfoMotion";
            default: return nullptr;
        }
    }();
    return id != nullptr ? wave.getAutomatableParameterByID(id) : nullptr;
}

juce::String fourOscMacroName(int index)
{
    switch (index)
    {
        case 0: return "Attack";
        case 1: return "Decay";
        case 2: return "Sustain";
        case 3: return "Release";
        case 4: return "Filter";
        case 5: return "Glide";
    }
    return {};
}

juce::String rhinoWaveMacroName(int index)
{
    switch (index)
    {
        case 0: return "Position";
        case 1: return "Shape";
        case 2: return "Motion";
        case 3: return "Osc 2";
        case 4: return "Tune 2";
        case 5: return "Cutoff";
        case 6: return "Env";
        case 7: return "Drive";
        case 8: return "Sub";
        case 9: return "Resonance";
        case 10: return "Attack";
        case 11: return "Decay";
        case 12: return "Sustain";
        case 13: return "Release";
        case 14: return "Unison";
        case 15: return "Detune";
        case 16: return "Width";
        case 17: return "Output";
        case 18: return "LFO Rate";
        case 19: return "LFO Pos";
        case 20: return "LFO Cutoff";
        case 21: return "LFO Pitch";
        case 22: return "LFO Motion";
    }
    return {};
}

namespace
{
// The unit a macro prints where the engine would not print it itself. An empty
// answer means the parameter formats its own value. Kept apart from the two
// formatters below because the reading for an arbitrary value needs the same
// units as the reading for the one the knob is holding, and spelling them
// twice is how the two would drift.
juce::String fourOscMacroUnits(int index, float value)
{
    switch (index)
    {
        case 0:
        case 1:
        case 3:
            return juce::String(juce::roundToInt(value * 1000.0f)) + "ms";
    }
    return {};
}

juce::String rhinoWaveMacroUnits(int index, float value)
{
    if (index == 4)
        return juce::String(value > 0.0f ? "+" : "") + juce::String(juce::roundToInt(value)) + " st";
    return {};
}
}

juce::String formatFourOscMacroValue(int index, float value, te::AutomatableParameter& parameter)
{
    const auto units = fourOscMacroUnits(index, value);
    return units.isNotEmpty() ? units : parameter.getCurrentValueAsStringWithLabel();
}

juce::String formatRhinoWaveMacroValue(int index, float value, te::AutomatableParameter& parameter)
{
    const auto units = rhinoWaveMacroUnits(index, value);
    return units.isNotEmpty() ? units : parameter.getCurrentValueAsStringWithLabel();
}

// A value the parameter is not currently holding, read the way its knob would
// read it. Tracktion only formats the value a parameter is on, so a point
// further along an automation curve has to be converted by hand - which is
// what the arrangement's hover readout prints.
juce::String formatExposedParameterValue(te::Plugin& plugin, int index, float value,
                                         te::AutomatableParameter& parameter)
{
    if (dynamic_cast<te::FourOscPlugin*>(&plugin) != nullptr)
        if (const auto units = fourOscMacroUnits(index, value); units.isNotEmpty())
            return units;
    if (dynamic_cast<RhinoWaveDevice*>(&plugin) != nullptr)
        if (const auto units = rhinoWaveMacroUnits(index, value); units.isNotEmpty())
            return units;
    const auto text = parameter.valueToString(value);
    const auto label = parameter.getLabel();
    return label.isEmpty() ? text : text + " " + label;
}

te::AutomatableParameter* exposedParameterAt(te::Plugin& plugin, int index)
{
    if (auto* synthPlugin = dynamic_cast<te::FourOscPlugin*>(&plugin))
        return fourOscMacroParameterAt(*synthPlugin, index);
    if (auto* wavePlugin = dynamic_cast<RhinoWaveDevice*>(&plugin))
        return rhinoWaveMacroParameterAt(*wavePlugin, index);
    return activeParameterAt(plugin, index);
}

float exposedParameterMaximum(te::Plugin& plugin, int index, float maximum)
{
    // 4OSC exposes a 60-second amp attack internally.  That makes the rack
    // control impractical, so Rhino presents the musically useful first 6 s.
    if (dynamic_cast<te::FourOscPlugin*>(&plugin) != nullptr && index == 0)
        return std::min(maximum, 6.0f);
    return maximum;
}

}
