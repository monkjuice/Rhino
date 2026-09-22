#include "TransportDisplay.h"

namespace rhino
{
TransportDisplay::TransportDisplay()
{
    setOpaque(true);
    configuration.setTooltip("Customize control bar");
    configuration.setAccessible(true);
    configuration.setButtonText("v");
    configuration.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    configuration.setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
    configuration.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffc7d0bf));
    configuration.setColour(juce::TextButton::textColourOnId, juce::Colour(0xffe4efc9));
    configuration.onClick = [this]
    {
        if (configurationRequested) configurationRequested();
    };
    addAndMakeVisible(configuration);
}

void TransportDisplay::GlyphButton::paintButton(juce::Graphics& g, bool highlighted, bool pressed)
{
    auto colour = findColour(juce::TextButton::textColourOffId);
    if (highlighted || pressed) colour = colour.brighter(0.3f);
    g.setColour(colour);
    g.setFont(juce::FontOptions(11.0f));
    g.drawFittedText(getButtonText(), getLocalBounds(), juce::Justification::centred, 1);
}

void TransportDisplay::setDisplayText(const juce::String& next)
{
    if (text == next) return;
    text = next;
    repaint();
}

void TransportDisplay::setSecondaryText(const juce::String& next)
{
    if (secondary == next) return;
    secondary = next;
    repaint();
}

void TransportDisplay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff101418));
    g.fillRoundedRectangle(bounds, 3.0f);
    g.setColour(juce::Colour(0xff3f4c55));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 3.0f, 1.0f);
    auto area = getLocalBounds().reduced(8, 1).withTrimmedRight(20);
    if (secondary.isNotEmpty())
    {
        // Two rows in a box sized for one line of 13px: the split is by weight
        // rather than in half, so the readings sit under the position instead
        // of beside a gap.
        const auto primaryHeight = juce::roundToInt(area.getHeight() * 0.58f);
        const auto lower = area.withTrimmedTop(primaryHeight);
        area = area.withHeight(primaryHeight);
        g.setColour(juce::Colour(0xff8fa08c));
        g.setFont(juce::FontOptions(10.0f));
        g.drawText(secondary, lower, juce::Justification::centredLeft, true);
    }
    g.setColour(juce::Colour(0xffd6e6a7));
    g.setFont(juce::FontOptions(13.0f).withStyle("Bold"));
    g.drawText(text, area, juce::Justification::centredLeft, true);
}

void TransportDisplay::resized()
{
    configuration.setBounds(getWidth() - 19, 2, 17, std::max(1, getHeight() - 4));
}
}
