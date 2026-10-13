#include "ShapedText.h"

// A line kept shaped: see ShapedText.h.

namespace rhino
{
bool ShapedLine::set(const juce::String& text, const juce::Font& newFont, juce::Rectangle<int> box,
                     juce::Justification justification, bool elide)
{
    if (text == source && box == area && justification == placement && elide == elided && newFont == font)
        return false;
    source = text;
    font = newFont;
    area = box;
    placement = justification;
    elided = elide;
    glyphs.clear();
    reach = {};
    // drawSnappedText's own placement, so the line lands on the same pixels.
    const auto line = elide ? elidedToWidth(font, text, area.getWidth()) : text;
    if (line.isEmpty()) return true;
    const auto baseline = area.getY()
        + juce::roundToInt((static_cast<float>(area.getHeight()) + font.getAscent() - font.getDescent()) * 0.5f);
    auto x = area.getX();
    if (justification.testFlags(juce::Justification::horizontallyCentred))
        x = area.getCentreX() - juce::roundToInt(juce::GlyphArrangement::getStringWidth(font, line) * 0.5f);
    else if (justification.testFlags(juce::Justification::right))
        x = area.getRight() - juce::GlyphArrangement::getStringWidthInt(font, line);
    glyphs.addLineOfText(font, line, static_cast<float>(x), static_cast<float>(baseline));
    // A caption is often set in a box shorter than its letters, or wider
    // than the box when it is not elided: the ink can stand outside it, and a
    // repaint that reaches only the ink must still draw the line.
    reach = area.getUnion(glyphs.getBoundingBox(0, -1, true).getSmallestIntegerContainer().expanded(1));
    return true;
}

void ShapedLine::clear()
{
    source = {};
    area = {};
    reach = {};
    glyphs.clear();
}
}
