#include "DeviceEditorPanel.h"
#include "ThetaSpaceDevice.h"
#include <algorithm>
#include <cmath>

namespace theta
{
namespace
{
void styleAutomationButton(juce::TextButton& button, const Session::DeviceParameter& parameter)
{
    button.setButtonText("A");
    button.setEnabled(parameter.automated);
    const auto fill = !parameter.automated ? juce::Colour(0xff242a30)
        : parameter.automationOverridden ? juce::Colour(0xff8a6a2e)
        : juce::Colour(0xff2f7d55);
    const auto text = parameter.automated ? juce::Colour(0xffeef5ef) : juce::Colour(0xff63707a);
    button.setColour(juce::TextButton::buttonColourId, fill);
    button.setColour(juce::TextButton::buttonOnColourId, fill);
    button.setColour(juce::TextButton::textColourOffId, text);
    button.setColour(juce::TextButton::textColourOnId, text);
    button.setTooltip(!parameter.automated ? "No automation for this parameter"
        : parameter.automationOverridden ? "Manual override. Click to follow automation"
        : "Following automation. Click to hold manual value");
}

float normalisedValue(const Session::DeviceParameter& parameter)
{
    const auto length = parameter.maximum - parameter.minimum;
    return length > 0.0f ? std::clamp((parameter.value - parameter.minimum) / length, 0.0f, 1.0f) : 0.0f;
}
}

DeviceEditorPanel::DeviceEditorPanel(Session& s) : session(s)
{
    setOpaque(false);
}

void DeviceEditorPanel::setTarget(int nextTrack, int nextPluginSlot, const Session::DeviceSlot* device)
{
    track = nextTrack;
    pluginSlot = nextPluginSlot;
    deviceName = device != nullptr ? device->name : juce::String();
    face = device != nullptr && device->type == ThetaSpaceDevice::xmlTypeName ? Face::ThetaSpace : Face::Generic;
    parameters = session.deviceParameters(track, pluginSlot);
    ensureControls();
    styleControls();
    resized();
    repaint();
}

int DeviceEditorPanel::visibleParameterCount() const
{
    return std::min(12, static_cast<int>(parameters.size()));
}

void DeviceEditorPanel::ensureControls()
{
    while (parameterLabels.size() < visibleParameterCount())
    {
        const auto index = parameterLabels.size();
        auto* name = parameterLabels.add(new juce::Label());
        auto* value = parameterValues.add(new juce::Label());
        auto* slider = parameterSliders.add(new juce::Slider());
        auto* automation = parameterAutomation.add(new juce::TextButton());
        name->setFont(juce::FontOptions(11.5f));
        name->setJustificationType(juce::Justification::centred);
        value->setFont(juce::FontOptions(10.5f));
        value->setJustificationType(juce::Justification::centred);
        slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider->onDragStart = [this, index]
        {
            const auto result = session.beginDeviceParameterGesture(track, pluginSlot, index);
            if (result.failed() && status) status(result.getErrorMessage());
        };
        slider->onValueChange = [this, index, slider]
        {
            if (syncing) return;
            const auto result = session.setDeviceParameter(track, pluginSlot, index, static_cast<float>(slider->getValue()));
            if (result.failed() && status) status(result.getErrorMessage());
        };
        slider->onDragEnd = [this, index]
        {
            const auto result = session.endDeviceParameterGesture(track, pluginSlot, index);
            if (result.failed() && status) status(result.getErrorMessage());
        };
        automation->onClick = [this, index]
        {
            const auto result = session.toggleParameterAutomationOverride(track, pluginSlot, index);
            if (status) status(result.wasOk() ? "Toggled parameter automation" : result.getErrorMessage());
        };
        addAndMakeVisible(name);
        addAndMakeVisible(value);
        addAndMakeVisible(slider);
        addAndMakeVisible(automation);
    }
}

void DeviceEditorPanel::styleControls()
{
    const auto count = visibleParameterCount();
    syncing = true;
    for (int i = 0; i < parameterLabels.size(); ++i)
    {
        const auto visible = i < count;
        parameterLabels[i]->setVisible(visible);
        parameterValues[i]->setVisible(visible);
        parameterSliders[i]->setVisible(visible);
        parameterAutomation[i]->setVisible(visible);
        if (!visible) continue;

        const auto& parameter = parameters[static_cast<size_t>(i)];
        const auto accent = face == Face::ThetaSpace ? juce::Colour(0xff75b9cc) : juce::Colour(0xffc6d58c);
        parameterLabels[i]->setText(parameter.name, juce::dontSendNotification);
        parameterLabels[i]->setColour(juce::Label::textColourId, juce::Colour(0xffdfe6ea));
        parameterValues[i]->setText(parameter.valueText, juce::dontSendNotification);
        parameterValues[i]->setColour(juce::Label::textColourId, juce::Colour(0xffaebbc3));
        parameterSliders[i]->setRange(parameter.minimum, parameter.maximum, parameter.discrete ? 1.0 : 0.0);
        parameterSliders[i]->setValue(parameter.value, juce::dontSendNotification);
        parameterSliders[i]->setTooltip(parameter.name + ": " + parameter.valueText);
        parameterSliders[i]->setColour(juce::Slider::trackColourId, accent);
        parameterSliders[i]->setColour(juce::Slider::rotarySliderFillColourId, accent);
        parameterSliders[i]->setColour(juce::Slider::backgroundColourId, juce::Colour(0xff20282e));
        parameterSliders[i]->setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff2d3940));
        parameterSliders[i]->setColour(juce::Slider::thumbColourId, accent.brighter(0.28f));
        styleAutomationButton(*parameterAutomation[i], parameter);
    }
    syncing = false;
}

void DeviceEditorPanel::paint(juce::Graphics& g)
{
    if (parameters.empty())
    {
        g.setColour(juce::Colour(0xff75818a));
        g.setFont(juce::FontOptions(12.0f));
        g.drawText(deviceName.isEmpty() ? "Select a device" : "This device has no exposed parameters",
                   getLocalBounds(), juce::Justification::centred);
        return;
    }

    if (face != Face::ThetaSpace)
        return;

    const auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff171d22));
    g.fillRoundedRectangle(bounds, 5.0f);
    g.setColour(juce::Colour(0xff344149));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 5.0f, 1.0f);

    if (!visualArea.isEmpty())
    {
        const auto area = visualArea.toFloat().reduced(8.0f);
        g.setColour(juce::Colour(0xff1c252b));
        g.fillRoundedRectangle(area, 4.0f);
        g.setColour(juce::Colour(0xff566872));
        g.drawRoundedRectangle(area, 4.0f, 1.0f);
        const auto centre = area.getCentre().translated(0.0f, 5.0f);
        const auto sizeValue = parameters.size() > 1 ? normalisedValue(parameters[1]) : 0.5f;
        const auto smearValue = parameters.size() > 2 ? normalisedValue(parameters[2]) : 0.3f;
        const auto radius = std::min(area.getWidth(), area.getHeight()) * (0.16f + sizeValue * 0.25f);
        for (int ring = 3; ring >= 0; --ring)
        {
            const auto expansion = radius * (0.55f + ring * 0.32f + smearValue * 0.25f);
            g.setColour(juce::Colour(0xff75b9cc).withAlpha(0.16f + (3 - ring) * 0.12f));
            g.drawEllipse(centre.x - expansion, centre.y - expansion * 0.56f,
                          expansion * 2.0f, expansion * 1.12f, 1.2f);
        }
        g.setColour(juce::Colour(0xffc6d58c));
        g.fillEllipse(centre.x - 3.0f, centre.y - 3.0f, 6.0f, 6.0f);
        g.setColour(juce::Colour(0xff89a0ac));
        g.setFont(juce::FontOptions(9.5f).withStyle("Bold"));
        g.drawText("SPACE FIELD", visualArea.withHeight(18), juce::Justification::centred);
    }
}

void DeviceEditorPanel::resized()
{
    if (face == Face::ThetaSpace && getWidth() >= 700 && getHeight() >= 100)
        layoutThetaSpace();
    else
        layoutGeneric();
}

void DeviceEditorPanel::layoutGeneric()
{
    visualArea = {};
    const auto count = visibleParameterCount();
    const auto columns = std::max(1, std::min(count, getWidth() >= 900 ? 6 : getWidth() >= 560 ? 4 : 3));
    const auto rows = count > 0 ? (count + columns - 1) / columns : 1;
    const auto cellWidth = getWidth() / columns;
    const auto cellHeight = getHeight() / rows;
    for (int i = 0; i < count; ++i)
    {
        const juce::Rectangle<int> cell((i % columns) * cellWidth, (i / columns) * cellHeight,
                                        cellWidth, cellHeight);
        const auto knobSize = std::min({64, std::max(34, cell.getWidth() - 28), std::max(34, cell.getHeight() - 36)});
        parameterLabels[i]->setBounds(cell.getX() + 4, cell.getY(), cell.getWidth() - 8, 18);
        parameterSliders[i]->setBounds(cell.withSizeKeepingCentre(knobSize, knobSize).translated(0, 4));
        parameterValues[i]->setBounds(cell.getX() + 4, cell.getBottom() - 20, cell.getWidth() - 8, 18);
        parameterAutomation[i]->setBounds(parameterSliders[i]->getRight() - 10, parameterSliders[i]->getY() - 2, 20, 18);
    }
}

void DeviceEditorPanel::layoutThetaSpace()
{
    auto bounds = getLocalBounds().reduced(6);
    visualArea = bounds.removeFromLeft(juce::jlimit(142, 220, getWidth() / 5));
    bounds.removeFromLeft(8);
    const auto count = visibleParameterCount();
    const auto cellWidth = count > 0 ? bounds.getWidth() / count : bounds.getWidth();
    for (int i = 0; i < count; ++i)
    {
        const juce::Rectangle<int> cell(bounds.getX() + i * cellWidth, bounds.getY(), cellWidth, bounds.getHeight());
        const auto knobSize = std::min({66, std::max(38, cell.getWidth() - 22), std::max(38, cell.getHeight() - 38)});
        parameterLabels[i]->setBounds(cell.getX() + 3, cell.getY() + 2, cell.getWidth() - 6, 18);
        parameterSliders[i]->setBounds(cell.withSizeKeepingCentre(knobSize, knobSize).translated(0, 5));
        parameterValues[i]->setBounds(cell.getX() + 3, cell.getBottom() - 20, cell.getWidth() - 6, 18);
        parameterAutomation[i]->setBounds(parameterSliders[i]->getRight() - 9, parameterSliders[i]->getY() - 2, 19, 17);
    }
}
}
