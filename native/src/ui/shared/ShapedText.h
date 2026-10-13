#pragma once
#include "Theme.h"

namespace rhino
{
// A line of text laid out once and drawn as often as it is painted. It stands
// exactly where drawSnappedText would put it - the baseline snapped to a pixel
// row, the same justification, the same elision - so a painter that keeps one
// draws what drawSnappedText would have drawn without measuring and shaping
// the line again. JUCE keeps the lines it draws, but every width
// drawSnappedText measures, to elide a label and to centre it, is shaped
// afresh: twice a centred key a paint, which in the DJ view was most of what
// a repaint cost (wiki/pages/ui-cost-of-a-change.md).
//
// The Drum Rack face keeps its lines the same way in one table (drawLine in
// DeviceEditorPanelDrumParts.cpp). A component that knows its lines keeps
// them as members instead, and lays them out where its layout changes.
class ShapedLine
{
public:
    // Lays the line out again only when the text, the font, the box or the
    // placement differ from the last time; says whether it did.
    bool set(const juce::String& text, const juce::Font& font, juce::Rectangle<int> box,
             juce::Justification justification = juce::Justification::centredLeft, bool elide = false);
    void clear();
    // In the graphics' current colour, where the line was laid out, or moved
    // by a transform: a letter shaped once and drawn wherever it is needed.
    void draw(juce::Graphics& g) const { glyphs.draw(g); }
    void draw(juce::Graphics& g, juce::AffineTransform transform) const { glyphs.draw(g, transform); }
    // Whether a repaint reaches the line: its box, or ink standing outside it.
    bool isIn(const juce::Graphics& g) const { return !reach.isEmpty() && g.clipRegionIntersects(reach); }
    juce::Rectangle<int> box() const { return area; }
    const juce::String& text() const { return source; }

private:
    juce::String source;
    juce::Font font {juce::FontOptions()};
    juce::Rectangle<int> area, reach;
    juce::Justification placement {juce::Justification::centredLeft};
    bool elided = false;
    juce::GlyphArrangement glyphs;
};
}
