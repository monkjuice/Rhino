#include "ControlBarIcons.h"
#include <cmath>

namespace rhino::icons
{
namespace
{
// The path data from assets/metronome.svg, in its 180x258 viewBox, with the
// subpaths run together: they are stroked at one width and so are one path.
constexpr const char* metronomeSvgPath =
    "M74 34 H110 C114 34 117 36 118 40 L150 214 C151 219 148 222 143 222 H38 C33 222 30 219 31 214 L64 40 C65 36 68 34 74 34Z"
    "M79 47 H103 L125 174 H56 L79 47Z"
    "M32 49 L68 133"
    "M53 104 L66 98 L73 113 L59 120 Z"
    "M43 222 L45 231 C46 234 48 235 51 235 H56 C59 235 61 233 62 230 L63 222"
    "M122 222 L124 231 C125 234 127 235 130 235 H135 C138 235 140 233 141 230 L142 222";

}

juce::Path metronome()
{
    // Parsed once: the string is fixed, and parsing it on every frame of a
    // thirty hertz repaint would be the most expensive thing the bar does.
    static const juce::Path parsed = juce::Drawable::parseSVGPath(metronomeSvgPath);
    return parsed;
}

juce::Path play()
{
    juce::Path path;
    path.addTriangle(0.12f, 0.0f, 0.12f, 1.0f, 0.94f, 0.5f);
    return path;
}

juce::Path stop()
{
    juce::Path path;
    path.addRectangle(0.08f, 0.08f, 0.84f, 0.84f);
    return path;
}

juce::Path returnToStart()
{
    juce::Path path;
    path.addRectangle(0.06f, 0.04f, 0.17f, 0.92f);
    path.addTriangle(0.96f, 0.04f, 0.96f, 0.96f, 0.30f, 0.5f);
    return path;
}

void drawCurvedArrow(juce::Graphics& g, juce::Rectangle<float> area, bool pointingLeft, float lineWidth)
{
    const auto size = std::min(area.getWidth(), area.getHeight());
    if (size <= 2.0f) return;
    const auto radius = size * 0.36f;
    // Set low in its box, because the head hangs below the arc and the pair has
    // to sit on the same optical line as the glyphs along the rest of the bar.
    const juce::Point<float> centre {area.getCentreX(), area.getCentreY() - size * 0.08f};
    juce::Path arc;
    // JUCE measures from twelve o'clock and sweeps clockwise, so this runs from
    // nine o'clock over the top to three o'clock: an arc opening downwards.
    arc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f,
                      -juce::MathConstants<float>::halfPi,
                      juce::MathConstants<float>::halfPi, true);
    g.strokePath(arc, juce::PathStrokeType(lineWidth, juce::PathStrokeType::curved, juce::PathStrokeType::butt));

    // The head sits on the end the arrow travels towards, pointing down along
    // the tangent there - which at either three or nine o'clock is straight
    // down. Filled, so it keeps a point at fourteen pixels.
    const auto tip = pointingLeft ? centre.x - radius : centre.x + radius;
    const auto head = size * 0.24f;
    juce::Path arrow;
    arrow.startNewSubPath(tip, centre.y + head);
    arrow.lineTo(tip - head * 0.62f, centre.y - head * 0.2f);
    arrow.lineTo(tip + head * 0.62f, centre.y - head * 0.2f);
    arrow.closeSubPath();
    g.fillPath(arrow);
}

void drawSidebar(juce::Graphics& g, juce::Rectangle<float> area)
{
    // Fixed at eighteen by fourteen and snapped to whole pixels: two solid
    // blocks with a gap between them only stay legible while every edge lands
    // on a pixel column, and the button this sits in is always the same size.
    const auto icon = area.withSizeKeepingCentre(18.0f, 14.0f);
    const auto x = std::floor(icon.getX());
    const auto y = std::floor(icon.getY());
    g.fillRect(x, y, 4.0f, 14.0f);
    g.fillRect(x + 7.0f, y, 10.0f, 14.0f);
}

juce::Path chevronDown()
{
    juce::Path path;
    path.startNewSubPath(0.0f, 0.12f);
    path.lineTo(0.5f, 0.88f);
    path.lineTo(1.0f, 0.12f);
    return path;
}

void strokeFitted(juce::Graphics& g, const juce::Path& path, juce::Rectangle<float> area, float lineWidth)
{
    if (path.isEmpty() || area.isEmpty()) return;
    // Half the stroke falls outside the path's own bounds, so the artwork is
    // fitted to a rectangle shrunk by that much or the icon is clipped at the
    // edges of its button.
    const auto inner = area.reduced(lineWidth * 0.5f);
    if (inner.isEmpty()) return;
    g.strokePath(path, juce::PathStrokeType(lineWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                 path.getTransformToScaleToFit(inner, true));
}

void fillFitted(juce::Graphics& g, const juce::Path& path, juce::Rectangle<float> area)
{
    if (path.isEmpty() || area.isEmpty()) return;
    g.fillPath(path, path.getTransformToScaleToFit(area, true));
}
}

namespace rhino
{
void IconButton::paintButton(juce::Graphics& g, bool highlighted, bool pressed)
{
    const auto bounds = getLocalBounds().toFloat();
    if (washes && isEnabled() && (highlighted || pressed))
    {
        g.setColour(juce::Colour(pressed ? 0x24ffffff : 0x14ffffff));
        g.fillRoundedRectangle(bounds.reduced(1.0f), 3.0f);
    }
    auto colour = getToggleState() ? active : idle;
    if (!isEnabled())                colour = palette::disabled;
    else if (pressed)                colour = colour.brighter(0.5f);
    else if (highlighted)            colour = colour.brighter(0.3f);
    if (painter)
        painter(g, bounds.reduced(bounds.getWidth() * inset, bounds.getHeight() * inset), colour);
}
}
