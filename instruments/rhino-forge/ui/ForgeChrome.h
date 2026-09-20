#pragma once

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
// This is the one place the panel uses a raster, and it is 96 pixels square
// rather than the size of a plate: tiled, it stays sharp at any window size and
// costs one small image for the whole panel, where a full-panel texture would
// be both enormous and wrong at every size but the one it was made at.
//
// The brush runs vertically — a per-column bias, with fine per-pixel noise over
// it — because that is the direction the mockup's plates are drawn to have been
// machined in, and it is what keeps a large plate from reading as flat paint.
inline const juce::Image& brushedGrain()
{
    static const juce::Image tile = []
    {
        constexpr int size = 96;
        juce::Image image(juce::Image::ARGB, size, size, true);
        juce::Random random(0x5f0267e);
        for (int x = 0; x < size; ++x)
        {
            // One bias for the whole column is what makes it a brush stroke
            // rather than a sandblast.
            const auto column = random.nextFloat() - 0.5f;
            for (int y = 0; y < size; ++y)
            {
                const auto speck = (random.nextFloat() - 0.5f) * 0.45f;
                const auto amount = juce::jlimit(-1.0f, 1.0f, column + speck);
                const auto white = amount > 0.0f;
                image.setPixelAt(x, y, (white ? juce::Colours::white : juce::Colours::black)
                                           .withAlpha(std::abs(amount)));
            }
        }
        return image;
    }();
    return tile;
}

// Lays the grain over whatever shape has already been filled. Kept very faint:
// the grain is meant to be felt at a glance and only found when looked for.
inline void fillGrain(juce::Graphics& g, const juce::Path& shape, float opacity)
{
    if (opacity <= 0.0f) return;
    juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(shape);
    g.setTiledImageFill(brushedGrain(), 0, 0, opacity);
    g.fillPath(shape);
}

// --- Fasteners --------------------------------------------------------------

// A screw: a machined boss with a socket sunk into it. Four of them hold the
// chassis down at its corners, and they are the largest piece of hardware on
// the panel, so they get a rim, a face lit from the top left, and a socket with
// its own shadow.
inline void drawScrew(juce::Graphics& g, juce::Point<float> centre, float radius, float alpha)
{
    if (radius < 2.0f) return;
    const auto boss = juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre);

    g.setColour(plateEdgeDark.withAlpha(0.7f * alpha));
    g.fillEllipse(boss.expanded(1.0f).translated(0.0f, 1.0f));

    juce::ColourGradient metal(juce::Colour(0xff9aa2b4).withAlpha(alpha), boss.getX(), boss.getY(),
                               juce::Colour(0xff2b303d).withAlpha(alpha), boss.getRight(), boss.getBottom(), false);
    g.setGradientFill(metal);
    g.fillEllipse(boss);

    g.setColour(plateEdgeDark.withAlpha(0.8f * alpha));
    g.drawEllipse(boss.reduced(0.5f), 1.0f);

    // The socket. Filled dark, then given a lit lower rim so it reads as a hole
    // rather than a dot painted on the boss.
    const auto socket = boss.reduced(radius * 0.42f);
    g.setColour(juce::Colour(0xff0b0d14).withAlpha(alpha));
    g.fillEllipse(socket);
    g.setColour(juce::Colour(0xffb6bdcc).withAlpha(0.35f * alpha));
    juce::Path lip;
    lip.addCentredArc(socket.getCentreX(), socket.getCentreY(),
                      socket.getWidth() * 0.5f, socket.getHeight() * 0.5f, 0.0f,
                      juce::MathConstants<float>::pi * 0.35f,
                      juce::MathConstants<float>::pi * 1.25f, true);
    g.strokePath(lip, juce::PathStrokeType(1.0f));
}

// A rivet: the small one, dotted along a plate's own corners. No socket — at
// this size a highlight and a shadow is all that survives, and anything more
// turns into a smudge.
inline void drawRivet(juce::Graphics& g, juce::Point<float> centre, float radius, float alpha)
{
    if (radius < 1.0f) return;
    const auto head = juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre);
    g.setColour(juce::Colour(0xff05070d).withAlpha(0.85f * alpha));
    g.fillEllipse(head);
    g.setColour(juce::Colour(0xff6f7787).withAlpha(0.5f * alpha));
    g.drawEllipse(head.reduced(0.4f).translated(0.0f, -0.4f), 0.8f);
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
                      float cut = plateCut, int corners = everyCorner, float grain = 0.045f)
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
