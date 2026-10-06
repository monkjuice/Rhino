#include "sdk/NativeDevice.h"
#include <cmath>

namespace rhino
{
namespace
{
// Enough places to see a change made by a small drag, and no more.
int placesFor(const ParamSpec& spec)
{
    if (spec.interval >= 1.0f)
        return 0;
    const auto span = std::abs(spec.maximum - spec.minimum);
    return span <= 2.0f ? 2 : span <= 20.0f ? 1 : 0;
}

juce::String signedText(float value, int places)
{
    return (value > 0.0f ? "+" : "") + juce::String(value, places);
}
}

float ParamSpec::clamp(float value) const
{
    if (!std::isfinite(value))
        return defaultValue;
    auto clamped = juce::jlimit(minimum, maximum, value);
    if (interval > 0.0f)
        clamped = juce::jlimit(minimum, maximum, minimum + std::round((clamped - minimum) / interval) * interval);
    return clamped;
}

juce::String ParamSpec::text(float value) const
{
    if (format)
        return format(value);
    if (isChoice())
        return choices[juce::jlimit(0, choices.size() - 1, juce::roundToInt(value))];
    if (toggle)
        return value >= 0.5f ? "On" : "Off";
    switch (unit)
    {
        case ParamUnit::percent:
            return juce::String(juce::roundToInt(value * 100.0f)) + "%";
        case ParamUnit::decibels:
            return juce::String(value, 1) + " dB";
        case ParamUnit::hertz:
            return value < 1000.0f ? juce::String(value, value < 100.0f ? 1 : 0) + " Hz"
                                   : juce::String(value / 1000.0f, 2) + " kHz";
        case ParamUnit::seconds:
            return value < 1.0f ? juce::String(juce::roundToInt(value * 1000.0f)) + " ms"
                                : juce::String(value, 2) + " s";
        case ParamUnit::milliseconds:
            return value < 1000.0f ? juce::String(juce::roundToInt(value)) + " ms"
                                   : juce::String(value / 1000.0f, 2) + " s";
        case ParamUnit::semitones:
            return signedText(value, interval >= 1.0f ? 0 : 1) + " st";
        case ParamUnit::ratio:
            return juce::String(value, 2) + "x";
        case ParamUnit::none:
            break;
    }
    return juce::String(value, placesFor(*this));
}

float ParamSpec::parse(const juce::String& reading) const
{
    const auto trimmed = reading.trim();
    if (isChoice())
    {
        const auto position = choices.indexOf(trimmed, true);
        return position >= 0 ? static_cast<float>(position) : clamp(trimmed.getFloatValue());
    }
    if (toggle)
    {
        if (trimmed.equalsIgnoreCase("on"))
            return 1.0f;
        if (trimmed.equalsIgnoreCase("off"))
            return 0.0f;
    }
    auto value = trimmed.getFloatValue();
    const auto lower = trimmed.toLowerCase();
    switch (unit)
    {
        case ParamUnit::percent:
            value /= 100.0f;
            break;
        case ParamUnit::hertz:
            if (lower.contains("k"))
                value *= 1000.0f;
            break;
        case ParamUnit::seconds:
            if (lower.contains("ms"))
                value /= 1000.0f;
            break;
        case ParamUnit::milliseconds:
            if (lower.endsWithChar('s') && !lower.contains("ms"))
                value *= 1000.0f;
            break;
        case ParamUnit::none:
        case ParamUnit::decibels:
        case ParamUnit::semitones:
        case ParamUnit::ratio:
            break;
    }
    return clamp(value);
}

float Param::value() const noexcept
{
    return slot->parameter->getCurrentValue();
}

int Param::index() const noexcept
{
    return juce::roundToInt(value());
}

bool Param::on() const noexcept
{
    return value() >= 0.5f;
}

float Param::smoothed() noexcept
{
    return slot->spec.smoothingSeconds > 0.0f ? slot->smoother.getNextValue() : value();
}

const ParamSpec& Param::spec() const
{
    return slot->spec;
}

te::AutomatableParameter& Param::automatable() const
{
    return *slot->parameter;
}

ParamBuilder::ParamBuilder(NativeDevice& owner, juce::String id, juce::String name) : device(owner)
{
    spec.id = std::move(id);
    spec.name = std::move(name);
}

ParamBuilder& ParamBuilder::range(float minimum, float maximum, float interval)
{
    spec.minimum = minimum;
    spec.maximum = maximum;
    spec.interval = interval;
    return *this;
}

ParamBuilder& ParamBuilder::defaultValue(float value)
{
    spec.defaultValue = value;
    return *this;
}

ParamBuilder& ParamBuilder::skewAround(float centre)
{
    spec.skewCentre = centre;
    return *this;
}

ParamBuilder& ParamBuilder::unit(ParamUnit newUnit)
{
    spec.unit = newUnit;
    return *this;
}

ParamBuilder& ParamBuilder::choices(juce::StringArray names, int defaultChoice)
{
    spec.choices = std::move(names);
    spec.minimum = 0.0f;
    spec.maximum = static_cast<float>(std::max(1, spec.choices.size() - 1));
    spec.interval = 1.0f;
    spec.defaultValue = static_cast<float>(defaultChoice);
    return *this;
}

ParamBuilder& ParamBuilder::toggle(bool defaultOn)
{
    spec.toggle = true;
    spec.minimum = 0.0f;
    spec.maximum = 1.0f;
    spec.interval = 1.0f;
    spec.defaultValue = defaultOn ? 1.0f : 0.0f;
    return *this;
}

ParamBuilder& ParamBuilder::section(juce::String name, juce::String tabGroup)
{
    spec.section = std::move(name);
    spec.tabGroup = std::move(tabGroup);
    return *this;
}

ParamBuilder& ParamBuilder::smoothing(float seconds)
{
    spec.smoothingSeconds = seconds;
    return *this;
}

ParamBuilder& ParamBuilder::format(std::function<juce::String(float)> reading)
{
    spec.format = std::move(reading);
    return *this;
}

ParamBuilder::operator Param()
{
    return device.add(std::move(spec));
}
}
