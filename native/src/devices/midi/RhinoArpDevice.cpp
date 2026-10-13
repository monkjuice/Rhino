#include "RhinoArpDevice.h"
#include "dsp/ScaleQuantizer.h"
#include <algorithm>

namespace rhino
{
namespace
{
constexpr std::array<const char*, 5> styleNames {"Up", "Down", "UpDown", "DownUp", "Chord"};
constexpr std::array<const char*, 5> rateNames {"1/8", "1/16", "1/32", "1/8T", "1/16T"};
constexpr std::array<const char*, 3> grooveNames {"Straight", "Swing 8", "Swing 16"};
constexpr std::array<const char*, 3> retriggerNames {"Off", "Note", "Beat"};
constexpr std::array<const char*, 4> intervalNames {"1/4", "1/2", "1 bar", "2 bars"};
constexpr std::array<const char*, 2> rateModeNames {"Beats", "Milliseconds"};
constexpr std::array<const char*, 12> rootNames {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
}

int RhinoArpDevice::parameterChoiceCount(int parameterIndex)
{
    switch (parameterIndex)
    {
        case styleParameter: return static_cast<int>(styleNames.size());
        case rateParameter: return static_cast<int>(rateNames.size());
        case stepsParameter: return 9;
        case offsetParameter: return 16;
        case grooveParameter: return static_cast<int>(grooveNames.size());
        case retriggerParameter: return static_cast<int>(retriggerNames.size());
        case intervalParameter: return static_cast<int>(intervalNames.size());
        case repeatsParameter: return 9;
        case rootParameter: return static_cast<int>(rootNames.size());
        case scaleParameter: return musicalScaleCount;
        case rateModeParameter: return static_cast<int>(rateModeNames.size());
        default: return 0;
    }
}

juce::String RhinoArpDevice::parameterChoiceName(int parameterIndex, int choice)
{
    const auto count = parameterChoiceCount(parameterIndex);
    const auto selected = juce::jlimit(0, std::max(0, count - 1), choice);
    switch (parameterIndex)
    {
        case styleParameter: return styleNames[static_cast<size_t>(selected)];
        case rateParameter: return rateNames[static_cast<size_t>(selected)];
        case stepsParameter:
        case offsetParameter: return juce::String(selected);
        case grooveParameter: return grooveNames[static_cast<size_t>(selected)];
        case retriggerParameter: return retriggerNames[static_cast<size_t>(selected)];
        case intervalParameter: return intervalNames[static_cast<size_t>(selected)];
        case repeatsParameter: return selected == 0 ? juce::String::fromUTF8("\xe2\x88\x9e")
                                                    : juce::String(selected);
        case rootParameter: return rootNames[static_cast<size_t>(selected)];
        case scaleParameter: return musicalScaleName(static_cast<MusicalScale>(selected));
        case rateModeParameter: return rateModeNames[static_cast<size_t>(selected)];
        default: return {};
    }
}

RhinoArpDevice::RhinoArpDevice(te::PluginCreationInfo info) : Plugin(info)
{
    // A new device plays a straight ascending chord. Pitch correction,
    // expansion, groove and retriggering are deliberate choices.
    style.referTo(state, "style", getUndoManager(), 0.0f);
    rateIndex.referTo(state, "rate", getUndoManager(), 1.0f);
    gatePercent.referTo(state, "gate", getUndoManager(), 84.0f);
    distance.referTo(state, "distance", getUndoManager(), 7.0f);
    steps.referTo(state, "steps", getUndoManager(), 0.0f);
    offset.referTo(state, "offset", getUndoManager(), 0.0f);
    groove.referTo(state, "groove", getUndoManager(), 0.0f);
    hold.referTo(state, "hold", getUndoManager(), 0.0f);
    retrigger.referTo(state, "retrigger", getUndoManager(), 0.0f);
    interval.referTo(state, "interval", getUndoManager(), 1.0f);
    repeats.referTo(state, "repeats", getUndoManager(), 0.0f);
    root.referTo(state, "root", getUndoManager(), 0.0f);
    scale.referTo(state, "scale", getUndoManager(), 0.0f);
    rateMode.referTo(state, "rateMode", getUndoManager(), 0.0f);
    freeRateMilliseconds.referTo(state, "freeRate", getUndoManager(), 125.0f);

    styleParam = addParam("style", "Style", {0.0f, 4.0f, 1.0f});
    rateParam = addParam("rate", "Rate", {0.0f, 4.0f, 1.0f});
    gateParam = addParam("gate", "Gate", {10.0f, 200.0f});
    distanceParam = addParam("distance", "Distance", {-12.0f, 12.0f, 1.0f});
    stepsParam = addParam("steps", "Steps", {0.0f, 8.0f, 1.0f});
    offsetParam = addParam("offset", "Offset", {0.0f, 15.0f, 1.0f});
    grooveParam = addParam("groove", "Groove", {0.0f, 2.0f, 1.0f});
    holdParam = addParam("hold", "Hold", {0.0f, 1.0f, 1.0f});
    retriggerParam = addParam("retrigger", "Retrigger", {0.0f, 2.0f, 1.0f});
    intervalParam = addParam("interval", "Interval", {0.0f, 3.0f, 1.0f});
    repeatsParam = addParam("repeats", "Repeats", {0.0f, 8.0f, 1.0f});
    rootParam = addParam("root", "Root", {0.0f, 11.0f, 1.0f});
    scaleParam = addParam("scale", "Scale", {0.0f, static_cast<float>(musicalScaleCount - 1), 1.0f});
    rateModeParam = addParam("rateMode", "Rate Mode", {0.0f, 1.0f, 1.0f});
    freeRateParam = addParam("freeRate", "Free Rate", {10.0f, 2000.0f, 1.0f});

    styleParam->valueToStringFunction = [] (float value)
    { return parameterChoiceName(styleParameter, juce::roundToInt(value)); };
    rateParam->valueToStringFunction = [] (float value)
    { return parameterChoiceName(rateParameter, juce::roundToInt(value)); };
    gateParam->valueToStringFunction = [] (float value) { return juce::String(juce::roundToInt(value)) + "%"; };
    distanceParam->valueToStringFunction = [this] (float value)
    {
        const auto unit = juce::roundToInt(scaleParam->getCurrentValue()) == 0 ? " st" : " sd";
        return juce::String(juce::roundToInt(value)) + unit;
    };
    stepsParam->valueToStringFunction = [] (float value)
    { return parameterChoiceName(stepsParameter, juce::roundToInt(value)); };
    offsetParam->valueToStringFunction = [] (float value)
    { return parameterChoiceName(offsetParameter, juce::roundToInt(value)); };
    grooveParam->valueToStringFunction = [] (float value)
    { return parameterChoiceName(grooveParameter, juce::roundToInt(value)); };
    holdParam->valueToStringFunction = [] (float value) { return value >= 0.5f ? "On" : "Off"; };
    retriggerParam->valueToStringFunction = [] (float value)
    { return parameterChoiceName(retriggerParameter, juce::roundToInt(value)); };
    intervalParam->valueToStringFunction = [] (float value)
    { return parameterChoiceName(intervalParameter, juce::roundToInt(value)); };
    repeatsParam->valueToStringFunction = [] (float value)
    { return parameterChoiceName(repeatsParameter, juce::roundToInt(value)); };
    rootParam->valueToStringFunction = [] (float value)
    { return parameterChoiceName(rootParameter, juce::roundToInt(value)); };
    scaleParam->valueToStringFunction = [] (float value)
    { return parameterChoiceName(scaleParameter, juce::roundToInt(value)); };
    rateModeParam->valueToStringFunction = [] (float value)
    { return parameterChoiceName(rateModeParameter, juce::roundToInt(value)); };
    freeRateParam->valueToStringFunction = [] (float value)
    { return juce::String(juce::roundToInt(value)) + " ms"; };

    styleParam->attachToCurrentValue(style);
    rateParam->attachToCurrentValue(rateIndex);
    gateParam->attachToCurrentValue(gatePercent);
    distanceParam->attachToCurrentValue(distance);
    stepsParam->attachToCurrentValue(steps);
    offsetParam->attachToCurrentValue(offset);
    grooveParam->attachToCurrentValue(groove);
    holdParam->attachToCurrentValue(hold);
    retriggerParam->attachToCurrentValue(retrigger);
    intervalParam->attachToCurrentValue(interval);
    repeatsParam->attachToCurrentValue(repeats);
    rootParam->attachToCurrentValue(root);
    scaleParam->attachToCurrentValue(scale);
    rateModeParam->attachToCurrentValue(rateMode);
    freeRateParam->attachToCurrentValue(freeRateMilliseconds);
}

RhinoArpDevice::~RhinoArpDevice()
{
    notifyListenersOfDeletion();
    styleParam->detachFromCurrentValue();
    rateParam->detachFromCurrentValue();
    gateParam->detachFromCurrentValue();
    distanceParam->detachFromCurrentValue();
    stepsParam->detachFromCurrentValue();
    offsetParam->detachFromCurrentValue();
    grooveParam->detachFromCurrentValue();
    holdParam->detachFromCurrentValue();
    retriggerParam->detachFromCurrentValue();
    intervalParam->detachFromCurrentValue();
    repeatsParam->detachFromCurrentValue();
    rootParam->detachFromCurrentValue();
    scaleParam->detachFromCurrentValue();
    rateModeParam->detachFromCurrentValue();
    freeRateParam->detachFromCurrentValue();
}

void RhinoArpDevice::restorePluginStateFromValueTree(const juce::ValueTree& source)
{
    te::copyPropertiesToCachedValues(source, style, rateIndex, gatePercent, distance, steps,
                                     offset, groove, hold, retrigger, interval, repeats, root, scale,
                                     rateMode, freeRateMilliseconds);
    for (auto* parameter : getAutomatableParameters())
        parameter->updateFromAttachedValue();
}
}
