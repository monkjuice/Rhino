#pragma once

#include "ForgeTheme.h"
#include <BinaryData.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Merli's material: the fur and panel textures, the rounded plate, the paw and
// the screws. Each primitive takes the geometry its Forge counterpart takes,
// so the dispatch at the top of a Forge primitive is one line.
//
// Nothing here holds a static juce::Image. A static Direct2D image in a plugin
// deadlocks NVIDIA's driver when the DLL unloads, so every texture is fetched
// from the ImageCache at the point of use, as drawWordmark fetches the logo:
// the cache keeps the decoded pixels between calls and pins nothing at exit.
namespace rhino::forge::ui::merli
{
// The ears are the cat's own: earRight is the ear on the viewer's left, as it
// is on a cat facing you.
enum class Asset { fur, panel, earLeft, earRight, screwSmall, screwLarge, titlePlate };

// The decoded PNG. The two screws are trimmed to their opaque pixels, so a
// radius handed to drawScrew describes the hardware and not the gutter the
// asset was exported with — the same trim fastenerImage gives the dome.
inline juce::Image image(Asset asset)
{
    const auto from = [] (const char* bytes, int size) { return juce::ImageCache::getFromMemory(bytes, size); };
    switch (asset)
    {
        case Asset::fur:        return from(BinaryData::merli_fur_png, BinaryData::merli_fur_pngSize);
        case Asset::panel:      return from(BinaryData::merli_panel_png, BinaryData::merli_panel_pngSize);
        case Asset::earLeft:    return from(BinaryData::merli_ear_left_png, BinaryData::merli_ear_left_pngSize);
        case Asset::earRight:   return from(BinaryData::merli_ear_right_png, BinaryData::merli_ear_right_pngSize);
        case Asset::screwSmall: return from(BinaryData::merli_screw_small_png, BinaryData::merli_screw_small_pngSize)
                                    .getClippedImage({5, 5, 55, 55});
        case Asset::screwLarge: return from(BinaryData::merli_screw_large_png, BinaryData::merli_screw_large_pngSize)
                                    .getClippedImage({12, 12, 105, 105});
        case Asset::titlePlate: return from(BinaryData::merli_title_plate_png, BinaryData::merli_title_plate_pngSize);
    }
    return {};
}

// Tiles a texture over a shape, from the origin given. The fill is bounded by
// the path, so no clip is pushed; the saved state only restores the brush.
inline void fillTexture(juce::Graphics& g, const juce::Path& shape, Asset asset, float scale, float opacity,
                        juce::Point<float> origin = {})
{
    if (opacity <= 0.0f || scale <= 0.0f) return;
    const auto tile = image(asset);
    if (tile.isNull()) return;
    juce::Graphics::ScopedSaveState state(g);
    juce::FillType fill(tile, juce::AffineTransform::scale(scale).translated(origin));
    fill.setOpacity(opacity);
    g.setFillType(fill);
    g.fillPath(shape);
}

// The paw: a pad and four toes, as ellipses. Drawn rather than blitted because
// it is on the frame path — the EnableLed wears one — and because it has to
// tint by state, which a PNG cannot. The proportions are read off the
// designer's paw icon (not shipped; see ui/assets/README.md): the pad is wider
// than tall and takes the lower half, the
// middle toes stand tallest, and the outer pair sit lower, smaller, and lean
// outward, which is what makes it a paw rather than a bunch of grapes.
inline void drawPaw(juce::Graphics& g, juce::Rectangle<float> box, juce::Colour colour, float alpha)
{
    if (box.getWidth() < 2.0f || box.getHeight() < 2.0f || alpha <= 0.0f) return;
    const auto x = box.getX(), y = box.getY(), w = box.getWidth(), h = box.getHeight();

    juce::Path paw;
    paw.addEllipse(x + w * 0.20f, y + h * 0.47f, w * 0.60f, h * 0.50f);
    const auto toe = [&] (float across, float down, float width, float height, float lean)
    {
        juce::Path shape;
        shape.addEllipse(-width * 0.5f, -height * 0.5f, width, height);
        shape.applyTransform(juce::AffineTransform::rotation(lean).translated(x + across * w, y + down * h));
        paw.addPath(shape);
    };
    toe(0.33f, 0.20f, w * 0.22f, h * 0.34f, -0.12f);
    toe(0.67f, 0.20f, w * 0.22f, h * 0.34f,  0.12f);
    toe(0.11f, 0.42f, w * 0.19f, h * 0.27f, -0.45f);
    toe(0.89f, 0.42f, w * 0.19f, h * 0.27f,  0.45f);

    g.setColour(colour.withMultipliedAlpha(alpha));
    g.fillPath(paw);
}

// One of the two Merli screws, the small one unless asked for the large.
inline void drawScrew(juce::Graphics& g, juce::Point<float> centre, float radius, bool large, float alpha)
{
    if (radius < 1.0f || alpha <= 0.0f) return;
    const auto art = image(large ? Asset::screwLarge : Asset::screwSmall);
    if (art.isNull()) return;
    juce::Graphics::ScopedSaveState state(g);
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.setOpacity(alpha);
    g.drawImage(art, juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre),
                juce::RectanglePlacement::centred);
}

// A Merli well: a rounded floor with a hairline wall. Forge's well is sunk,
// its wall shaded at the top and lit at the foot; a matte panel throws no
// light for a wall to catch, so one row of `line` is all that separates the
// well from the plate it is let into.
inline void drawRoundedWell(juce::Graphics& g, juce::Rectangle<float> box, juce::Colour floorColour,
                            float alpha, float radius)
{
    if (box.getWidth() < 2.0f || box.getHeight() < 2.0f || alpha <= 0.0f) return;
    const auto corner = juce::jmax(0.0f, juce::jmin(radius, box.getWidth() * 0.5f, box.getHeight() * 0.5f));

    g.setColour(floorColour.withAlpha(alpha));
    g.fillRoundedRectangle(box, corner);

    // The wall is the well's outermost row, so the stroke runs half a pixel
    // in. Centred on the edge itself it would straddle two rows at half
    // strength each and read as a blur rather than a wall.
    juce::Path wall;
    wall.addRoundedRectangle(box.reduced(0.5f), juce::jmax(0.0f, corner - 0.5f));
    g.setColour(line.withAlpha(alpha));
    g.strokePath(wall, juce::PathStrokeType(1.0f));
}

// The face of a shaped part in Merli: the plate gradient and a hairline, and
// nothing else. drawMetalPiece stands Forge's parts off the chassis with lips
// and grain; a matte panel has no relief to show, so the outline alone says
// where the part ends.
inline void drawMatteFill(juce::Graphics& g, const juce::Path& shape, float alpha)
{
    const auto box = shape.getBounds();
    if (box.getWidth() < 2.0f || box.getHeight() < 2.0f || alpha <= 0.0f) return;
    juce::ColourGradient face(plateFaceTop.withAlpha(alpha), box.getCentreX(), box.getY(),
                              plateFaceFoot.withAlpha(alpha), box.getCentreX(), box.getBottom(), false);
    g.setGradientFill(face);
    g.fillPath(shape);
    g.setColour(line.withAlpha(alpha));
    g.strokePath(shape, juce::PathStrokeType(1.0f));
}

// The Merli plate: a rounded matte panel standing a little off the chassis.
// Shadow, face, hairline and a lit lip along the top, with small screws at
// the corners when asked. No grain, no rails, no wear — the warmth is in the
// colours, and the fur around it is texture enough.
// Screws at the four corners when asked, or only the two at the foot: a plate
// whose header carries a paw at its top-left corner has no room for one there.
inline void drawRoundedPlate(juce::Graphics& g, juce::Rectangle<float> box, float radius, float alpha, bool screws,
                             bool topScrews = true)
{
    if (box.getWidth() < 2.0f || box.getHeight() < 2.0f || alpha <= 0.0f) return;
    const auto corner = juce::jmax(0.0f, juce::jmin(radius, box.getWidth() * 0.5f, box.getHeight() * 0.5f));

    // Three rings rather than a blur, as drawPlateShadow draws Forge's: a
    // blurred shadow per plate per rebuild is a cost this panel will not pay,
    // and at this spread the difference cannot be seen.
    for (int ring = 3; ring >= 1; --ring)
    {
        const auto spread = static_cast<float>(ring);
        juce::Path halo;
        halo.addRoundedRectangle(box.expanded(spread).translated(0.0f, spread * 0.4f), corner + spread);
        g.setColour(juce::Colours::black.withAlpha(0.22f * alpha / spread));
        g.strokePath(halo, juce::PathStrokeType(spread * 1.5f));
    }

    juce::Path face;
    face.addRoundedRectangle(box, corner);
    juce::ColourGradient fill(plateFaceTop.withAlpha(alpha), box.getCentreX(), box.getY(),
                              plateFaceFoot.withAlpha(alpha), box.getCentreX(), box.getBottom(), false);
    g.setGradientFill(fill);
    g.fillPath(face);

    // The hairline first and the lip over its top run: the mockup's plate goes
    // from the dark gap straight to the lit row, with the grey edge only down
    // the sides and along the foot. The lip is two rows, full strength then
    // half — that falloff is what reads as light on an edge rather than as a
    // stripe painted along it.
    // Stroked half a pixel inside the edge, so the hairline lies on the face
    // rather than straddling it: straddling, its outer half stood above the
    // lip as a grey row on a scaled display.
    juce::Path edge;
    edge.addRoundedRectangle(box.reduced(0.5f), juce::jmax(0.0f, corner - 0.5f));
    g.setColour(line.withAlpha(alpha));
    g.strokePath(edge, juce::PathStrokeType(1.0f));
    {
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(face);
        const auto inset = corner * 0.7f;
        g.setColour(plateEdgeLit.withAlpha(alpha));
        g.fillRect(box.getX() + inset, box.getY(), box.getWidth() - inset * 2.0f, 1.0f);
        g.setColour(plateEdgeLit.withAlpha(0.5f * alpha));
        g.fillRect(box.getX() + inset, box.getY() + 1.0f, box.getWidth() - inset * 2.0f, 1.0f);
    }

    if (!screws) return;
    const auto in = corner + 2.0f;
    for (const auto centre : {juce::Point<float>(box.getX() + in, box.getY() + in),
                              {box.getRight() - in, box.getY() + in},
                              {box.getX() + in, box.getBottom() - in},
                              {box.getRight() - in, box.getBottom() - in}})
    {
        if (!topScrews && centre.y < box.getCentreY()) continue;
        drawScrew(g, centre, 3.0f, false, alpha);
        // A hair of light over the screw's upper-left shoulder, so it sits in
        // the plate's light the way the lip does rather than floating on it.
        juce::Path glint;
        glint.addCentredArc(centre.x, centre.y, 3.5f, 3.5f, 0.0f, -2.2f, 0.6f, true);
        g.setColour(knobRimLit.withAlpha(0.55f * alpha));
        g.strokePath(glint, juce::PathStrokeType(1.0f));
    }
}
}
