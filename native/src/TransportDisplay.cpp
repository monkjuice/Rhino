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

void TransportDisplay::setTrailingText(const juce::String& next)
{
    if (trailing == next) return;
    trailing = next;
    repaint();
}

void TransportDisplay::setSecondaryTrailingText(const juce::String& next)
{
    if (secondaryTrailing == next) return;
    secondaryTrailing = next;
    repaint();
}

namespace
{
// One line of the readout: a run against the left edge and, when the line is
// wide enough for both, a second against the right. The left run is the one
// that must hold still - it is a clock, and a digit's worth of drift is read
// as the clock itself moving - so the right run is what goes when they do not
// fit, whole rather than truncated to a stub.
void drawReadoutRow(juce::Graphics& g, juce::Rectangle<int> row,
                    const juce::String& left, const juce::Font& leftFont, juce::Colour leftColour,
                    const juce::String& right, const juce::Font& rightFont, juce::Colour rightColour)
{
    if (right.isNotEmpty())
    {
        const auto rightWidth = juce::GlyphArrangement::getStringWidthInt(rightFont, right);
        const auto leftWidth = juce::GlyphArrangement::getStringWidthInt(leftFont, left);
        if (leftWidth + 16 + rightWidth <= row.getWidth())
        {
            g.setColour(rightColour);
            g.setFont(rightFont);
            g.drawText(right, row.removeFromRight(rightWidth), juce::Justification::centredRight, false);
        }
    }
    g.setColour(leftColour);
    g.setFont(leftFont);
    g.drawText(left, row, juce::Justification::centredLeft, true);
}
}

void TransportDisplay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff101418));
    g.fillRoundedRectangle(bounds, 3.0f);
    g.setColour(juce::Colour(0xff3f4c55));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 3.0f, 1.0f);
    auto area = getLocalBounds().reduced(8, 1).withTrimmedRight(20);
    const juce::Font positionFont(juce::FontOptions(13.0f).withStyle("Bold"));
    const juce::Font readingFont(juce::FontOptions(10.0f));
    // The loop shares the position's line but not its weight: it is set once
    // and then watched out of the corner of the eye, so it is drawn at the
    // size of a reading rather than the size of the clock beside it.
    const juce::Font trailingFont(juce::FontOptions(11.0f));
    if (secondary.isNotEmpty() || secondaryTrailing.isNotEmpty())
    {
        // Two rows in a box sized for one line of 13px: the split is by weight
        // rather than in half, so the readings sit under the position instead
        // of beside a gap.
        const auto primaryHeight = juce::roundToInt(area.getHeight() * 0.58f);
        const auto lower = area.withTrimmedTop(primaryHeight);
        area = area.withHeight(primaryHeight);
        drawReadoutRow(g, lower, secondary, readingFont, juce::Colour(0xff8fa08c),
                       secondaryTrailing, readingFont, juce::Colour(0xff76856f));
    }
    drawReadoutRow(g, area, text, positionFont, juce::Colour(0xffd6e6a7),
                   trailing, trailingFont, juce::Colour(0xff8fa08c));
}

void TransportDisplay::resized()
{
    configuration.setBounds(getWidth() - 19, 2, 17, std::max(1, getHeight() - 4));
}
}
