#pragma once

#include "ForgePanels.h"

// The loaded preset, named on the title bar's plate: on Forge's maker plate in
// the three lines its stamped legend used to fill, on Merli's beside the cat.
//
// A component over the cached chassis rather than part of it, because it
// changes with every preset and the chassis is rebuilt only when the window's
// size does. Not buffered to an image: three short lines cost the 24 Hz frame
// next to nothing, and a buffered child under Direct2D shifted two pixels of an
// oscillator's table field between two fresh panels, which is exactly what the
// theme suite's byte-for-byte check exists to catch. It takes no clicks: the
// panel decides what a click on the plate does.
namespace rhino::forge::ui
{
class PresetPlate final : public juce::Component
{
public:
    PresetPlate() { setInterceptsMouseClicks(false, false); }

    // Name, then who made it and its style, then where it came from. `warned`
    // lights the last line, which is where a conversion's losses are counted.
    void show(const juce::String& presetName, const juce::String& byline, const juce::String& origin, bool warned)
    {
        if (presetName == name && byline == by && origin == from && warned == warning) return;
        name = presetName;
        by = byline;
        from = origin;
        warning = warned;
        repaint();
    }

    // Where the plate's words go in the panel, for the theme on show.
    static juce::Rectangle<int> boundsFor(juce::Rectangle<int> editorBounds)
    {
        if (isMerli()) return merli::catPlateTextBounds(editorBounds);
        return identityTextBounds(identityPlateBounds(editorBounds));
    }

    // How far the byline may run on Forge's plate before the stripes.
    int bylineWidth = 0;

    void paint(juce::Graphics& g) override
    {
        if (isMerli()) paintMerli(g);
        else paintForge(g);
    }

private:
    juce::String name, by, from;
    bool warning = false;

    // The widest the name may be set in the plate's tracked lettering before it
    // gives up the tracking, and then the size, rather than its last letters.
    static void drawName(juce::Graphics& g, const juce::String& lettering, juce::Rectangle<float> box,
                         Face face, float size, float tracking)
    {
        for (auto height = size; height >= size - 4.0f; height -= 1.0f)
        {
            const auto font = panelFont(face, height);
            const auto width = juce::GlyphArrangement::getStringWidth(font, lettering)
                             + tracking * static_cast<float>(juce::jmax(0, lettering.length() - 1));
            if (width <= box.getWidth())
            {
                g.setFont(font);
                drawTrackedText(g, lettering, box, tracking, juce::Justification::centredLeft);
                return;
            }
            tracking = juce::jmax(0.0f, tracking - 0.8f);
        }
        g.setFont(panelFont(face, size - 4.0f));
        g.drawFittedText(lettering, box.toNearestInt(), juce::Justification::centredLeft, 1, 0.9f);
    }

    void paintForge(juce::Graphics& g)
    {
        // The same lettering the maker's legend was stamped in: the name where
        // RHINO FORGE stood, the rule under it, two stamped lines below.
        const auto box = getLocalBounds().toFloat();
        const auto title = juce::Rectangle<float>(2.0f, 3.0f, box.getWidth() - 2.0f, 19.0f);
        g.setColour(identityTitle);
        drawName(g, name.toUpperCase(), title, Face::reading, 15.0f, 2.2f);
        g.setColour(identityRule);
        g.drawLine(title.getX(), title.getBottom() + 1.0f, title.getX() + juce::jmin(130.0f, title.getWidth()),
                   title.getBottom() + 1.0f, 0.7f);
        const auto lines = juce::jmax(40.0f, static_cast<float>(bylineWidth > 0 ? bylineWidth : getWidth()) - 2.0f);
        g.setFont(panelFont(Face::label, 11.0f));
        g.setColour(legendText);
        g.drawFittedText(by, juce::Rectangle<float>(2.0f, 24.0f, lines, 13.0f).toNearestInt(),
                         juce::Justification::centredLeft, 1, 0.8f);
        g.setFont(panelFont(Face::label, 10.5f));
        g.setColour(warning ? signalViolet : legendText);
        g.drawFittedText(from, juce::Rectangle<float>(2.0f, 37.0f, lines, 13.0f).toNearestInt(),
                         juce::Justification::centredLeft, 1, 0.8f);
    }

    void paintMerli(juce::Graphics& g)
    {
        // Matte and warm: the name in the plate's own text colour, the two
        // lines under it in its legend, nothing stamped or tracked.
        auto area = getLocalBounds();
        g.setColour(text);
        drawName(g, name.toUpperCase(), area.removeFromTop(19).toFloat(), Face::header, 14.0f, 0.8f);
        area.removeFromTop(2);
        g.setFont(panelFont(Face::label, 11.0f));
        g.setColour(labelText);
        g.drawFittedText(by, area.removeFromTop(13), juce::Justification::centredLeft, 1, 0.8f);
        g.setFont(panelFont(Face::label, 10.5f));
        g.setColour(warning ? signalViolet : legendText);
        g.drawFittedText(from, area.removeFromTop(13), juce::Justification::centredLeft, 1, 0.8f);
    }
};
}
