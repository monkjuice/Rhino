#include "MonitorSelector.h"
#include "Theme.h"

namespace rhino
{

void MonitorSelector::setMode(Session::InputMonitoring newMode)
{
    if (current == newMode) return;
    current = newMode;
    repaint();
}

juce::Rectangle<int> MonitorSelector::segmentBounds(int index) const
{
    const auto step = static_cast<float>(getWidth()) / static_cast<float>(order.size());
    const auto left = juce::roundToInt(static_cast<float>(index) * step);
    const auto right = juce::roundToInt(static_cast<float>(index + 1) * step);
    return {left, 0, right - left, getHeight()};
}

int MonitorSelector::segmentAt(juce::Point<int> point) const
{
    for (int i = 0; i < static_cast<int>(order.size()); ++i)
        if (segmentBounds(i).contains(point))
            return i;
    return -1;
}

void MonitorSelector::paint(juce::Graphics& g)
{
    g.setColour(palette::control);
    g.fillRect(getLocalBounds());
    g.setFont(uiFont(9.0f));
    for (int i = 0; i < static_cast<int>(order.size()); ++i)
    {
        const auto box = segmentBounds(i);
        const auto chosen = order[static_cast<size_t>(i)] == current;
        if (chosen)
        {
            // The same neutral light solo wears, and for the same reason: the
            // chrome stays grey so that a track's colour, a fader and the
            // record dot are the only saturated things on a card.
            g.setColour(palette::activeNeutral);
            g.fillRect(box);
        }
        else if (i == hovered)
        {
            g.setColour(palette::hover);
            g.fillRect(box);
        }
        // The seam between two segments, so the control reads as three rather
        // than as one box with a lit patch in it.
        if (i > 0)
        {
            g.setColour(palette::appBackground);
            g.fillRect(box.getX(), 0, 1, getHeight());
        }
        g.setColour(chosen ? palette::appBackground : palette::textDim);
        drawSnappedText(g, Session::inputMonitoringName(order[static_cast<size_t>(i)]), box,
                        juce::Justification::centred);
    }
    g.setColour(palette::border);
    g.drawRect(getLocalBounds(), 1);
}

void MonitorSelector::mouseMove(const juce::MouseEvent& event)
{
    setHovered(segmentAt(event.getPosition()));
}

void MonitorSelector::mouseExit(const juce::MouseEvent&)
{
    setHovered(-1);
}

void MonitorSelector::mouseDown(const juce::MouseEvent& event)
{
    const auto index = segmentAt(event.getPosition());
    if (index < 0 || !onChoose) return;
    onChoose(order[static_cast<size_t>(index)]);
}

void MonitorSelector::setHovered(int index)
{
    if (hovered == index) return;
    hovered = index;
    repaint();
}

}
