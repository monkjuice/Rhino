#include "TransportDisplay.h"

namespace theta
{
TransportDisplay::TransportDisplay()
{
    setOpaque(true);
    configuration.setTooltip("Customize control bar");
    configuration.setAccessible(true);
    configuration.setButtonText("v");
    configuration.onClick = [this]
    {
        if (configurationRequested) configurationRequested();
    };
    addAndMakeVisible(configuration);
}

void TransportDisplay::setDisplayText(const juce::String& next)
{
    if (text == next) return;
    text = next;
    repaint();
}

void TransportDisplay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff101418));
    g.fillRoundedRectangle(bounds, 3.0f);
    g.setColour(juce::Colour(0xff3f4c55));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 3.0f, 1.0f);
    g.setColour(juce::Colour(0xffd6e6a7));
    g.setFont(juce::FontOptions(13.0f).withStyle("Bold"));
    g.drawText(text, getLocalBounds().reduced(8, 1).withTrimmedRight(20), juce::Justification::centredLeft, true);
}

void TransportDisplay::resized()
{
    configuration.setBounds(getWidth() - 19, 2, 17, std::max(1, getHeight() - 4));
}
}
