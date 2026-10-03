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
}

void DeviceEditorPanel::layoutArp()
{
    auto bounds = contentArea;
    visualArea = bounds.removeFromLeft(164);
    bounds.removeFromLeft(6);

    constexpr std::array<int, 7> top {
        RhinoArpDevice::styleParameter,
        RhinoArpDevice::holdParameter,
        RhinoArpDevice::offsetParameter,
        RhinoArpDevice::grooveParameter,
        RhinoArpDevice::retriggerParameter,
        RhinoArpDevice::intervalParameter,
        RhinoArpDevice::repeatsParameter
    };
    constexpr std::array<int, 6> bottom {
        RhinoArpDevice::rateParameter,
        RhinoArpDevice::distanceParameter,
        RhinoArpDevice::stepsParameter,
        RhinoArpDevice::gateParameter,
        RhinoArpDevice::rootParameter,
        RhinoArpDevice::scaleParameter
    };

    const auto rowHeight = bounds.getHeight() / 2;
    const auto place = [this] (int index, juce::Rectangle<int> cell)
    {
        if (!juce::isPositiveAndBelow(index, parameterSliders.size()))
            return;
        const auto knobSize = std::min({40, std::max(30, cell.getWidth() - 26),
                                        std::max(30, cell.getHeight() - 34)});
        parameterLabels[index]->setBounds(cell.getX() + 2, cell.getY(), cell.getWidth() - 4, 15);
        parameterSliders[index]->setBounds(cell.withSizeKeepingCentre(knobSize, knobSize).translated(0, 2));
        parameterValues[index]->setBounds(cell.getX() + 2, cell.getBottom() - 17, cell.getWidth() - 4, 15);
        parameterAutomation[index]->setBounds(parameterSliders[index]->getRight() - 7,
                                               parameterSliders[index]->getY() - 1, 16, 14);
    };

    const auto topWidth = bounds.getWidth() / static_cast<int>(top.size());
    for (int column = 0; column < static_cast<int>(top.size()); ++column)
        place(top[static_cast<size_t>(column)],
              {bounds.getX() + column * topWidth, bounds.getY(), topWidth, rowHeight});

    const auto bottomWidth = bounds.getWidth() / static_cast<int>(bottom.size());
    for (int column = 0; column < static_cast<int>(bottom.size()); ++column)
        place(bottom[static_cast<size_t>(column)],
              {bounds.getX() + column * bottomWidth, bounds.getY() + rowHeight,
               bottomWidth, bounds.getHeight() - rowHeight});
}

void DeviceEditorPanel::paintArp(juce::Graphics& g)
{
    if (visualArea.isEmpty())
        return;

    const auto panel = visualArea.toFloat().reduced(5.0f);
    g.setColour(palette::displayInset);
    g.fillRoundedRectangle(panel, 4.0f);
    g.setColour(palette::border);
    g.drawRoundedRectangle(panel, 4.0f, 1.0f);

    g.setColour(palette::textDim);
    g.setFont(uiFontBold(8.0f));
    drawSnappedText(g, "BOUNCE", visualArea.reduced(10, 4).withHeight(14),
                    juce::Justification::centredLeft);

    auto graph = visualArea.reduced(13, 8).withTrimmedTop(18).toFloat();
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
}
}
