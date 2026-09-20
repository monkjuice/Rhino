#pragma once

#include <array>
#include <BinaryData.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Forge's material, drawn rather than blitted.
//
// Everything the panel is built out of — a plate, the chassis it sits on, the
// bevel around a well, a screw, a rivet, a run of hatching — is defined once
// here as a path and a gradient, so it is the same material wherever it is used
// and it holds at every size the window allows. Nothing in this file reads a
// pixel from an image: the one raster it does use is a small tile of grain it
// generates itself, which is the one effect a gradient genuinely cannot carry.
//
// This is deliberately not a screenshot cut into slices. The panel resizes over
// a 1.7x span, so a sliced plate would stretch its chamfers and its legend at
// every size but one; a path does not.
namespace rhino::forge::ui
{
// --- The material's palette -------------------------------------------------
//
// A plate is gunmetal lit from above: the top of its face catches the light,
// the foot of it falls away. The two edge colours are what turns that face into
// something raised rather than a rectangle of paint — a lit rim along the top
// and a black one along the bottom is the whole of the trick.
//
// The numbers are measured off the reference rather than chosen, by sampling a
// column straight down through a plate in both and comparing: its chassis sits
// at about RGB 3, its face at (17,19,23), and the lit band along its top edge
// peaks near 100. Two passes were needed. The first had the chassis as light as
// the plates and every plate disappeared into it, which no amount of bevel
// could rescue; the second left the face at (26,30,38) — half a stop light and
// noticeably bluer than the reference's neutral gunmetal — with a rim
// overshooting at 117.
inline const auto chassis        = juce::Colour(0xff020304);
inline const auto chassisLit     = juce::Colour(0xff05060a);
inline const auto plateFaceTop   = juce::Colour(0xff1a1c22);
inline const auto plateFaceFoot  = juce::Colour(0xff0d0e12);
inline const auto plateEdgeLit   = juce::Colour(0xff868d9c);
inline const auto plateEdgeDark  = juce::Colour(0xff020407);
// Stamped legends: a part number, a plate's long name, the markings on a decal.
// Dimmer than mutedText, because a legend is read once and then ignored.
inline const auto legendText     = juce::Colour(0xff6c7386);

// The corner cut every plate shares. Machined, not rounded: the panel reads as
// something assembled out of parts, and a 45 degree corner is what says so.
inline constexpr float plateCut = 11.0f;

// Which corners a chamfer actually cuts. The module plates cut all four; the
// chassis frame cuts only the outer ones, because its inner corners are not
// corners of anything.
enum Corner
{
    topLeft     = 1,
    topRight    = 2,
    bottomRight = 4,
    bottomLeft  = 8,
    everyCorner = topLeft | topRight | bottomRight | bottomLeft
};

// --- Chamfer ----------------------------------------------------------------

// The outline of a plate: a rectangle with its corners cut off. The cut is
// clamped to half the shorter side, so a plate narrower than two cuts collapses
// to a lozenge rather than turning itself inside out.
inline juce::Path chamferedPath(juce::Rectangle<float> box, float cut, int corners = everyCorner)
{
    const auto c = juce::jmax(0.0f, juce::jmin(cut, box.getWidth() * 0.5f, box.getHeight() * 0.5f));
    const auto x1 = box.getX(), y1 = box.getY(), x2 = box.getRight(), y2 = box.getBottom();

    // Walked clockwise from the top-left. A cut corner is two points where an
    // uncut one is a single vertex, which is the whole of the difference.
    juce::Path path;
    path.startNewSubPath((corners & topLeft) != 0 ? x1 + c : x1, y1);

    if ((corners & topRight) != 0)     { path.lineTo(x2 - c, y1); path.lineTo(x2, y1 + c); }
    else                                 path.lineTo(x2, y1);

    if ((corners & bottomRight) != 0)  { path.lineTo(x2, y2 - c); path.lineTo(x2 - c, y2); }
    else                                 path.lineTo(x2, y2);

    if ((corners & bottomLeft) != 0)   { path.lineTo(x1 + c, y2); path.lineTo(x1, y2 - c); }
    else                                 path.lineTo(x1, y2);

    if ((corners & topLeft) != 0)        path.lineTo(x1, y1 + c);
    else                                 path.lineTo(x1, y1);

    path.closeSubPath();
    return path;
}

// --- Grain ------------------------------------------------------------------

// A seamless tile of brushed metal, generated once and tiled thereafter.
//
// This is the one place the panel uses a raster, and it is 128 pixels square
// rather than the size of a plate: tiled, it stays sharp at any window size and
// costs one small image for the whole panel, where a full-panel texture would
// be both enormous and wrong at every size but the one it was made at.
//
// The brush runs *horizontally*, and it is fine.
//
// Two passes at this were wrong in opposite directions and both were caught by
// looking rather than by measuring. The first ran the grain vertically at a
// standard deviation of 2, and at that depth a plate is flat paint. The second
// chased the reference's measured standard deviation of 13 with banded rows
// and a second blown-up pass of the same tile, and landed on television static
// — the number was inflated by a knob sitting inside the patch I sampled.
//
// What the reference actually has is a tight two-pixel weave with a little
// unevenness over it: dense, even, low contrast per pixel. That is what a
// machined face looks like, and it is nothing like noise. Fine tooth, gentle
// drift, and a few hairline scratches for incident.
inline const juce::Image& brushedGrain()
{
    static const juce::Image tile = []
    {
        constexpr int size = 128;
        juce::Image image(juce::Image::ARGB, size, size, true);
        juce::Random random(0x5f0267e);

        // The drift: one value per row, smoothed along its length and wrapped,
        // so the tile joins itself and no seam shows where it repeats.
        std::array<float, size> drift {};
        for (auto& value : drift) value = random.nextFloat() - 0.5f;
        std::array<float, size> smoothed {};
        for (int y = 0; y < size; ++y)
            smoothed[static_cast<size_t>(y)] =
                (drift[static_cast<size_t>((y + size - 1) % size)]
                 + drift[static_cast<size_t>(y)] * 2.0f
                 + drift[static_cast<size_t>((y + 1) % size)]) * 0.25f;

        for (int y = 0; y < size; ++y)
        {
            // The weave itself: alternate rows lit and shaded, which at one
            // pixel each is the two-pixel period the reference has.
            const auto weave = (y % 2 == 0 ? 0.42f : -0.42f);
            for (int x = 0; x < size; ++x)
            {
                const auto speck = (random.nextFloat() - 0.5f) * 0.34f;
                const auto amount = juce::jlimit(-1.0f, 1.0f,
                                                 weave + smoothed[static_cast<size_t>(y)] * 0.5f + speck);
                image.setPixelAt(x, y, (amount > 0.0f ? juce::Colours::white : juce::Colours::black)
                                           .withAlpha(std::abs(amount)));
            }
        }

        // A few hairlines, short and faint. More than this and they stop being
        // marks on a surface and become a pattern in their own right.
        for (int n = 0; n < 4; ++n)
        {
            const auto y = random.nextInt(size);
            const auto length = size / 4 + random.nextInt(size / 2);
            const auto from = random.nextInt(size);
            const auto bright = 0.16f + random.nextFloat() * 0.16f;
            for (int i = 0; i < length; ++i)
            {
                const auto fade = bright * (1.0f - std::abs(i / (float) length - 0.5f) * 1.7f);
                if (fade <= 0.0f) continue;
                image.setPixelAt((from + i) % size, y,
                                 juce::Colours::white.withAlpha(juce::jlimit(0.0f, 1.0f, fade)));
            }
        }
        return image;
    }();
    return tile;
}

// Lays the grain over whatever shape has already been filled.
//
// Drawn at a scale as well as an opacity, because one frequency of noise still
// reads as noise: the fine pass is the tooth of the metal and a second pass of
// the same tile blown up several times is the mottling a rolled sheet has, and
// it is the second one that stops a large plate looking like an even wash.
inline void fillGrain(juce::Graphics& g, const juce::Path& shape, float opacity, float scale = 1.0f)
{
    if (opacity <= 0.0f) return;
    juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(shape);
    juce::FillType fill(brushedGrain(), juce::AffineTransform::scale(scale));
    fill.setOpacity(opacity);
    g.setFillType(fill);
    g.fillPath(shape);
}

// --- Fasteners --------------------------------------------------------------

// The two pieces of hardware the panel is put together with.
//
// A fastener is the one thing here where a drawing loses to a photograph. It
// is small, it is round, and what sells it is the way real metal catches the
// light across a curve — a handful of arcs and gradients gets the shape and
// none of the material, which is why the procedural pair these replace read as
// grey dots however they were tuned. Everything structural is still geometry;
// this is the exception the rule is worth making for.
//
// Both are 64px square and are always drawn smaller than that, so the blit is
// a downscale and stays sharp at any panel size and on a scaled display.
enum class Fastener
{
    // The black cross-head, holding the chassis together at its corners.
    cross,
    // The domed stud, at the corners of the plates bolted onto it.
    dome
};

inline const juce::Image& fastenerImage(Fastener kind)
{
    static const juce::Image cross = juce::ImageCache::getFromMemory(
        BinaryData::screw_cross_png, BinaryData::screw_cross_pngSize);
    static const juce::Image dome = juce::ImageCache::getFromMemory(
        BinaryData::screw_dome_png, BinaryData::screw_dome_pngSize);
    return kind == Fastener::cross ? cross : dome;
}

inline void drawFastener(juce::Graphics& g, juce::Point<float> centre, float radius,
                         Fastener kind, float alpha)
{
    if (radius < 1.0f || alpha <= 0.0f) return;
    const auto& art = fastenerImage(kind);
    if (art.isNull()) return;

    juce::Graphics::ScopedSaveState state(g);
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.setOpacity(alpha);
    g.drawImage(art, juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre),
                juce::RectanglePlacement::centred);
}

// The names the rest of the panel already calls these by.
inline void drawScrew(juce::Graphics& g, juce::Point<float> centre, float radius, float alpha)
{
    drawFastener(g, centre, radius, Fastener::cross, alpha);
}

inline void drawRivet(juce::Graphics& g, juce::Point<float> centre, float radius, float alpha)
{
    drawFastener(g, centre, radius, Fastener::dome, alpha);
}

// --- Markings ---------------------------------------------------------------

// Diagonal hatching, the decal that fills a stretch of plate nothing is written
// on. Clipped to the area given, so a run of it ends square against whatever it
// is butted up to rather than trailing off at an angle.
inline void drawHatch(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour,
                      float thickness = 3.0f, float pitch = 9.0f)
{
    if (area.isEmpty() || pitch <= 0.0f) return;
    juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(area.toNearestInt());
    g.setColour(colour);
    const auto lean = area.getHeight();
    for (auto x = area.getX() - lean; x < area.getRight(); x += pitch)
    {
        juce::Path stripe;
        stripe.startNewSubPath(x, area.getBottom());
        stripe.lineTo(x + lean, area.getY());
        g.strokePath(stripe, juce::PathStrokeType(thickness));
    }
}

// --- Plates -----------------------------------------------------------------

// The edge that turns a face into a raised part: lit along the top, black along
// the foot, following whatever outline it is handed — which is what keeps the
// light consistent across a chamfer instead of stopping at the corners.
inline void strokeBevel(juce::Graphics& g, const juce::Path& outline, juce::Rectangle<float> box,
                        float alpha, float thickness, float lift)
{
    juce::ColourGradient edge(plateEdgeLit.withAlpha(lift * alpha), box.getCentreX(), box.getY(),
                              plateEdgeDark.withAlpha(0.9f * alpha), box.getCentreX(), box.getBottom(), false);
    // The light falls off quickly: most of a plate's height is its dark half,
    // and only the first sliver of it is actually catching anything.
    edge.addColour(0.35, plateEdgeLit.interpolatedWith(plateEdgeDark, 0.7f).withAlpha(0.8f * alpha));
    g.setGradientFill(edge);
    g.strokePath(outline, juce::PathStrokeType(thickness));
}

// And the left-hand edge, which the stroke above cannot reach.
//
// The light on this panel comes from above *and to the left*. A gradient
// running straight down lights the top edge and leaves the left one as dark as
// the right, and a plate then reads as a flat card with a line along its top
// rather than as a part standing off the chassis. This is the second half of
// the bevel: bright at the left edge, gone by a third of the way across, laid
// over the first so the top-left corner is where the two meet and is the
// brightest thing on the plate.
inline void strokeSideLight(juce::Graphics& g, const juce::Path& outline, juce::Rectangle<float> box,
                            float alpha, float thickness)
{
    juce::ColourGradient side(plateEdgeLit.withAlpha(0.85f * alpha), box.getX(), box.getCentreY(),
                              plateEdgeLit.withAlpha(0.0f),
                              box.getX() + box.getWidth() * 0.34f, box.getCentreY(), false);
    g.setGradientFill(side);
    g.strokePath(outline, juce::PathStrokeType(thickness));
}

// The shadow a plate casts on the chassis. Three strokes rather than a blur:
// a blurred drop shadow per module per repaint is a frame cost this panel will
// not spend, and at this radius the difference cannot be seen.
inline void drawPlateShadow(juce::Graphics& g, juce::Rectangle<float> box, float cut, int corners,
                            float alpha)
{
    for (int ring = 3; ring >= 1; --ring)
    {
        const auto spread = static_cast<float>(ring);
        g.setColour(juce::Colours::black.withAlpha(0.22f * alpha / spread));
        g.strokePath(chamferedPath(box.expanded(spread).translated(0.0f, spread * 0.4f),
                                   cut + spread, corners),
                     juce::PathStrokeType(spread * 1.5f));
    }
}

// One plate, complete: shadow, face, grain, and the bevel that raises it.
//
// This is the primitive every module, every decal and the chassis itself is
// drawn with. Everything that makes the panel look machined lives in these few
// lines, which is the point of having them in one place — a module that drew
// its own background would drift away from this the first time either changed.
inline void drawPlate(juce::Graphics& g, juce::Rectangle<float> box, float alpha = 1.0f,
                      float cut = plateCut, int corners = everyCorner, float grain = 0.11f)
{
    if (box.getWidth() < 2.0f || box.getHeight() < 2.0f) return;
    const auto outline = chamferedPath(box, cut, corners);

    drawPlateShadow(g, box, cut, corners, alpha);

    juce::ColourGradient face(plateFaceTop.withAlpha(alpha), box.getCentreX(), box.getY(),
                              plateFaceFoot.withAlpha(alpha), box.getCentreX(), box.getBottom(), false);
    g.setGradientFill(face);
    g.fillPath(outline);

    fillGrain(g, outline, grain * alpha);

    // Two edges with a dark gap between them, which is what the reference's top
    // edge actually is: an outer rim, a shadow, and the milled lip inside it.
    // The lip is the brighter and the wider of the two -- that inversion is
    // what stops the plate reading as a rectangle with a line drawn round it.
    // Three lines in the top few pixels, not one: the outer rim, the milled lip,
    // and a step inside that. Sampling the reference's top edge gives bright
    // bands at +0, +4 and +6 with dark between them, and it is that repetition
    // — rather than any single line's brightness — that reads as machined.
    strokeBevel(g, outline, box, alpha, 1.1f, 0.6f);
    const auto lip = chamferedPath(box.reduced(2.4f), cut - 1.6f, corners);
    strokeBevel(g, lip, box, alpha, 1.6f, 1.0f);
    strokeSideLight(g, lip, box, alpha, 1.6f);
    const auto step = chamferedPath(box.reduced(4.8f), cut - 3.4f, corners);
    strokeBevel(g, step, box, alpha, 1.0f, 0.62f);
    strokeSideLight(g, step, box, alpha * 0.6f, 1.0f);
}

// A module's own panel inside a shared plate. Shallower than a plate — a
// thinner lip, no shadow, no fasteners — because it is a division of one piece
// of metal rather than a second piece bolted onto it, and giving it the full
// relief would say the opposite.
inline void drawInnerPanel(juce::Graphics& g, juce::Rectangle<float> box, float alpha = 1.0f,
                           float cut = 6.0f)
{
    if (box.getWidth() < 2.0f || box.getHeight() < 2.0f) return;
    const auto outline = chamferedPath(box, cut);

    juce::ColourGradient face(plateFaceTop.brighter(0.16f).withAlpha(alpha), box.getCentreX(), box.getY(),
                              plateFaceFoot.withAlpha(alpha), box.getCentreX(), box.getBottom(), false);
    g.setGradientFill(face);
    g.fillPath(outline);
    fillGrain(g, outline, 0.09f * alpha);

    strokeBevel(g, outline, box, alpha * 0.8f, 1.1f, 0.8f);
    strokeSideLight(g, outline, box, alpha * 0.7f, 1.1f);
}

// A well: the inverse of a plate. Sunk into the face rather than raised off it,
// so the light runs the other way — dark at the top where the wall shades the
// floor, faintly lit at the foot where it catches again.
inline void drawWell(juce::Graphics& g, juce::Rectangle<float> box, juce::Colour floorColour,
                     float alpha = 1.0f, float cut = 4.0f, int corners = everyCorner)
{
    if (box.getWidth() < 2.0f || box.getHeight() < 2.0f) return;
    const auto outline = chamferedPath(box, cut, corners);

    g.setColour(floorColour.withAlpha(alpha));
    g.fillPath(outline);

    juce::ColourGradient wall(plateEdgeDark.withAlpha(0.95f * alpha), box.getCentreX(), box.getY(),
                              plateEdgeLit.withAlpha(0.45f * alpha), box.getCentreX(), box.getBottom(), false);
    g.setGradientFill(wall);
    g.strokePath(outline, juce::PathStrokeType(1.2f));
}

// --- Legends ----------------------------------------------------------------

// The stamped lettering a plate carries: small, wide-tracked, and dim. JUCE has
// no tracking control, so the spacing is done by drawing the string a character
// at a time — which at these lengths is a handful of glyphs once per repaint.
inline void drawTrackedText(juce::Graphics& g, const juce::String& textToDraw,
                            juce::Rectangle<float> area, float tracking,
                            juce::Justification justification)
{
    if (textToDraw.isEmpty()) return;
    const auto font = g.getCurrentFont();
    const auto widthOf = [&font] (const juce::String& glyph)
    {
        return juce::GlyphArrangement::getStringWidth(font, glyph);
    };

    auto total = 0.0f;
    for (int i = 0; i < textToDraw.length(); ++i)
        total += widthOf(textToDraw.substring(i, i + 1)) + tracking;
    total -= tracking;

    auto x = area.getX();
    if (justification.testFlags(juce::Justification::horizontallyCentred))
        x = area.getCentreX() - total * 0.5f;
    else if (justification.testFlags(juce::Justification::right))
        x = area.getRight() - total;

    const auto y = area.getCentreY() - font.getHeight() * 0.5f;
    for (int i = 0; i < textToDraw.length(); ++i)
    {
        const auto glyph = textToDraw.substring(i, i + 1);
        const auto glyphWidth = widthOf(glyph);
        g.drawText(glyph, juce::Rectangle<float>(x, y, glyphWidth + 1.0f, font.getHeight()),
                   juce::Justification::centredLeft);
        x += glyphWidth + tracking;
    }
}
}
