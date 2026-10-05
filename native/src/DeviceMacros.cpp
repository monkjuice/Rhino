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

namespace
{
// The unit a macro prints where the engine would not print it itself. An empty
// answer means the parameter formats its own value. Kept apart from the
// formatter below because the reading for an arbitrary value needs the same
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
}

juce::String formatFourOscMacroValue(int index, float value, te::AutomatableParameter& parameter)
{
    const auto units = fourOscMacroUnits(index, value);
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
    const auto text = parameter.valueToString(value);
    const auto label = parameter.getLabel();
    return label.isEmpty() ? text : text + " " + label;
}

te::AutomatableParameter* exposedParameterAt(te::Plugin& plugin, int index)
{
    if (auto* synthPlugin = dynamic_cast<te::FourOscPlugin*>(&plugin))
        return fourOscMacroParameterAt(*synthPlugin, index);
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
