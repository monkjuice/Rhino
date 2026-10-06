#include "DevicePreset.h"
#include "ExactFloatText.h"

namespace rhino
{
namespace
{
const juce::Identifier presetTag { "RHINO_PRESET" };
const juce::Identifier parameterTag { "PARAM" };
}

std::optional<float> DevicePreset::valueOf(const juce::String& parameterId) const
{
    for (const auto& [id, value] : values)
        if (id == parameterId)
            return value;
    return std::nullopt;
}

std::unique_ptr<juce::XmlElement> DevicePreset::toXml() const
{
    auto xml = std::make_unique<juce::XmlElement>(presetTag);
    xml->setAttribute("format", format);
    xml->setAttribute("device", deviceId);
    for (const auto& [id, value] : values)
    {
        auto* parameter = xml->createNewChildElement(parameterTag.toString());
        parameter->setAttribute("id", id);
        parameter->setAttribute("value", exactFloatText(value));
    }
    return xml;
}

juce::Result DevicePreset::fromXml(const juce::XmlElement& xml, DevicePreset& into)
{
    if (!xml.hasTagName(presetTag.toString()))
        return juce::Result::fail("This is not a Rhino preset.");
    if (xml.getIntAttribute("format") != format)
        return juce::Result::fail("This preset was saved in format " + xml.getStringAttribute("format")
                                  + ", and this Rhino reads format " + juce::String(format) + ".");
    DevicePreset preset;
    preset.deviceId = xml.getStringAttribute("device");
    if (preset.deviceId.isEmpty())
        return juce::Result::fail("This preset does not say which device it is for.");
    for (const auto* parameter : xml.getChildWithTagNameIterator(parameterTag.toString()))
    {
        const auto id = parameter->getStringAttribute("id");
        const auto value = exactFloatFrom(parameter->getStringAttribute("value"));
        if (id.isEmpty() || !value.has_value())
            return juce::Result::fail("This preset has a setting with no name or no number for its value.");
        if (preset.valueOf(id).has_value())
            return juce::Result::fail("This preset sets " + id + " twice.");
        preset.values.emplace_back(id, *value);
    }
    into = std::move(preset);
    return juce::Result::ok();
}

juce::Result DevicePreset::write(const juce::File& file) const
{
    if (const auto made = file.getParentDirectory().createDirectory(); made.failed())
        return juce::Result::fail("The preset folder could not be made: " + made.getErrorMessage());
    if (!toXml()->writeTo(file))
        return juce::Result::fail("The preset could not be written to " + file.getFullPathName() + ".");
    return juce::Result::ok();
}

juce::Result DevicePreset::read(const juce::File& file, DevicePreset& into)
{
    if (!file.existsAsFile())
        return juce::Result::fail("That preset file is missing.");
    const auto xml = juce::parseXML(file);
    if (xml == nullptr)
        return juce::Result::fail(DevicePreset::nameOf(file) + " is not a readable preset.");
    return fromXml(*xml, into);
}
}
