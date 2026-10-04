#include "DeviceEditorPanel.h"
#include "Theme.h"
#include "midi/RhinoArpDevice.h"
#include <algorithm>
#include <array>

namespace rhino
{
namespace
{
float parameterValue(const std::vector<Session::DeviceParameter>& parameters, int index, float fallback)
{
    return juce::isPositiveAndBelow(index, static_cast<int>(parameters.size()))
        ? parameters[static_cast<size_t>(index)].value : fallback;
}

int displayOrdinal(int style, int step)
{
    constexpr std::array<int, 4> upDown {0, 1, 2, 1};
    constexpr std::array<int, 4> downUp {2, 1, 0, 1};
    if (style == 0) return step % 3;
    if (style == 1) return 2 - step % 3;
    if (style == 2) return upDown[static_cast<size_t>(step % 4)];
    if (style == 3) return downUp[static_cast<size_t>(step % 4)];
    return 1;
}

int displayPatternLength(int style)
{
    return style == 2 || style == 3 ? 4 : style == 4 ? 1 : 3;
}

struct ArpLayout
{
    juce::Rectangle<int> visual;
    juce::Rectangle<int> pattern, rate, timing, harmony;
};

ArpLayout arpLayout(juce::Rectangle<int> bounds)
{
    ArpLayout layout;
    layout.visual = bounds.removeFromLeft(202);
    bounds.removeFromLeft(8);
    layout.pattern = bounds.removeFromLeft(160);
    bounds.removeFromLeft(6);
    layout.rate = bounds.removeFromLeft(92);
    bounds.removeFromLeft(6);
    layout.timing = bounds.removeFromLeft(150);
    bounds.removeFromLeft(6);
    layout.harmony = bounds;
    return layout;
}

void drawCaption(juce::Graphics& g, const juce::String& text, juce::Rectangle<int> control)
{
    g.setColour(palette::textDim);
    g.setFont(uiFontBold(7.5f));
    drawSnappedText(g, text.toUpperCase(), control.withY(control.getY() - 11).withHeight(10),
                    juce::Justification::centred);
}
}

void DeviceEditorPanel::ensureArpControls()
{
    if (arpControlsCreated)
        return;
    arpControlsCreated = true;

    constexpr std::array<int, 7> choiceParameters {
        RhinoArpDevice::styleParameter,
        RhinoArpDevice::stepsParameter,
        RhinoArpDevice::offsetParameter,
        RhinoArpDevice::grooveParameter,
        RhinoArpDevice::retriggerParameter,
        RhinoArpDevice::rootParameter,
        RhinoArpDevice::scaleParameter
    };

    for (const auto parameter : choiceParameters)
    {
        auto* choice = arpChoiceControls.add(new juce::ComboBox());
        arpChoiceParameters.push_back(parameter);
        for (int value = 0; value < RhinoArpDevice::parameterChoiceCount(parameter); ++value)
            choice->addItem(RhinoArpDevice::parameterChoiceName(parameter, value), value + 1);
        choice->setJustificationType(juce::Justification::centred);
        choice->onChange = [this, parameter, choice]
        {
            if (syncing || choice->getSelectedId() == 0)
                return;
            const auto started = session.beginDeviceParameterGesture(track, pluginSlot, parameter);
            if (started.failed())
            {
                if (status) status(started.getErrorMessage());
                return;
            }
            const auto changed = session.setDeviceParameter(track, pluginSlot, parameter,
                                                            static_cast<float>(choice->getSelectedId() - 1));
            const auto ended = session.endDeviceParameterGesture(track, pluginSlot, parameter);
            if (changed.failed() && status) status(changed.getErrorMessage());
            else if (ended.failed() && status) status(ended.getErrorMessage());
        };
        addAndMakeVisible(choice);
    }

    arpHold.setClickingTogglesState(true);
    arpHold.onClick = [this]
    {
        constexpr auto parameter = RhinoArpDevice::holdParameter;
        const auto wasOn = juce::isPositiveAndBelow(parameter, static_cast<int>(parameters.size()))
            && parameters[static_cast<size_t>(parameter)].value >= 0.5f;
        setArpButtonParameter(parameter, wasOn ? 0.0f : 1.0f);
    };
    addAndMakeVisible(arpHold);

    arpRateBeats.setName("Beat rate");
    arpRateMilliseconds.setName("Millisecond rate");
    arpRateBeats.setConnectedEdges(juce::Button::ConnectedOnRight);
    arpRateMilliseconds.setConnectedEdges(juce::Button::ConnectedOnLeft);
    arpRateBeats.onClick = [this]
    {
        setArpButtonParameter(RhinoArpDevice::rateModeParameter, 0.0f);
    };
    arpRateMilliseconds.onClick = [this]
    {
        setArpButtonParameter(RhinoArpDevice::rateModeParameter, 1.0f);
    };
    addAndMakeVisible(arpRateBeats);
    addAndMakeVisible(arpRateMilliseconds);
}

void DeviceEditorPanel::setArpButtonParameter(int parameter, float value)
{
    const auto started = session.beginDeviceParameterGesture(track, pluginSlot, parameter);
    if (started.failed())
    {
        if (status) status(started.getErrorMessage());
        return;
    }
    const auto changed = session.setDeviceParameter(track, pluginSlot, parameter, value);
    const auto ended = session.endDeviceParameterGesture(track, pluginSlot, parameter);
    if (changed.failed() && status) status(changed.getErrorMessage());
    else if (ended.failed() && status) status(ended.getErrorMessage());
}

void DeviceEditorPanel::styleArpControls()
{
    if (!arpControlsCreated)
        return;

    syncing = true;
    for (int i = 0; i < arpChoiceControls.size(); ++i)
    {
        const auto parameter = arpChoiceParameters[static_cast<size_t>(i)];
        const auto valid = juce::isPositiveAndBelow(parameter, static_cast<int>(parameters.size()));
        auto* choice = arpChoiceControls[i];
        choice->setVisible(valid);
        if (!valid)
            continue;
        const auto& value = parameters[static_cast<size_t>(parameter)];
        choice->setSelectedId(juce::roundToInt(value.value) + 1, juce::dontSendNotification);
        choice->setTooltip(value.name + ": " + value.valueText + ". Right-click for automation.");
        choice->setColour(juce::ComboBox::backgroundColourId, palette::control);
        choice->setColour(juce::ComboBox::outlineColourId, palette::border);
        choice->setColour(juce::ComboBox::textColourId, palette::text);
        choice->setColour(juce::ComboBox::arrowColourId, palette::midiEffect);
    }

    const auto holdOn = juce::isPositiveAndBelow(RhinoArpDevice::holdParameter,
                                                  static_cast<int>(parameters.size()))
        && parameters[RhinoArpDevice::holdParameter].value >= 0.5f;
    arpHold.setVisible(true);
    arpHold.setToggleState(holdOn, juce::dontSendNotification);
    arpHold.setTooltip("Hold: " + juce::String(holdOn ? "On" : "Off") + ". Right-click for automation.");
    arpHold.setColour(juce::TextButton::buttonColourId, palette::control);
    arpHold.setColour(juce::TextButton::buttonOnColourId, palette::midiEffect.darker(0.62f));
    arpHold.setColour(juce::TextButton::textColourOffId, palette::textDim);
    arpHold.setColour(juce::TextButton::textColourOnId, palette::text);

    const auto beatMode = !juce::isPositiveAndBelow(RhinoArpDevice::rateModeParameter,
                                                     static_cast<int>(parameters.size()))
        || parameters[RhinoArpDevice::rateModeParameter].value < 0.5f;
    const auto styleRateButton = [beatMode] (juce::TextButton& button, bool beats)
    {
        const auto selected = beatMode == beats;
        button.setVisible(true);
        button.setToggleState(selected, juce::dontSendNotification);
        button.setColour(juce::TextButton::buttonColourId, palette::control);
        button.setColour(juce::TextButton::buttonOnColourId, palette::midiEffect.darker(0.62f));
        button.setColour(juce::TextButton::textColourOffId, palette::textDim);
        button.setColour(juce::TextButton::textColourOnId, palette::text);
    };
    styleRateButton(arpRateBeats, true);
    styleRateButton(arpRateMilliseconds, false);
    arpRateBeats.setTooltip("Use tempo-synchronised note divisions. Right-click for automation.");
    arpRateMilliseconds.setTooltip("Use a free rate in milliseconds. Right-click for automation.");
    syncing = false;
}

juce::ComboBox* DeviceEditorPanel::arpChoiceFor(int parameter) const
{
    for (int i = 0; i < arpChoiceControls.size(); ++i)
        if (arpChoiceParameters[static_cast<size_t>(i)] == parameter)
            return arpChoiceControls[i];
    return nullptr;
}

int DeviceEditorPanel::arpParameterForComponent(const juce::Component* component) const
{
    if (component == &arpHold || arpHold.isParentOf(component))
        return RhinoArpDevice::holdParameter;
    if (component == &arpRateBeats || arpRateBeats.isParentOf(component)
        || component == &arpRateMilliseconds || arpRateMilliseconds.isParentOf(component))
        return RhinoArpDevice::rateModeParameter;
    for (int i = 0; i < arpChoiceControls.size(); ++i)
        if (component == arpChoiceControls[i] || arpChoiceControls[i]->isParentOf(component))
            return arpChoiceParameters[static_cast<size_t>(i)];
    return -1;
}

void DeviceEditorPanel::layoutArp()
{
    const auto layout = arpLayout(contentArea);
    visualArea = layout.visual;
    const auto choice = [this] (int parameter, juce::Rectangle<int> bounds)
    {
        if (auto* control = arpChoiceFor(parameter))
            control->setBounds(bounds);
    };
    const auto twoColumns = [] (juce::Rectangle<int> bounds, int row)
    {
        constexpr int inset = 7, gap = 5, rowHeight = 22;
        const auto width = (bounds.getWidth() - inset * 2 - gap) / 2;
        const auto y = bounds.getY() + (row == 0 ? 31 : 87);
        return std::array<juce::Rectangle<int>, 2> {
            juce::Rectangle<int>(bounds.getX() + inset, y, width, rowHeight),
            juce::Rectangle<int>(bounds.getX() + inset + width + gap, y, width, rowHeight)
        };
    };

    constexpr int patternInset = 7, patternGap = 5, wideControl = 88;
    const auto narrowControl = layout.pattern.getWidth() - patternInset * 2 - patternGap - wideControl;
    const auto patternTopY = layout.pattern.getY() + 31;
    const auto patternBottomY = layout.pattern.getY() + 87;
    choice(RhinoArpDevice::styleParameter,
           {layout.pattern.getX() + patternInset, patternTopY, wideControl, 22});
    arpHold.setBounds(layout.pattern.getX() + patternInset + wideControl + patternGap,
                      patternTopY, narrowControl, 22);
    choice(RhinoArpDevice::offsetParameter,
           {layout.pattern.getX() + patternInset, patternBottomY, narrowControl, 22});
    choice(RhinoArpDevice::grooveParameter,
           {layout.pattern.getX() + patternInset + narrowControl + patternGap,
            patternBottomY, wideControl, 22});

    const auto rateButtonWidth = (layout.rate.getWidth() - 23) / 2;
    arpRateBeats.setBounds(layout.rate.getX() + 10, layout.rate.getY() + 22, rateButtonWidth, 16);
    arpRateMilliseconds.setBounds(arpRateBeats.getRight() + 3, arpRateBeats.getY(), rateButtonWidth, 16);
    const auto rateKnob = juce::Rectangle<int>(0, 0, 54, 54)
        .withCentre({layout.rate.getCentreX(), layout.rate.getY() + 76});
    const auto rateParameter = parameters[RhinoArpDevice::rateModeParameter].value < 0.5f
        ? RhinoArpDevice::rateParameter : RhinoArpDevice::freeRateParameter;
    if (juce::isPositiveAndBelow(rateParameter, parameterSliders.size()))
        parameterSliders[rateParameter]->setBounds(rateKnob);

    choice(RhinoArpDevice::retriggerParameter,
           {layout.timing.getX() + 7, layout.timing.getY() + 31,
            layout.timing.getWidth() - 14, 22});
    if (juce::isPositiveAndBelow(RhinoArpDevice::intervalParameter, parameterSliders.size()))
        parameterSliders[RhinoArpDevice::intervalParameter]->setBounds(
            layout.timing.getX() + 15, layout.timing.getY() + 74, 46, 46);
    if (juce::isPositiveAndBelow(RhinoArpDevice::repeatsParameter, parameterSliders.size()))
    {
        parameterSliders[RhinoArpDevice::repeatsParameter]->setBounds(
            layout.timing.getX() + 78, layout.timing.getY() + 88, 63, 20);
        parameterValues[RhinoArpDevice::repeatsParameter]->setBounds(
            parameterSliders[RhinoArpDevice::repeatsParameter]->getBounds());
    }

    const auto harmonyTop = twoColumns(layout.harmony, 0);
    choice(RhinoArpDevice::rootParameter, harmonyTop[0]);
    choice(RhinoArpDevice::scaleParameter, harmonyTop[1]);

    const auto& harmony = layout.harmony;
    choice(RhinoArpDevice::stepsParameter, {harmony.getX() + 7, harmony.getY() + 78, 69, 22});
    const auto placeKnob = [this, &harmony] (int parameter, int x)
    {
        if (!juce::isPositiveAndBelow(parameter, parameterSliders.size()))
            return;
        parameterSliders[parameter]->setBounds(harmony.getX() + x, harmony.getY() + 73, 43, 43);
    };
    placeKnob(RhinoArpDevice::distanceParameter, 82);
    placeKnob(RhinoArpDevice::gateParameter, harmony.getWidth() - 50);
}

void DeviceEditorPanel::paintArp(juce::Graphics& g)
{
    if (visualArea.isEmpty())
        return;

    const auto layout = arpLayout(contentArea);
    const auto panel = visualArea.toFloat().reduced(5.0f);
    g.setColour(palette::displayInset);
    g.fillRoundedRectangle(panel, 4.0f);
    g.setColour(palette::border);
    g.drawRoundedRectangle(panel, 4.0f, 1.0f);

    g.setColour(palette::textDim);
    g.setFont(uiFontBold(8.0f));
    drawSnappedText(g, "SEQUENCE", visualArea.reduced(10, 4).withHeight(14),
                    juce::Justification::centredLeft);

    auto graph = visualArea.reduced(13, 8).withTrimmedTop(18).withTrimmedBottom(18).toFloat();
    const auto style = juce::jlimit(0, 4, juce::roundToInt(parameterValue(
        parameters, RhinoArpDevice::styleParameter, 2.0f)));
    const auto distance = juce::roundToInt(parameterValue(
        parameters, RhinoArpDevice::distanceParameter, -7.0f));
    const auto steps = juce::jlimit(0, 8, juce::roundToInt(parameterValue(
        parameters, RhinoArpDevice::stepsParameter, 2.0f)));
    const auto patternLength = displayPatternLength(style);

    constexpr int points = 12;
    std::array<int, points> pitches {};
    for (int point = 0; point < points; ++point)
    {
        const auto transpose = steps > 0 ? (point / patternLength) % (steps + 1) : 0;
        pitches[static_cast<size_t>(point)] = displayOrdinal(style, point) * 3 + transpose * distance;
    }
    const auto [lowest, highest] = std::minmax_element(pitches.begin(), pitches.end());
    const auto range = std::max(1, *highest - *lowest);
    const auto position = [&] (int point)
    {
        const auto x = graph.getX() + graph.getWidth() * point / static_cast<float>(points - 1);
        const auto normal = (pitches[static_cast<size_t>(point)] - *lowest) / static_cast<float>(range);
        return juce::Point<float>(x, graph.getBottom() - normal * graph.getHeight());
    };

    for (int line = 1; line < 4; ++line)
    {
        const auto y = graph.getY() + graph.getHeight() * line / 4.0f;
        g.setColour(palette::border.withAlpha(0.42f));
        g.drawHorizontalLine(juce::roundToInt(y), graph.getX(), graph.getRight());
    }

    juce::Path path;
    path.startNewSubPath(position(0));
    for (int point = 1; point < points; ++point)
        path.lineTo(position(point));
    g.setColour(palette::midiEffect.withAlpha(0.32f));
    g.strokePath(path, juce::PathStrokeType(1.0f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
    for (int point = 0; point < points; ++point)
    {
        const auto centre = position(point);
        const auto note = juce::Rectangle<float>(4.0f, 11.0f).withCentre(centre);
        g.setColour(palette::midiEffect.withAlpha(point == 0 ? 0.95f : 0.74f));
        g.fillRoundedRectangle(note, 1.5f);
        g.setColour(palette::midiEffect.brighter(0.32f).withAlpha(0.75f));
        g.drawRoundedRectangle(note.reduced(0.5f), 1.2f, 0.8f);
    }

    const auto parameterText = [this] (int parameter)
    {
        return juce::isPositiveAndBelow(parameter, static_cast<int>(parameters.size()))
            ? parameters[static_cast<size_t>(parameter)].valueText : juce::String {};
    };
    g.setColour(palette::textDim);
    g.setFont(uiFont(8.0f));
    const auto freeRate = parameters[RhinoArpDevice::rateModeParameter].value >= 0.5f;
    const auto summary = parameterText(RhinoArpDevice::styleParameter).toUpperCase()
        + "  \xc2\xb7  " + parameterText(freeRate ? RhinoArpDevice::freeRateParameter
                                                   : RhinoArpDevice::rateParameter);
    drawSnappedText(g, summary, visualArea.reduced(10, 4).withTrimmedTop(visualArea.getHeight() - 19),
                    juce::Justification::centredLeft);

    const auto paintGroup = [&g] (juce::Rectangle<int> bounds, const juce::String& name)
    {
        const auto card = bounds.toFloat().reduced(0.5f);
        g.setColour(palette::displayInset.withAlpha(0.42f));
        g.fillRoundedRectangle(card, 3.0f);
        g.setColour(palette::border);
        g.drawRoundedRectangle(card, 3.0f, 1.0f);
        g.setColour(palette::midiEffect.withAlpha(0.88f));
        g.setFont(uiFontBold(7.5f));
        drawSnappedText(g, name, bounds.reduced(7, 3).withHeight(12), juce::Justification::centredLeft);
    };
    paintGroup(layout.pattern, "PATTERN");
    paintGroup(layout.rate, "RATE");
    paintGroup(layout.timing, "TIMING");
    paintGroup(layout.harmony, "HARMONY");

    const auto caption = [this, &g] (int parameter)
    {
        if (auto* control = arpChoiceFor(parameter))
            drawCaption(g, parameters[static_cast<size_t>(parameter)].name, control->getBounds());
    };
    for (const auto parameter : {RhinoArpDevice::styleParameter,
                                 RhinoArpDevice::offsetParameter, RhinoArpDevice::grooveParameter,
                                 RhinoArpDevice::retriggerParameter, RhinoArpDevice::rootParameter,
                                 RhinoArpDevice::scaleParameter, RhinoArpDevice::stepsParameter})
        caption(parameter);

    const auto knobCaption = [this, &g] (int parameter, const juce::String& name)
    {
        if (!juce::isPositiveAndBelow(parameter, parameterSliders.size()))
            return;
        const auto bounds = parameterSliders[parameter]->getBounds();
        g.setColour(palette::textDim);
        g.setFont(uiFontBold(7.0f));
        drawSnappedText(g, name, bounds.withY(bounds.getY() - 10).withHeight(9), juce::Justification::centred);
        g.setColour(palette::text);
        g.setFont(uiFont(7.0f));
        drawSnappedText(g, parameters[static_cast<size_t>(parameter)].valueText,
                        bounds.withY(bounds.getBottom() - 1).withHeight(10), juce::Justification::centred);
    };
    const auto rateParameter = freeRate ? RhinoArpDevice::freeRateParameter
                                       : RhinoArpDevice::rateParameter;
    knobCaption(rateParameter, {});
    knobCaption(RhinoArpDevice::intervalParameter, "INTERVAL");
    if (juce::isPositiveAndBelow(RhinoArpDevice::repeatsParameter, parameterSliders.size()))
        drawCaption(g, "REPEATS", parameterSliders[RhinoArpDevice::repeatsParameter]->getBounds());
    knobCaption(RhinoArpDevice::distanceParameter, "DIST");
    knobCaption(RhinoArpDevice::gateParameter, "GATE");
}
}
