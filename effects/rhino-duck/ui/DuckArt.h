#pragma once

#include "DuckTheme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cmath>

// The panel's physical parts, drawn rather than shipped as pictures: the
// plates and their screws, the recessed wells, a grain to keep a flat fill
// from looking printed, and the rubber duck, whose head is the brand's mark
// and whose whole self floats in the meter. Only the frame, the meter, the
// knobs and the scope's well get this treatment; chips, tiles and labels stay
// flat.
namespace rhino::duck::ui
{
// A tile of faint light and dark specks, laid over a plate. Made once by
// whoever paints plates and kept, so no paint generates it.
inline juce::Image makeGrain()
{
    constexpr int size = 128;
    juce::Image tile(juce::Image::ARGB, size, size, true, juce::SoftwareImageType());
    juce::Image::BitmapData pixels(tile, juce::Image::BitmapData::writeOnly);
    juce::Random random(7);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const auto speck = random.nextFloat() - 0.5f;
            const auto ink = speck > 0.0f ? juce::Colours::white : juce::Colours::black;
            pixels.setPixelColour(x, y, ink.withAlpha(std::abs(speck) * 0.09f));
        }
    return tile;
}

// A raised section of the panel: a shadow under it, a fill a shade lighter at
// the top, the grain, a lit top edge and a dark outline.
inline void drawPlate(juce::Graphics& g, juce::Rectangle<float> area, const juce::Image& grain, float corner = 8.0f)
{
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.fillRoundedRectangle(area.translated(0.0f, 2.0f), corner);
    g.setGradientFill(juce::ColourGradient(colour::plate.brighter(0.08f), area.getX(), area.getY(),
                                           colour::plate.darker(0.12f), area.getX(), area.getBottom(), false));
    g.fillRoundedRectangle(area, corner);
    if (grain.isValid())
    {
        g.setTiledImageFill(grain, 0, 0, 1.0f);
        g.fillRoundedRectangle(area, corner);
    }
    g.setColour(juce::Colours::white.withAlpha(0.05f));
    g.drawRoundedRectangle(area.reduced(1.5f), corner - 1.0f, 1.0f);
    g.setColour(colour::panelEdge);
    g.drawRoundedRectangle(area.reduced(0.5f), corner, 1.0f);
}

// A recess cut into a plate: the scope, a value box. Darker at its top edge,
// where the lip shades it, and lit along its bottom.
inline void drawRecess(juce::Graphics& g, juce::Rectangle<float> area, float corner)
{
    g.setColour(colour::well);
    g.fillRoundedRectangle(area, corner);
    juce::Path inside;
    inside.addRoundedRectangle(area, corner);
    juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(inside);
    g.setGradientFill(juce::ColourGradient(juce::Colours::black.withAlpha(0.55f), area.getX(), area.getY(),
                                           juce::Colours::transparentBlack, area.getX(), area.getY() + std::min(10.0f, area.getHeight() * 0.5f), false));
    g.fillRect(area);
    g.setColour(juce::Colours::white.withAlpha(0.05f));
    g.drawRoundedRectangle(area.reduced(0.5f).translated(0.0f, 0.5f), corner, 1.0f);
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.drawRoundedRectangle(area.reduced(0.5f), corner, 1.0f);
}

// A countersunk screw head with its slot at `angle`.
inline void drawScrew(juce::Graphics& g, juce::Point<float> centre, float radius, float angle)
{
    const auto head = juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre);
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.fillEllipse(head.expanded(1.0f).translated(0.0f, 0.6f));
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff4a4f57), head.getX(), head.getY(),
                                           juce::Colour(0xff1a1c20), head.getRight(), head.getBottom(), false));
    g.fillEllipse(head);
    const auto reach = juce::Point<float>(std::cos(angle), std::sin(angle)) * (radius * 0.72f);
    g.setColour(juce::Colour(0xff0c0d0f));
    g.drawLine(juce::Line<float>(centre - reach, centre + reach), std::max(1.0f, radius * 0.38f));
}

// A screw in each corner of a plate, each slot turned its own way.
inline void drawScrews(juce::Graphics& g, juce::Rectangle<float> plate, float inset = 9.0f, float radius = 3.6f)
{
    const std::array<juce::Point<float>, 4> corners { plate.getTopLeft() + juce::Point<float>(inset, inset),
                                                      plate.getTopRight() + juce::Point<float>(-inset, inset),
                                                      plate.getBottomLeft() + juce::Point<float>(inset, -inset),
                                                      plate.getBottomRight() + juce::Point<float>(-inset, -inset) };
    const std::array<float, 4> angles { 0.6f, 2.1f, 1.2f, 0.2f };
    for (size_t i = 0; i < corners.size(); ++i) drawScrew(g, corners[i], radius, angles[i]);
}

// ---- the duck ------------------------------------------------------------------

// The rubber duck, facing right: a tail up at the left, a round chest, the
// head over the front, the beak and the rhino's horn to the right, and a
// scowl. The meter floats all of it; the brand shows the head alone. In a
// drawing about 200 units wide, built once by whoever draws it, since parsing
// per paint would cost every frame.
struct DuckArt
{
    juce::Path body, wing, head, beak, mouth, horn, hornRidge, brow, eye;

    DuckArt()
    {
        body = juce::Drawable::parseSVGPath("M12 74C30 82 44 94 64 96L150 94C186 94 198 120 190 138C180 160 142 168 100 168C52 168 22 152 16 126C12 110 14 92 12 74Z");
        wing = juce::Drawable::parseSVGPath("M58 118C80 104 114 108 128 126C110 142 80 144 58 132Z");
        head.addEllipse(86.0f, 24.0f, 84.0f, 84.0f);
        beak = juce::Drawable::parseSVGPath("M160 60C180 50 200 56 210 68C200 80 180 84 160 80Z");
        mouth = juce::Drawable::parseSVGPath("M164 70C178 72 194 71 208 68");
        // A rhino's horn on the crown: wide where it grows, rising to a point
        // that leans back, its front bowed and its back hollowed. Its base
        // tucks in behind the head, which is drawn over it.
        horn = juce::Drawable::parseSVGPath("M130 36C134 16 140 -4 148 -22C162 -6 172 16 174 44C160 44 144 40 130 36Z");
        hornRidge = juce::Drawable::parseSVGPath("M136 18C148 20 160 22 171 22");
        brow = juce::Drawable::parseSVGPath("M120 44L160 54L157 61L118 51Z");
        eye.addEllipse(136.0f, 56.0f, 13.0f, 14.0f);
    }

    juce::Rectangle<float> headBounds() const { return head.getBounds().getUnion(beak.getBounds()).getUnion(horn.getBounds()); }
    juce::Rectangle<float> duckBounds() const { return body.getBounds().getUnion(headBounds()); }
};

// How the floating duck is leaning and pressed: a tilt in radians and a
// squash of its height, both small.
struct DuckPose
{
    float tilt = 0.0f, squash = 0.0f;
};

namespace detail
{
// Rubber: lit from a point on its upper left, shading to its lower right.
inline void fillRubber(juce::Graphics& g, const juce::Path& path, float lightX, float lightY)
{
    const auto bounds = path.getBounds();
    juce::ColourGradient fill(colour::duckBody.brighter(0.35f), bounds.getX() + bounds.getWidth() * lightX, bounds.getY() + bounds.getHeight() * lightY,
                              colour::duckShade, bounds.getRight(), bounds.getBottom(), true);
    fill.addColour(0.45, colour::duckBody);
    g.setGradientFill(fill);
    g.fillPath(path);
}

// The head and what is on it, through `fit`: the horn behind it, then the
// head, the beak, the scowl and the eye.
inline void drawHead(juce::Graphics& g, const DuckArt& art, const juce::AffineTransform& fit)
{
    const auto shape = [&fit] (const juce::Path& path) { auto placed = path; placed.applyTransform(fit); return placed; };

    const auto horn = shape(art.horn);
    const auto hornBounds = horn.getBounds();
    g.setGradientFill(juce::ColourGradient(colour::duckHorn.brighter(0.45f), hornBounds.getX(), hornBounds.getBottom(),
                                           colour::duckHorn.darker(0.5f), hornBounds.getRight(), hornBounds.getY(), false));
    g.fillPath(horn);
    g.setColour(colour::duckInk.withAlpha(0.25f));
    g.strokePath(shape(art.hornRidge), juce::PathStrokeType(std::max(0.8f, hornBounds.getWidth() * 0.03f)));

    fillRubber(g, shape(art.head), 0.35f, 0.3f);

    const auto beak = shape(art.beak);
    const auto beakBounds = beak.getBounds();
    g.setGradientFill(juce::ColourGradient(colour::duckBeak.brighter(0.25f), beakBounds.getX(), beakBounds.getY(),
                                           colour::duckBeak.darker(0.2f), beakBounds.getX(), beakBounds.getBottom(), false));
    g.fillPath(beak);
    g.setColour(colour::duckInk.withAlpha(0.35f));
    g.strokePath(shape(art.mouth), juce::PathStrokeType(std::max(0.7f, beakBounds.getHeight() * 0.06f)));

    g.setColour(colour::duckInk);
    g.fillPath(shape(art.brow));
    const auto eye = shape(art.eye);
    g.fillPath(eye);
    const auto glint = eye.getBounds();
    g.setColour(juce::Colours::white.withAlpha(0.85f));
    g.fillEllipse(juce::Rectangle<float>(glint.getWidth() * 0.28f, glint.getWidth() * 0.28f)
                      .withCentre({ glint.getX() + glint.getWidth() * 0.62f, glint.getY() + glint.getHeight() * 0.32f }));
}
}

// The duck's head, the brand's mark, fitted into `box`.
inline void drawDuckHead(juce::Graphics& g, const DuckArt& art, juce::Rectangle<float> box)
{
    detail::drawHead(g, art, juce::RectanglePlacement(juce::RectanglePlacement::centred).getTransformToFit(art.headBounds(), box));
}

// The whole rubber duck fitted into `box`, tilted about its middle and
// squashed from its bottom.
inline void drawDuck(juce::Graphics& g, const DuckArt& art, juce::Rectangle<float> box, DuckPose pose = {})
{
    const auto centre = box.getCentre();
    const auto fit = juce::RectanglePlacement(juce::RectanglePlacement::centred).getTransformToFit(art.duckBounds(), box)
                         .followedBy(juce::AffineTransform::scale(1.0f + pose.squash * 0.5f, 1.0f - pose.squash, centre.x, box.getBottom()))
                         .followedBy(juce::AffineTransform::rotation(pose.tilt, centre.x, centre.y));
    auto body = art.body;
    body.applyTransform(fit);
    detail::fillRubber(g, body, 0.42f, 0.25f);
    auto wing = art.wing;
    wing.applyTransform(fit);
    g.setColour(colour::duckShade.withAlpha(0.45f));
    g.fillPath(wing);
    detail::drawHead(g, art, fit);
}
}
