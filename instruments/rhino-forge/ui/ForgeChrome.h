#pragma once

// The colours first: every token the material below is drawn with lives in
// ForgeTheme.h, and Merli's own primitives follow it so a Forge primitive can
// hand off to its counterpart in one line.
#include "ForgeTheme.h"
#include "ForgeMerliMaterial.h"
#include <algorithm>
#include <array>
#include <vector>
#include <BinaryData.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Forge's material, drawn rather than blitted.
//
// Shared metal surfaces, grain, bevels and fasteners. Structural parts are
// paths so their cuts stay sharp when resized; the fasteners use small assets.
namespace rhino::forge::ui
{
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

// --- Images the panel makes -------------------------------------------------

// Never give a juce::Image static storage in Forge. On Windows a plain image is
// a Direct2D bitmap, and a static one is released when the plugin's DLL
// detaches, under the loader lock, where NVIDIA's driver waits forever for
// threads that need that lock: RhinoDAW stayed running with no window after
// being closed whenever Forge's panel had been open.
//
// An image the panel makes for itself goes in JUCE's ImageCache instead, under
// a key of its own, and is made again whenever the cache has let it go. JUCE
// empties the cache on the message thread when the last plugin instance goes,
// and lets an image go a few seconds after nothing holds it. getFromMemory
// keys an asset by its address, which in a 64-bit process never sets the top
// bits these keys set, so the two cannot meet.
inline constexpr juce::int64 grainImageKey = 0x46f0'0000'0000'0001;
inline constexpr juce::int64 wheelImageKey = 0x46f0'0000'0000'0002;

template <typename Make>
juce::Image cachedImage(juce::int64 key, Make&& make)
{
    if (auto kept = juce::ImageCache::getFromHashCode(key); kept.isValid())
        return kept;
    auto made = make();
    juce::ImageCache::addImageToCache(made, key);
    return made;
}

// --- Grain ------------------------------------------------------------------

// A seamless tile of brushed metal, generated when first drawn and tiled
// thereafter.
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
inline juce::Image brushedGrain()
{
    return cachedImage(grainImageKey, []
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
    });
}

// Lays the grain over whatever shape has already been filled.
//
// Drawn at a scale as well as an opacity, because one frequency of noise still
// reads as noise: the fine pass is the tooth of the metal and a second pass of
// the same tile blown up several times is the mottling a rolled sheet has, and
// it is the second one that stops a large plate looking like an even wash.
inline void fillGrain(juce::Graphics& g, const juce::Path& shape, float opacity, float scale = 1.0f)
{
    // The grain is the metal. Merli's plates are matte, and the fur around
    // them is texture enough.
    if (isMerli()) return;
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

// Fetched from the ImageCache at every call, as Merli's assets are, and never
// kept (see cachedImage).
inline juce::Image fastenerImage(Fastener kind)
{
    if (kind == Fastener::cross)
        return juce::ImageCache::getFromMemory(BinaryData::screw_cross_png, BinaryData::screw_cross_pngSize);
    // The dome asset has a wide transparent gutter. Trim it on the way out so
    // the requested radius describes the hardware, as it does for the screw.
    return juce::ImageCache::getFromMemory(BinaryData::screw_dome_png, BinaryData::screw_dome_pngSize)
        .getClippedImage({20, 19, 28, 28});
}

inline void drawFastener(juce::Graphics& g, juce::Point<float> centre, float radius,
                         Fastener kind, float alpha)
{
    // Merli's hardware: its large screw where the cross-head holds Forge's
    // chassis together, its small one where the dome stud holds a plate on.
    if (isMerli()) { merli::drawScrew(g, centre, radius, kind == Fastener::cross, alpha); return; }
    if (radius < 1.0f || alpha <= 0.0f) return;
    const auto art = fastenerImage(kind);
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
    // Merli's plates carry no decal: the one stretch Forge hatches is the
    // identity plate, and Merli's header has a cat where that was.
    if (isMerli()) return;
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
                      float cut = plateCut, int corners = everyCorner, float grain = 0.035f)
{
    // Rounded to the module plates' radius at most: a chamfer is a larger cut
    // than the rounding that stands in for it, so a caller's cut can only
    // tighten the corner, never widen it past what the rest of Merli draws.
    if (isMerli()) { merli::drawRoundedPlate(g, box, juce::jmin(cut, 7.0f), alpha, false); return; }
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
    if (isMerli()) { merli::drawRoundedPlate(g, box, juce::jmin(cut, 7.0f), alpha, false); return; }
    if (box.getWidth() < 2.0f || box.getHeight() < 2.0f) return;
    const auto outline = chamferedPath(box, cut);

    juce::ColourGradient face(plateFaceTop.brighter(0.16f).withAlpha(alpha), box.getCentreX(), box.getY(),
                              plateFaceFoot.withAlpha(alpha), box.getCentreX(), box.getBottom(), false);
    g.setGradientFill(face);
    g.fillPath(outline);
    fillGrain(g, outline, 0.035f * alpha);

    strokeBevel(g, outline, box, alpha * 0.8f, 1.1f, 0.8f);
    strokeSideLight(g, outline, box, alpha * 0.7f, 1.1f);
}

// A well: the inverse of a plate. Sunk into the face rather than raised off it,
// so the light runs the other way — dark at the top where the wall shades the
// floor, faintly lit at the foot where it catches again.
inline void drawWell(juce::Graphics& g, juce::Rectangle<float> box, juce::Colour floorColour,
                     float alpha = 1.0f, float cut = 4.0f, int corners = everyCorner)
{
    // Every Merli well is rounded, whichever corners the caller would have
    // cut: the mask was only ever about chamfers. A small cut rounds to 4 so
    // it still reads as a corner, but no cut stays square — the spectral well
    // asks for none because its picture fills it to the wall, and a rounded
    // floor would leave the picture's corners hanging over the plate.
    if (isMerli())
    {
        merli::drawRoundedWell(g, box, floorColour, alpha, cut <= 0.0f ? 0.0f : juce::jmax(4.0f, cut));
        return;
    }
    if (box.getWidth() < 2.0f || box.getHeight() < 2.0f) return;
    const auto outline = chamferedPath(box, cut, corners);

    g.setColour(floorColour.withAlpha(alpha));
    g.fillPath(outline);

    juce::ColourGradient wall(plateEdgeDark.withAlpha(0.95f * alpha), box.getCentreX(), box.getY(),
                              plateEdgeLit.withAlpha(0.45f * alpha), box.getCentreX(), box.getBottom(), false);
    g.setGradientFill(wall);
    g.strokePath(outline, juce::PathStrokeType(1.2f));
}

// Nonrectangular parts use the same finish as the rectangular plates. Insets
// follow the outline, including the long diagonal cuts on the fascia pieces.
inline juce::Path metalPolygon(std::initializer_list<juce::Point<float>> points)
{
    juce::Path shape;
    auto first = true;
    for (const auto point : points)
    {
        if (first) shape.startNewSubPath(point); else shape.lineTo(point);
        first = false;
    }
    shape.closeSubPath();
    return shape;
}

inline void drawMetalPiece(juce::Graphics& g, const juce::Path& shape, float alpha = 1.0f,
                           float grain = 0.035f)
{
    if (isMerli()) { merli::drawMatteFill(g, shape, alpha); return; }
    const auto box = shape.getBounds();
    if (box.getWidth() < 2.0f || box.getHeight() < 2.0f) return;
    g.setColour(juce::Colours::black.withAlpha(0.95f * alpha));
    g.strokePath(shape, juce::PathStrokeType(5.0f));
    juce::ColourGradient face(plateFaceTop.withAlpha(alpha), box.getX(), box.getY(),
                              plateFaceFoot.withAlpha(alpha), box.getRight(), box.getBottom(), false);
    face.addColour(0.25, plateFaceMid.withAlpha(alpha));
    g.setGradientFill(face);
    g.fillPath(shape);
    fillGrain(g, shape, grain * alpha);
    strokeBevel(g, shape, box, alpha, 1.0f, 0.75f);
    for (const auto inset : {2.0f, 4.3f})
    {
        if (box.getWidth() <= inset * 2.0f || box.getHeight() <= inset * 2.0f) continue;
        auto lip = shape;
        lip.applyTransform(shape.getTransformToScaleToFit(box.reduced(inset), false));
        g.setColour(juce::Colours::black.withAlpha(0.9f * alpha));
        g.strokePath(lip, juce::PathStrokeType(2.4f));
        strokeBevel(g, lip, box, alpha, 0.9f, inset < 3.0f ? 0.95f : 0.35f);
        strokeSideLight(g, lip, box, alpha * 0.42f, 0.8f);
    }
}

// A shallow folded strip with a stepped end, used on the module rims and the
// long chassis rails. These are separate pieces, not an unbroken bright border.
inline void drawMetalRail(juce::Graphics& g, juce::Rectangle<float> box, bool reverse = false)
{
    // A rail is a folded strip of the metal Merli has none of; its plates
    // end at their hairline.
    if (isMerli()) return;
    const auto x = box.getX(), y = box.getY(), r = box.getRight(), b = box.getBottom();
    auto shape = metalPolygon({{x + 5, y}, {r, y}, {r - 5, b}, {x, b}});
    if (reverse) shape.applyTransform(juce::AffineTransform::verticalFlip(y + b));
    g.setGradientFill(juce::ColourGradient(railLit, x, y,
                                          railDark, x, b, false));
    g.fillPath(shape);
    g.setColour(railLine);
    g.strokePath(shape, juce::PathStrokeType(0.8f));
    g.setColour(plateEdgeLit.withAlpha(0.7f));
    g.drawLine(x + 6, y + 0.7f, r - 2, y + 0.7f, 0.7f);
}

inline void drawEdgeWear(juce::Graphics& g, juce::Rectangle<float> box, int seed)
{
    // Wear is scratched into a lit edge, and a matte plate has no lit edge
    // to scratch.
    if (isMerli()) return;
    juce::Random random(seed);
    const auto count = juce::jmax(3, static_cast<int>(box.getWidth() / 30.0f));
    for (int i = 0; i < count; ++i)
    {
        const auto x = box.getX() + 10.0f + random.nextFloat() * (box.getWidth() - 20.0f);
        const auto y = i % 3 == 0 ? box.getBottom() - 3.0f : box.getY() + 3.0f;
        const auto length = 0.8f + random.nextFloat() * 3.4f;
        g.setColour(plateEdgeLit.withAlpha(0.15f + random.nextFloat() * 0.26f));
        g.drawLine(x, y, x + length, y + random.nextFloat() * 0.6f, 0.6f);
        g.setColour(juce::Colours::black.withAlpha(0.7f));
        g.drawLine(x, y + 0.8f, x + length, y + 0.8f, 0.65f);
    }
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
    // Resizing moves fixed legends without changing their metrics. Keep the
    // widths so a full-resolution redraw need not shape each character twice.
    // This bounded cache is used on the message thread, like panelFont.
    struct Metrics
    {
        juce::Font font;
        juce::String lettering;
        std::vector<float> widths;
    };
    static std::vector<Metrics> cached;
    auto found = std::find_if(cached.begin(), cached.end(), [&] (const auto& entry)
    {
        return entry.font == font && entry.lettering == textToDraw;
    });
    if (found == cached.end())
    {
        if (cached.size() == 128) cached.erase(cached.begin());
        Metrics entry {font, textToDraw, {}};
        for (int i = 0; i < textToDraw.length(); ++i)
            entry.widths.push_back(juce::GlyphArrangement::getStringWidth(font, textToDraw.substring(i, i + 1)));
        cached.push_back(std::move(entry));
        found = cached.end() - 1;
    }
    const auto widthOf = [&found] (int i)
    {
        return found->widths[static_cast<size_t>(i)];
    };

    auto total = 0.0f;
    for (int i = 0; i < textToDraw.length(); ++i)
        total += widthOf(i) + tracking;
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
        const auto glyphWidth = widthOf(i);
        g.drawText(glyph, juce::Rectangle<float>(x, y, glyphWidth + 1.0f, font.getHeight()),
                   juce::Justification::centredLeft);
        x += glyphWidth + tracking;
    }
}
}
