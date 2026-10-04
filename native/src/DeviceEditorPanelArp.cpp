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
    std::array<juce::Rectangle<int>, 3> groups;
};

ArpLayout arpLayout(juce::Rectangle<int> bounds)
{
    ArpLayout layout;
    layout.visual = bounds.removeFromLeft(238);
    bounds.removeFromLeft(10);
    constexpr int gap = 6;
    const auto groupWidth = (bounds.getWidth() - gap * 2) / 3;
    for (int group = 0; group < 3; ++group)
        layout.groups[static_cast<size_t>(group)] = {
            bounds.getX() + group * (groupWidth + gap), bounds.getY(), groupWidth, bounds.getHeight()
        };
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
        constexpr int inset = 7, gap = 4, rowHeight = 22;
        const auto width = (bounds.getWidth() - inset * 2 - gap) / 2;
        const auto y = bounds.getY() + (row == 0 ? 31 : 78);
        return std::array<juce::Rectangle<int>, 2> {
            juce::Rectangle<int>(bounds.getX() + inset, y, width, rowHeight),
            juce::Rectangle<int>(bounds.getX() + inset + width + gap, y, width, rowHeight)
        };
    };

    const auto patternTop = twoColumns(layout.groups[0], 0);
    const auto patternBottom = twoColumns(layout.groups[0], 1);
    choice(RhinoArpDevice::styleParameter, patternTop[0]);
    choice(RhinoArpDevice::rateParameter, patternTop[1]);
    choice(RhinoArpDevice::offsetParameter, patternBottom[0]);
    arpHold.setBounds(patternBottom[1]);

    const auto timingTop = twoColumns(layout.groups[1], 0);
    const auto timingBottom = twoColumns(layout.groups[1], 1);
    choice(RhinoArpDevice::grooveParameter, timingTop[0]);
    choice(RhinoArpDevice::retriggerParameter, timingTop[1]);
    choice(RhinoArpDevice::intervalParameter, timingBottom[0]);
    choice(RhinoArpDevice::repeatsParameter, timingBottom[1]);

    const auto harmonyTop = twoColumns(layout.groups[2], 0);
    choice(RhinoArpDevice::rootParameter, harmonyTop[0]);
    choice(RhinoArpDevice::scaleParameter, harmonyTop[1]);

    const auto& harmony = layout.groups[2];
    choice(RhinoArpDevice::stepsParameter, {harmony.getX() + 7, harmony.getY() + 78, 69, 22});
    const auto placeKnob = [this, &harmony] (int parameter, int x)
    {
        if (!juce::isPositiveAndBelow(parameter, parameterSliders.size()))
            return;
        parameterSliders[parameter]->setBounds(harmony.getX() + x, harmony.getY() + 73, 43, 43);
    };
    placeKnob(RhinoArpDevice::gateParameter, 81);
    placeKnob(RhinoArpDevice::distanceParameter, 130);
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
    drawSnappedText(g, "PATTERN PREVIEW", visualArea.reduced(10, 4).withHeight(14),
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

    juce::Path path;
    path.startNewSubPath(position(0));
    for (int point = 1; point < points; ++point)
        path.lineTo(position(point));
    g.setColour(palette::midiEffect.withAlpha(0.35f));
    g.strokePath(path, juce::PathStrokeType(3.6f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
    g.setColour(palette::midiEffect);
    g.strokePath(path, juce::PathStrokeType(1.25f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
    for (int point = 0; point < points; ++point)
    {
        const auto centre = position(point);
        g.fillEllipse(centre.x - 2.5f, centre.y - 2.5f, 5.0f, 5.0f);
    }

    const auto parameterText = [this] (int parameter)
    {
        return juce::isPositiveAndBelow(parameter, static_cast<int>(parameters.size()))
            ? parameters[static_cast<size_t>(parameter)].valueText : juce::String {};
    };
    g.setColour(palette::textDim);
    g.setFont(uiFont(8.0f));
    const auto summary = parameterText(RhinoArpDevice::styleParameter).toUpperCase()
        + "  \xc2\xb7  " + parameterText(RhinoArpDevice::rateParameter);
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
    paintGroup(layout.groups[0], "PATTERN");
    paintGroup(layout.groups[1], "TIMING");
    paintGroup(layout.groups[2], "HARMONY");

    const auto caption = [this, &g] (int parameter)
    {
        if (auto* control = arpChoiceFor(parameter))
            drawCaption(g, parameters[static_cast<size_t>(parameter)].name, control->getBounds());
    };
    for (const auto parameter : {RhinoArpDevice::styleParameter, RhinoArpDevice::rateParameter,
                                 RhinoArpDevice::offsetParameter, RhinoArpDevice::grooveParameter,
                                 RhinoArpDevice::retriggerParameter, RhinoArpDevice::intervalParameter,
                                 RhinoArpDevice::repeatsParameter, RhinoArpDevice::rootParameter,
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
    knobCaption(RhinoArpDevice::gateParameter, "GATE");
    knobCaption(RhinoArpDevice::distanceParameter, "DIST");
}
}
