#pragma once

#include "ForgeChrome.h"
#include "ForgePlacement.h"
#include "ForgeType.h"
#include "ForgeMerliMaterial.h"

// Merli's chassis and module shells: the fur, the ears, the header plates, the
// well the modules stand in, the shelf, and the rounded plate every module is
// drawn on. Everything here goes into the cached chassis and plates layers,
// which is why it may fetch textures and build a mask image: a rebuild pays
// for it once per size, and no frame pays for it at all.
//
// The layout is Forge's. Every rectangle is read from ForgePlacement.h or
// worked out from the window's width and height, never from a fixed size, and
// nothing a component is given moves.
namespace rhino::forge::ui
{
// Defined in ForgePanels.h, which is the one header that includes this one.
// The sky behind the ARP plate's falling notes is the same in both themes, so
// Merli's housing borrows it rather than carrying a second copy of the stars.
inline void drawArpSky(juce::Graphics& g, juce::Rectangle<int> wellBounds);
}

namespace rhino::forge::ui::merli
{
// Where the fur begins. Above it is backdrop, so the ears read as a silhouette
// against it rather than as blotches on fur; below it, a band of fur shows
// over the header plates before they start.
inline constexpr float furBandTop = 28.0f;

// Where the cat plate and the wordmark's box start: a row above the tabs well,
// so the three line up, and eight rows into the fur.
inline constexpr float headerPlateTop = 36.0f;

// The ears as drawn: their width on the panel, and where the art's box sits so
// that the tip is two rows from the top and the ear stands at the very end of
// the head. The art carries 18.5 px of margin outside the ear's outer edge
// where it meets the fur, so the box hangs that far past the window: at a
// 6 px overhang the band's square corner stuck out 12.5 px beyond each ear,
// a head wider than its ears at the top. The art is about twice this size, so
// a scaled display stays sharp.
inline constexpr float earWidth = 76.0f;
inline constexpr float earTop = -5.0f;
inline constexpr float earOverhang = 18.5f;

// One ear, from the art: a finished ear with its pink and its lit hair, filled
// solid and faded out toward its base so it grows out of the band. The cat's
// right ear is the one on the viewer's left.
inline void drawEar(juce::Graphics& g, float panelWidth, bool onLeft)
{
    const auto art = image(onLeft ? Asset::earRight : Asset::earLeft);
    if (art.isNull()) return;
    const auto height = earWidth * static_cast<float>(art.getHeight()) / static_cast<float>(art.getWidth());
    const auto x = onLeft ? -earOverhang : panelWidth - earWidth + earOverhang;
    juce::Graphics::ScopedSaveState state(g);
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    // An image is drawn at the current colour's alpha, and the last colour
    // set was the band's rim at 55%: without this the ears came out at half
    // strength.
    g.setOpacity(1.0f);
    g.drawImage(art, {x, earTop, earWidth, height}, juce::RectanglePlacement::stretchToFit);
}

// The fur: the band across the header, the side margins, the foot and the
// shelf behind its plates, one shape filled with one tile at its own size.
// The tile was made seamless and carries its own tone (the light tips kept,
// the mids taken down to the mockup's coat), so it is laid down as it is.
//
// From 80 px left of the window's edge: one small symmetric whorl survives in
// the tile, and from this origin every copy of it in the header falls under a
// plate or the tabs well, at the default width and at the minimum.
inline juce::Point<float> furOrigin() { return {-80.0f, 0.0f}; }

inline void drawFur(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    const auto w = bounds.getWidth(), h = bounds.getHeight();
    juce::Path fur;
    fur.addRoundedRectangle(0.0f, furBandTop, w, h - furBandTop, 12.0f, 12.0f, false, false, true, true);

    g.setColour(chassis);
    g.fillPath(fur);
    fillTexture(g, fur, Asset::fur, 1.0f, 1.0f, furOrigin());

    // The lip where the fur meets the window's bottom edge, following the
    // rounded corners because it is clipped to the fur rather than drawn
    // across it, and the lit rim along the band's top edge.
    {
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(fur);
        g.setColour(furLip);
        g.fillRect(0.0f, h - 2.0f, w, 2.0f);
    }
    // The rim stops short of the ears: their art fades toward its base, and
    // a lit line under that fade read as a wire across each ear.
    const auto rimInset = earWidth - earOverhang;
    g.setColour(furRimLit.withAlpha(0.55f));
    g.fillRect(rimInset, furBandTop, w - rimInset * 2.0f, 1.0f);

    drawEar(g, w, true);
    drawEar(g, w, false);
}

// The well the modules stand in: the panel texture at its own size, then the
// well floor at 60% over it, so the gaps between modules come out near-black
// with a faint warm speckle rather than flat. A dark row round its edge is
// what sinks it into the fur.
inline void drawModuleWell(juce::Graphics& g, juce::Rectangle<float> box)
{
    if (box.getWidth() < 2.0f || box.getHeight() < 2.0f) return;
    juce::Path bed;
    bed.addRoundedRectangle(box, 10.0f);
    g.setColour(chassisWellFloor);
    g.fillPath(bed);
    fillTexture(g, bed, Asset::panel, 1.0f, 1.0f, {box.getX(), box.getY()});
    g.setColour(chassisWellFloor.withAlpha(0.6f));
    g.fillPath(bed);

    juce::Path wall;
    wall.addRoundedRectangle(box.reduced(0.5f), 9.5f);
    g.setColour(juce::Colours::black.withAlpha(0.85f));
    g.strokePath(wall, juce::PathStrokeType(1.0f));
}

// The wordmark's geometry is worked out for letters 40 px tall, the size the
// header gives them, and scaled with them when a narrow panel fits them
// smaller, so the shadow and the hairs keep to the letters.
inline constexpr float wordmarkDesignHeight = 40.0f;

// The shadow the letters cast on the fur: a wide soft one that sinks them into
// the coat and a tight one where they rest on it. Blurred from the letters'
// own alpha at the layer's resolution, in an image padded wider than the blur
// reaches, because a drop shadow is blurred inside its source's bounds.
inline void drawLetterShadows(juce::Graphics& g, const juce::Image& logo, juce::Rectangle<float> target)
{
    const auto scale = juce::jmax(1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const auto size = target.getHeight() / wordmarkDesignHeight;
    const auto area = target.expanded(12.0f * size);
    juce::Image letters(juce::Image::ARGB, juce::roundToInt(area.getWidth() * scale),
                        juce::roundToInt(area.getHeight() * scale), true, juce::SoftwareImageType());
    {
        juce::Graphics into(letters);
        into.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        into.drawImage(logo, (target - area.getPosition()) * scale, juce::RectanglePlacement::stretchToFit);
    }
    juce::Graphics::ScopedSaveState state(g);
    g.addTransform(juce::AffineTransform::scale(1.0f / scale).translated(area.getX(), area.getY()));
    const auto cast = [&] (float radius, float drop, float alpha)
    {
        juce::DropShadow(juce::Colours::black.withAlpha(alpha), juce::jmax(1, juce::roundToInt(radius * size * scale)),
                         {0, juce::roundToInt(drop * size * scale)}).drawForImage(g, letters);
    };
    cast(5.0f, 3.0f, 0.85f);
    cast(2.0f, 1.5f, 0.9f);
}

// Where the letters meet the fur: in every column of the logo, its lowest
// opaque pixel and the outward normal of the edge there. A column whose lowest
// edge faces sideways more than down is a letter's flank and grows no hair;
// the inner edges, the counters and the undersides of arms with metal below
// them, are never a column's lowest and grow none either, which is what keeps
// the letters resting on the coat rather than furred themselves.
struct LetterFoot { float x, y, nx, ny; };

inline std::vector<LetterFoot> letterFeet(const juce::Image& logo)
{
    std::vector<LetterFoot> feet;
    const juce::Image::BitmapData pixels(logo, juce::Image::BitmapData::readOnly);
    const auto w = logo.getWidth(), h = logo.getHeight();
    const auto opacity = [&] (int x, int y)
    {
        return x < 0 || y < 0 || x >= w || y >= h ? 0.0f : pixels.getPixelColour(x, y).getFloatAlpha();
    };
    for (int x = 0; x < w; ++x)
        for (int y = h - 1; y >= 0; --y)
        {
            if (opacity(x, y) < 0.5f) continue;
            const auto gx = opacity(x + 1, y - 1) + 2.0f * opacity(x + 1, y) + opacity(x + 1, y + 1)
                          - opacity(x - 1, y - 1) - 2.0f * opacity(x - 1, y) - opacity(x - 1, y + 1);
            const auto gy = opacity(x - 1, y + 1) + 2.0f * opacity(x, y + 1) + opacity(x + 1, y + 1)
                          - opacity(x - 1, y - 1) - 2.0f * opacity(x, y - 1) - opacity(x + 1, y - 1);
            const auto length = std::hypot(gx, gy);
            if (length > 0.001f && -gy / length > 0.3f)
                feet.push_back({static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, -gx / length, -gy / length});
            break;
        }
    return feet;
}

// A value every `period` along the foot, eased between, so neighbouring hairs
// grow to like heights and lean alike: that is what reads as a coat rather
// than as stubble.
struct FurNoise
{
    FurNoise(juce::Random& random, float span, float periodToUse) : period(periodToUse)
    {
        for (int i = 0, count = static_cast<int>(span / period) + 2; i < count; ++i)
            knots.push_back(random.nextFloat());
    }

    float operator()(float at) const
    {
        const auto t = juce::jmax(0.0f, at / period);
        const auto i = juce::jmin(static_cast<int>(t), static_cast<int>(knots.size()) - 2);
        auto u = juce::jlimit(0.0f, 1.0f, t - static_cast<float>(i));
        u = u * u * (3.0f - 2.0f * u);
        return knots[static_cast<size_t>(i)] + (knots[static_cast<size_t>(i) + 1] - knots[static_cast<size_t>(i)]) * u;
    }

    float period;
    std::vector<float> knots;
};

// One hair: a tapered blade from its root in the fur to a fine tip over the
// letter, bending a little along its length. Its outer half also goes into
// `tips`, which is lit the way the coat's own hairs are lit at their ends.
inline void addHair(juce::Path& hairs, juce::Path& tips, juce::Point<float> root, float angle, float bend,
                    float length, float width)
{
    constexpr int segments = 6;
    std::array<juce::Point<float>, segments + 1> left, right;
    for (int i = 0; i <= segments; ++i)
    {
        const auto t = static_cast<float>(i) / static_cast<float>(segments);
        const auto heading = angle + bend * t;
        const auto spine = root + juce::Point<float>(std::cos(heading), std::sin(heading)) * (length * t);
        const auto half = width * 0.5f * std::pow(1.0f - t, 0.8f) + 0.08f;
        const auto across = juce::Point<float>(-std::sin(heading), std::cos(heading)) * half;
        left[static_cast<size_t>(i)] = spine + across;
        right[static_cast<size_t>(i)] = spine - across;
    }
    const auto outline = [&] (juce::Path& into, int from)
    {
        into.startNewSubPath(left[static_cast<size_t>(from)]);
        for (int i = from + 1; i <= segments; ++i) into.lineTo(left[static_cast<size_t>(i)]);
        for (int i = segments; i >= from; --i) into.lineTo(right[static_cast<size_t>(i)]);
        into.closeSubPath();
    };
    outline(hairs, 0);
    outline(tips, segments / 2);
}

// The fringe of fur over the letters' feet: two hairs rooted a pixel or two
// out in the coat below every column's foot, growing back over the metal. The
// hairs are filled with the fur tile from the band's own origin, so each one
// is the coat around it carried over the edge, lit toward its tip; a shadow
// under each, on the letters alone, lifts it off the metal. The seed is fixed,
// so every rebuild grows the same coat.
inline void drawFurOverLetters(juce::Graphics& g, const juce::Image& logo, juce::Rectangle<float> target)
{
    // Logo pixels to design units, and design units to the panel.
    const auto unit = wordmarkDesignHeight / static_cast<float>(logo.getHeight());
    const auto size = target.getHeight() / wordmarkDesignHeight;
    juce::Random random(0x5eed);
    // The noise is read along x at 1.3 and down at 0.7; twice the width covers
    // both.
    const auto span = static_cast<float>(logo.getWidth()) * unit * 2.0f;
    const FurNoise tall(random, span, 5.0f), leaning(random, span, 14.0f);

    juce::Path hairs, tips;
    for (const auto& foot : letterFeet(logo))
        for (int i = 0; i < 2; ++i)
        {
            const auto x = foot.x * unit, y = foot.y * unit;
            const auto key = x * 1.3f + y * 0.7f;
            const auto height = (1.0f + 4.5f * std::pow(tall(key), 1.6f)) * (0.6f + 0.55f * random.nextFloat());
            const auto angle = std::atan2(-foot.ny, -foot.nx) + (leaning(key) - 0.5f) * 1.4f
                             + (random.nextFloat() - 0.5f) * 0.36f;
            const auto along = (random.nextFloat() - 0.5f) * 1.2f;
            const auto depth = 0.8f + 1.2f * random.nextFloat();
            const juce::Point<float> root(x + foot.nx * depth - foot.ny * along, y + foot.ny * depth + foot.nx * along);
            const auto bend = (random.nextFloat() - 0.5f) * 0.7f;
            addHair(hairs, tips, root, angle, bend, height + depth, 0.45f + 0.4f * random.nextFloat());
        }
    const auto toPanel = juce::AffineTransform::scale(size).translated(target.getX(), target.getY());
    hairs.applyTransform(toPanel);
    tips.applyTransform(toPanel);

    {
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(logo, juce::AffineTransform::scale(size * unit).translated(target.getX(), target.getY()));
        g.setColour(juce::Colours::black.withAlpha(0.45f));
        g.fillPath(hairs, juce::AffineTransform::translation(0.0f, 0.8f * size));
    }
    fillTexture(g, hairs, Asset::fur, 1.0f, 1.0f, furOrigin());
    g.setColour(furRimLit.withAlpha(0.45f));
    g.fillPath(tips);
}

// The wordmark, standing in the fur with no plate under it. Forge sets the logo
// at a fixed origin on an irregular header piece; Merli centres it in the box
// a plate would fill, less a plate's margins: 40 px tall, three under Forge's
// 43, because the box starts at headerPlateTop.
inline void drawWordmarkOnFur(juce::Graphics& g, juce::Rectangle<float> box)
{
    const auto logo = juce::ImageCache::getFromMemory(BinaryData::forge_logo_png, BinaryData::forge_logo_pngSize);
    if (logo.isNull()) return;
    const auto target = juce::RectanglePlacement(juce::RectanglePlacement::centred)
                            .appliedTo(logo.getBounds().toFloat(), box.reduced(14.0f, 7.0f));
    if (target.getHeight() < 4.0f) return;

    drawLetterShadows(g, logo, target);
    {
        juce::Graphics::ScopedSaveState state(g);
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        // Full strength, whatever the fur's rim left the colour at: an image
        // takes the current colour's alpha.
        g.setOpacity(1.0f);
        g.drawImage(logo, target, juce::RectanglePlacement::stretchToFit);
    }
    drawFurOverLetters(g, logo, target);
}

// The cat, lifted off merli_title_plate.png as a mask. The asset's cat sits on
// a plate face of its own, so blitting the crop would leave a rectangle of the
// wrong grey around it; reading the silhouette out by brightness and filling
// its alpha with a token puts the cat on Merli's plate and nothing else. The
// crop is the cat's own extent, ending at the tip of its tail: any wider takes
// in a scratch and the edge of the asset's corner screw, which are grey and so
// pass the gate. The warmth gate drops the orange streaks that cross into it.
// A software image, built per rebuild and let go: nothing here is static.
inline constexpr int catArtWidth = 146;
inline constexpr int catArtHeight = 89;

inline juce::Image catSilhouette()
{
    const auto plate = image(Asset::titlePlate);
    if (plate.isNull()) return {};
    const auto crop = juce::Rectangle<int>(325, 24, catArtWidth, catArtHeight).getIntersection(plate.getBounds());
    if (crop.isEmpty()) return {};

    juce::Image mask(juce::Image::ARGB, crop.getWidth(), crop.getHeight(), true, juce::SoftwareImageType());
    const juce::Image::BitmapData from(plate, juce::Image::BitmapData::readOnly);
    juce::Image::BitmapData to(mask, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < crop.getHeight(); ++y)
        for (int x = 0; x < crop.getWidth(); ++x)
        {
            const auto pixel = from.getPixelColour(crop.getX() + x, crop.getY() + y);
            const auto level = (pixel.getFloatRed() + pixel.getFloatGreen() + pixel.getFloatBlue()) / 3.0f;
            const auto warm = pixel.getFloatRed() - pixel.getFloatBlue();
            const auto alpha = warm > 0.16f ? 0.0f : juce::jlimit(0.0f, 1.0f, (level - 0.11f) / 0.08f);
            to.setPixelColour(x, y, juce::Colours::white.withAlpha(alpha));
        }
    return mask;
}

// The cat plate and the housing beside it, which carries LOAD and SAVE. The
// housing starts before the status line, which is a fixed rectangle whose text
// is set right against LOAD, so a plate edge anywhere between the line and the
// button would fall under its last glyph; that also gives its large screw the
// room at the left end the mockup puts it in.
inline juce::Rectangle<float> presetHousingBounds(juce::Rectangle<int> componentBounds)
{
    const auto name = presetLabelBounds(componentBounds).toFloat();
    const auto save = presetButtonBounds(componentBounds, true).toFloat();
    return juce::Rectangle<float>::leftTopRightBottom(name.getX() - 32.0f, 52.0f, save.getRight() + 8.0f, 90.0f);
}

inline juce::Rectangle<float> catPlateBounds(juce::Rectangle<int> componentBounds)
{
    const auto split = static_cast<float>(headerSplit(componentBounds.getWidth()));
    return juce::Rectangle<float>::leftTopRightBottom(split + 8.0f, headerPlateTop,
                                                      presetHousingBounds(componentBounds).getX() - 8.0f, 90.0f);
}

// Where the cat stands on the plate: a little under half size, and never more
// than a third of the plate, which the loaded preset's name shares with it;
// toward the plate's right end as it does on the asset.
inline juce::Rectangle<float> catBounds(juce::Rectangle<float> plate)
{
    const auto scale = juce::jmin(0.46f, plate.getWidth() * 0.34f / static_cast<float>(catArtWidth));
    if (scale <= 0.1f) return {};
    const auto width = static_cast<float>(catArtWidth) * scale;
    const auto height = static_cast<float>(catArtHeight) * scale;
    return juce::Rectangle<float>(width, height).withCentre({plate.getRight() - 18.0f - width * 0.5f, plate.getCentreY()});
}

// Where the loaded preset is named on the cat plate: everything left of the
// cat. The words are drawn live over this layer (ui/ForgePresetPlate.h); the
// plate itself carries none.
inline juce::Rectangle<int> catPlateTextBounds(juce::Rectangle<int> componentBounds)
{
    const auto plate = catPlateBounds(componentBounds);
    const auto cat = catBounds(plate);
    const auto right = cat.isEmpty() ? plate.getRight() - 12.0f : cat.getX() - 8.0f;
    return juce::Rectangle<float>::leftTopRightBottom(plate.getX() + 14.0f, plate.getY() + 5.0f,
                                                      right, plate.getBottom() - 5.0f).toNearestInt();
}

// The plate that stands where Forge's identity plate was. The cat is the
// maker's mark, at its right end; the preset is named to its left, live.
inline void drawCatPlate(juce::Graphics& g, juce::Rectangle<float> box)
{
    drawRoundedPlate(g, box, 6.0f, 1.0f, true);
    const auto cat = catSilhouette();
    const auto target = catBounds(box);
    if (cat.isNull() || target.isEmpty()) return;
    juce::Graphics::ScopedSaveState state(g);
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.setColour(plateEdgeLit.withAlpha(0.75f));
    g.drawImage(cat, target, juce::RectanglePlacement::centred, true);
}

// The shelf's three plates. The wheels leave the deck plate's corners free,
// so it carries screws; the ARP face and the note guide's well each run to
// within seven pixels of their plate's edge, which is where a screw would go,
// so those two plates are plain.
inline void drawDeckPlates(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    drawRoundedPlate(g, deckLeftBounds(bounds).toFloat(), 7.0f, 1.0f, true);
}

inline void drawNoteGuideHousing(juce::Graphics& g, juce::Rectangle<int> componentBounds)
{
    const auto plate = noteGuidePlateBounds(componentBounds).toFloat();
    drawRoundedPlate(g, plate, 7.0f, 1.0f, false);
    drawRoundedWell(g, plate.reduced(6.0f), plateWellFloor, 1.0f, 5.0f);
}

// The face well is a rounded rectangle on arpPlateFace at arpPlateFaceCut,
// which is the outline ForgeArpPlate.h lights when the panes are open; the
// two have to agree or the light would miss the well's corners.
inline void drawArpPlateHousing(juce::Graphics& g, juce::Rectangle<int> componentBounds)
{
    drawRoundedPlate(g, arpPlateBounds(componentBounds).toFloat(), 7.0f, 1.0f, false);
    drawRoundedWell(g, arpPlateFace(componentBounds).toFloat(), plateWellFloor, 1.0f, arpPlateFaceCut);
    // The rule between the door and the slot buttons, as Forge draws it.
    const auto name = arpPlateNameBounds(componentBounds).toFloat();
    g.setColour(plateEdgeLit.withAlpha(0.16f));
    g.drawLine(name.getRight() + 5.0f, name.getY() + 6.0f, name.getRight() + 5.0f,
               name.getBottom() - 6.0f, 0.8f);
    drawArpSky(g, arpWaterfallBounds(componentBounds));
}

// The whole chassis, back to front: backdrop, fur, the module well, the header
// plates, the shelf.
inline void drawBackdrop(juce::Graphics& g, juce::Rectangle<int> componentBounds)
{
    const auto bounds = componentBounds.toFloat();
    const auto w = bounds.getWidth();
    const auto width = componentBounds.getWidth();

    g.fillAll(backdrop);
    drawFur(g, bounds);

    // Seven rows of fur below the header plates, six above the shelf, and the
    // well's nine-pixel margin at the sides, as Forge's has.
    const auto shelfTop = static_cast<float>(keyboardStrip(componentBounds).getY());
    drawModuleWell(g, {9.0f, 97.0f, w - 18.0f, shelfTop - 6.0f - 97.0f});

    // The header. The tabs well is Forge's; the wordmark's box ends short of
    // it, which at the minimum width is well inside Forge's fixed plate edge.
    const auto tabs = tabBounds(0, width).getUnion(tabBounds(tabCount - 1, width)).toFloat().expanded(6.0f, 5.0f);
    drawWordmarkOnFur(g, juce::Rectangle<float>::leftTopRightBottom(12.0f, headerPlateTop, tabs.getX() - 11.0f, 90.0f));
    drawRoundedWell(g, tabs, tabWellFloor, 1.0f, 6.0f);

    // The right-hand end is two plates: the cat's, and a housing carrying the
    // status line with the two preset buttons.
    const auto housing = presetHousingBounds(componentBounds);
    drawCatPlate(g, catPlateBounds(componentBounds));
    drawRoundedPlate(g, housing, 6.0f, 1.0f, false);
    drawScrew(g, {housing.getX() + 11.0f, housing.getCentreY()}, 5.5f, true, 1.0f);

    // The shelf. The keys sit in a well with a black edge; the fur shows in the
    // gaps between it and the plates either side.
    const auto keys = keyboardBounds(componentBounds).toFloat().expanded(4.0f, 3.0f);
    g.setColour(keyboardWellFloor);
    g.fillRoundedRectangle(keys, 4.0f);
    g.setColour(juce::Colours::black);
    g.drawRoundedRectangle(keys.reduced(0.5f), 3.5f, 1.0f);
    drawDeckPlates(g, componentBounds);
    drawArpPlateHousing(g, componentBounds);
    drawNoteGuideHousing(g, componentBounds);
}

// Which modules get a paw from the plate. One with an EnableLed gets its paw
// from the LED, which draws one in Merli; one with a drag handle has a card
// where the paw would go; one with bank cards and no handle, the rack, has
// its title held to the gutter before the first card and no room to shift it.
inline bool titleWearsPaw(const Module& module)
{
    return module.enableId == nullptr && module.handleSource == 0 && bankCount(module) == 1;
}

// A module's shell: the rounded plate, the paw and the title, the stamped
// code and GLOBAL's rule. No badge, no rails, no wear — the header is flat.
inline void drawModuleShell(juce::Graphics& g, juce::Rectangle<int> area, const Module& module, bool on,
                            const juce::String& code)
{
    const auto box = area.toFloat();
    const auto id = juce::String(module.id);
    const auto grouped = module.group != nullptr;
    const auto lower = module.row > 0;
    // A grouped module's own panel sits inside the plate drawGroupPlate laid
    // down for the group, at Forge's inset. Its screws are its own, as the
    // mockup's SUB and NOISE have them, and only at the foot: at the top-left
    // corner of so narrow a panel a screw crowds the paw beside it.
    if (grouped)
        drawRoundedPlate(g, box.withTrimmedBottom(static_cast<float>(plateFootMargin)).reduced(4.0f, 3.0f),
                         5.0f, 1.0f, true, false);
    else
        drawRoundedPlate(g, box, 7.0f, 1.0f, true);

    if (module.handleSource == 0)
    {
        auto header = area.withHeight(headerHeight).reduced(grouped ? 8 : 12, 0);
        if (module.enableId != nullptr)
            header.removeFromLeft(headerHeight);
        else if (titleWearsPaw(module))
        {
            // The paw stands where the lamp would, and the title moves over
            // for it. Paint only: the rectangle the layout gave the module is
            // untouched.
            const auto paw = header.removeFromLeft(18).toFloat();
            drawPaw(g, juce::Rectangle<float>(14.0f, 13.0f).withCentre({paw.getX() + 7.0f, paw.getCentreY()}),
                    pawTint, on ? 1.0f : 0.45f);
        }
        // A module with a field in its header stops its title short of it, as
        // Forge's does.
        if (seatsRowInHeader(module))
            header = header.withRight(juce::jmax(header.getX(), headerCarve(area, module).getX() - 4));
        g.setColour(text.withAlpha(on ? 1.0f : 0.55f));
        g.setFont(panelFont(Face::header, 14.5f));
        g.drawFittedText(module.title, header, juce::Justification::centredLeft, 1, 0.85f);
    }

    if (!grouped && (lower || id == "filter"))
    {
        g.setFont(panelFont(Face::label, 12.5f));
        g.setColour(legendText);
        drawTrackedText(g, code, box.withHeight(static_cast<float>(headerHeight)).reduced(14.0f, 0.0f), 0.7f,
                        juce::Justification::centredRight);
    }
    if (id == "global")
    {
        const auto body = controlArea(area, module).toFloat();
        const auto y = body.getY() + body.getHeight() * 0.66f;
        g.setColour(plateEdgeLit.withAlpha(0.24f));
        g.drawLine(box.getX() + 12.0f, y, box.getRight() - 12.0f, y, 0.7f);
    }
}

// The plate a group shares. Its corners are under its members' panels, so
// screws there would be hidden; the members carry none either.
inline void drawGroupPlate(juce::Graphics& g, juce::Rectangle<int> area)
{
    drawRoundedPlate(g, area.toFloat(), 7.0f, 1.0f, false);
}
}
