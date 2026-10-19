#pragma once

#include "ForgeChrome.h"
#include "ForgePlacement.h"
#include "ForgeType.h"
#include "ForgeMerliChassis.h"

// Static chassis furniture. Editor caches this layer at the display's pixel
// scale; the irregular plates, engraving and wear are never rebuilt per frame.
namespace rhino::forge::ui
{
inline const juce::String unitMark = juce::String::fromUTF8("\xe5\x88\x9d\xe5\x8f\xb7\xe6\xa9\x9f");

inline void stamp(juce::Graphics& g, const juce::String& label, juce::Rectangle<float> box,
                  float size = 10.0f, float tracking = 0.8f,
                  juce::Justification alignment = juce::Justification::centredLeft)
{
    g.setFont(panelFont(Face::label, size));
    g.setColour(legendText);
    drawTrackedText(g, label, box, tracking, alignment);
}

inline juce::Path housingOutline(juce::Rectangle<float> box, float footCut = 10.0f)
{
    const auto x = box.getX(), y = box.getY(), r = box.getRight(), b = box.getBottom();
    return metalPolygon({{x + 11, y}, {r - 10, y}, {r, y + 10},
                         {r, b - footCut}, {r - footCut, b}, {x + 9, b}, {x, b - 9}, {x, y + 11}});
}

inline void drawHousing(juce::Graphics& g, juce::Rectangle<float> box, float footCut = 10.0f)
{
    drawMetalPiece(g, housingOutline(box, footCut));
    drawMetalRail(g, {box.getX() + 13, box.getY() + 2, juce::jmin(88.0f, box.getWidth() * 0.38f), 4});
    drawMetalRail(g, {box.getX() + 30, box.getBottom() - 5, box.getWidth() - footCut - 45, 3}, true);
    drawEdgeWear(g, box, juce::roundToInt(box.getX() * 7 + box.getY()));
    drawRivet(g, {box.getX() + 7, box.getY() + 12}, 2.1f, 0.8f);
    drawRivet(g, {box.getRight() - 9, box.getBottom() - 10}, 4.4f, 1.0f);
}

inline void drawModuleDetail(juce::Graphics& g, juce::Rectangle<int> area, const Module& module,
                             bool on, const juce::String& detailOverride, int detailRightInset)
{
    const auto detail = detailOverride.isNotEmpty() ? detailOverride : juce::String(module.detail);
    if (detail.isEmpty()) return;
    // Static labels have their own positions below the stamped part numbers.
    const auto secondary = juce::String(module.id) == "global" || juce::String(module.id) == "macros";
    auto header = area.withHeight(headerHeight).reduced(14, 0);
    if (secondary) header = header.translated(0, 24).withHeight(16);
    if (module.enableId != nullptr) header.removeFromLeft(headerHeight);
    if (module.display == Display::envelope || module.display == Display::lfo) detailRightInset += 48;
    header.removeFromRight(juce::jlimit(0, header.getWidth(), detailRightInset));
    g.setColour(mutedText.withAlpha(on ? 1.0f : 0.45f));
    g.setFont(panelFont(Face::label, 11.5f));
    g.drawText(detail, header, secondary && juce::String(module.id) == "global"
                                  ? juce::Justification::centredLeft : juce::Justification::centredRight);
}

inline void drawModuleShell(juce::Graphics& g, juce::Rectangle<int> area, const Module& module, bool on,
                            juce::Colour /*accent*/, const juce::String& code = {})
{
    if (isMerli()) { merli::drawModuleShell(g, area, module, on, code); return; }
    const auto box = area.toFloat();
    const auto id = juce::String(module.id);
    const auto grouped = module.group != nullptr;
    const auto lower = module.row > 0;
    if (grouped)
    {
        const auto inner = box.withTrimmedBottom(plateFootMargin).reduced(4.0f, 3.0f);
        drawMetalPiece(g, housingOutline(inner, id == "noise" ? 27.0f : 5.0f));
        drawMetalRail(g, {inner.getX() + 8, inner.getY() + 2, juce::jmin(68.0f, inner.getWidth() - 18), 3});
        drawEdgeWear(g, inner, id == "noise" ? 76 : 35);
    }
    else
        drawHousing(g, box, id == "global" || id == "macros" ? 24.0f : 10.0f);

    // The title sits on a shallow overlapping strip. Its angled end is visible
    // on GLOBAL/MACROS/FILTER; oscillator headers continue into their displays.
    if (module.handleSource == 0)
    {
        const auto badgeWidth = static_cast<float>(headerBadgeWidth(area, module));
        const auto x = box.getX() + 6, y = box.getY() + 7;
        const auto badge = metalPolygon({{x + 3, y}, {x + badgeWidth, y},
                                         {x + badgeWidth - 13, y + 23}, {x, y + 23}, {x, y + 3}});
        g.setGradientFill(juce::ColourGradient(badgeTop, x, y,
                                              badgeFoot, x, y + 23, false));
        g.fillPath(badge);
        g.setColour(plateEdgeLit.withAlpha(0.18f));
        g.drawLine(x + 2, y + 23, x + badgeWidth - 13, y + 23, 0.65f);
        auto header = area.withHeight(headerHeight).reduced(grouped ? 8 : 12, 0);
        if (module.enableId != nullptr) header.removeFromLeft(headerHeight);
        // A module with a field in its header stops its title short of it.
        // Without this the two are laid out independently and a long title runs
        // under the control — which is exactly what a spectral oscillator's
        // sample name did when it was drawn in this strip.
        if (seatsRowInHeader(module))
            header = header.withRight(juce::jmax(header.getX(),
                                                 headerCarve(area, module).getX() - 4));
        g.setColour(text.withAlpha(on ? 1.0f : 0.55f));
        g.setFont(panelFont(Face::header, 14.5f));
        g.drawFittedText(module.title, header, juce::Justification::centredLeft, 1, 0.85f);
    }

    if (!grouped && (lower || id == "filter"))
        stamp(g, code, box.withHeight(headerHeight).reduced(14, 0), 12.5f, 0.7f,
              juce::Justification::centredRight);
    if (id == "global")
    {
        const auto body = controlArea(area, module).toFloat();
        const auto y = body.getY() + body.getHeight() * 0.66f;
        g.setColour(plateEdgeLit.withAlpha(0.24f));
        g.drawLine(box.getX() + 12, y, box.getRight() - 12, y, 0.7f);
    }
}

inline void drawGroupPlate(juce::Graphics& g, juce::Rectangle<int> area)
{
    if (isMerli()) { merli::drawGroupPlate(g, area); return; }
    drawHousing(g, area.toFloat());
}

inline void drawWordmark(juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto logo = juce::ImageCache::getFromMemory(BinaryData::forge_logo_png, BinaryData::forge_logo_pngSize);
    juce::Graphics::ScopedSaveState state(g);
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.drawImage(logo, area, juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid);
}

// The maker plate's stamped stripes, under the plate's knee. They start far
// enough right to leave the preset's byline the width of a name and a style.
inline juce::Rectangle<float> identityHatchBounds(juce::Rectangle<int> plate)
{
    const auto box = plate.toFloat();
    const auto knee = box.getX() + box.getWidth() * 0.48f;
    const auto left = box.getX() + 215.0f;
    return {left, box.getY() + 40.0f, juce::jmax(0.0f, knee + 7.0f - left), 23.0f};
}

// Where the preset is named on the maker plate: the name along the top, beside
// the serial bar, up to the cap's diagonal, and two lines under it that stop
// short of the stripes.
inline juce::Rectangle<int> identityTextBounds(juce::Rectangle<int> plate)
{
    const auto shoulder = plate.getX() + plate.getWidth() * 67 / 100;
    return {plate.getX() + 27, plate.getY() + 9, juce::jmax(40, shoulder - 40 - (plate.getX() + 27)), 52};
}

inline void drawIdentityPlate(juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto box = area.toFloat();
    const auto x = box.getX(), y = box.getY(), r = box.getRight(), b = box.getBottom();
    const auto knee = x + box.getWidth() * 0.48f;
    const auto shoulder = x + box.getWidth() * 0.67f;
    // A maker plate below a separate upper-right cap, with a diagonal cut-out
    // for the preset controls. All three pieces meet at the same stepped seam.
    drawMetalPiece(g, metalPolygon({{x + 3, y}, {shoulder - 35, y}, {shoulder, y + 35},
                                    {knee + 34, y + 35}, {knee, b}, {x, b - 1}, {x, y + 3}}));
    drawMetalPiece(g, metalPolygon({{shoulder - 29, y}, {r - 11, y}, {r, y + 11},
                                    {r, y + 34}, {shoulder + 5, y + 34}}));
    drawMetalPiece(g, metalPolygon({{knee + 37, y + 40}, {r, y + 40}, {r, b - 8},
                                    {r - 8, b}, {knee + 3, b}}));
    drawMetalRail(g, {x + box.getWidth() * 0.37f, y - 6, 108, 6});
    drawEdgeWear(g, box, 345);
    drawRivet(g, {x + 7, y + 6}, 2.7f, 0.9f);
    drawRivet(g, {r - 7, y + 28}, 2.7f, 0.9f);

    // The vertical serial marker is aligned with the three lines of lettering,
    // which name the loaded preset and are drawn live over this layer
    // (ui/ForgePresetPlate.h): the plate itself carries no words.
    g.setColour(identityBar);
    g.fillRect(x + 9, y + 14, 7.0f, 45.0f);
    g.setColour(identityBarLit);
    g.fillRect(x + 9, y + 14, 1.0f, 45.0f);
    const auto hatch = identityHatchBounds(area);
    if (hatch.getWidth() > 20)
        drawHatch(g, hatch, identityHatch, 6.0f, 13.0f);
}

inline void drawUnitMark(juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto box = area.toFloat();
    g.setColour(markRed);
    g.setFont(fallbackFont(17.0f, true));
    drawTrackedText(g, unitMark, box.withHeight(19), 1.0f, juce::Justification::centredRight);
    g.setFont(panelFont(Face::label, 10.0f));
    drawTrackedText(g, "UNIT 01", box.translated(0, 18).withHeight(12), 2.0f, juce::Justification::centredRight);
}

inline void drawDeckPlates(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    if (isMerli()) { merli::drawDeckPlates(g, bounds); return; }
    const auto left = deckLeftBounds(bounds).toFloat();
    const auto x = left.getX(), y = left.getY(), r = left.getRight(), b = left.getBottom();
    drawMetalPiece(g, metalPolygon({{x + 5, y - 5}, {r - 16, y - 5}, {r, y + 11},
                                    {r, b + 8}, {x + 14, b + 8}, {x, b - 6}, {x, y}}));
    drawRivet(g, {x + 12, y + 7}, 4.2f, 1.0f);
    drawEdgeWear(g, left, 41);
}

inline void drawNoteGuideHousing(juce::Graphics& g, juce::Rectangle<int> componentBounds)
{
    if (isMerli()) { merli::drawNoteGuideHousing(g, componentBounds); return; }
    const auto plate = noteGuidePlateBounds(componentBounds).toFloat();
    drawMetalPiece(g, housingOutline(plate.withTrimmedTop(-5).withTrimmedBottom(-8), 16));
    drawEdgeWear(g, plate, 63);
    drawRivet(g, {plate.getRight() - 12, plate.getY() + 7}, 3.4f, 0.9f);
    drawRivet(g, {plate.getX() + 9, plate.getBottom() - 9}, 2.6f, 0.8f);
    drawWell(g, plate.reduced(6.0f), plateWellFloor, 1.0f, 5);
}

// The sky behind the notes falling through the ARP plate's well: a faint
// violet haze and a scatter of stars, the far side of the picture the sparks
// fall in front of. Static, so it lives in the cached chassis with the metal;
// the seed is fixed so the same stars are there every time the layer is
// rebuilt rather than a new sky at every resize.
inline void drawArpSky(juce::Graphics& g, juce::Rectangle<int> wellBounds)
{
    if (wellBounds.getWidth() < 8 || wellBounds.getHeight() < 8) return;
    const auto well = wellBounds.toFloat();
    juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(wellBounds);
    juce::ColourGradient haze(signalViolet.withAlpha(0.0f), well.getCentreX(), well.getY(),
                              signalViolet.withAlpha(0.07f), well.getCentreX(), well.getBottom(), false);
    g.setGradientFill(haze);
    g.fillRect(well);
    juce::Random random(0x4a7f);
    const auto count = juce::jmax(6, juce::roundToInt(well.getWidth() * well.getHeight() / 260.0f));
    for (int i = 0; i < count; ++i)
    {
        const auto x = well.getX() + random.nextFloat() * well.getWidth();
        const auto y = well.getY() + random.nextFloat() * well.getHeight();
        const auto size = 0.6f + random.nextFloat() * 1.1f;
        g.setColour(juce::Colours::white.withAlpha(0.05f + random.nextFloat() * 0.16f));
        g.fillEllipse(x, y, size, size);
    }
}

// The ARP plate on the shelf between the keys and the note guide, the width of
// the octave the keyboard gave up for it.
//
// One plate doing three jobs: the lamp switches the arpeggiator on, the four
// buttons choose which of its slots is playing, and the name — or any of the
// plate those do not cover — opens the six panes over the modulators. The well
// at its right-hand end is where the notes the voices play fall.
//
// It is drawn in two halves, for the same reason the chassis and the module
// plates are two layers. The metal, the well, the rule between the door and
// the buttons and the sky in the well never change and go into the cached
// chassis with the rest of the furniture; the lamp, the lit face, the word
// under the name, the buttons and the falling notes follow the arp's state,
// and only those are on the frame path (ui/ForgeArpPlate.h). Drawn whole in
// the live pass it cost about three milliseconds a frame — `drawMetalPiece`
// and `drawEdgeWear` are the expensive primitives here, and they are exactly
// what the cached layer exists to pay for once.
inline void drawArpPlateHousing(juce::Graphics& g, juce::Rectangle<int> componentBounds)
{
    if (isMerli()) { merli::drawArpPlateHousing(g, componentBounds); return; }
    const auto plate = arpPlateBounds(componentBounds).toFloat();
    drawMetalPiece(g, housingOutline(plate, 12.0f));
    drawEdgeWear(g, plate, 137);
    drawRivet(g, {plate.getRight() - 9, plate.getY() + 9}, 3.2f, 0.9f);
    drawRivet(g, {plate.getX() + 8, plate.getBottom() - 9}, 2.4f, 0.8f);
    drawWell(g, arpPlateFace(componentBounds).toFloat(), plateWellFloor, 1.0f,
             arpPlateFaceCut);
    // The rule between the door and the slot buttons, the same engraved line
    // GLOBAL carries under its knobs.
    const auto name = arpPlateNameBounds(componentBounds).toFloat();
    g.setColour(plateEdgeLit.withAlpha(0.16f));
    g.drawLine(name.getRight() + 5.0f, name.getY() + 6.0f, name.getRight() + 5.0f,
               name.getBottom() - 6.0f, 0.8f);
    drawArpSky(g, arpWaterfallBounds(componentBounds));
}

inline void drawBackdrop(juce::Graphics& g, juce::Rectangle<int> componentBounds)
{
    if (isMerli()) { merli::drawBackdrop(g, componentBounds); return; }
    const auto bounds = componentBounds.toFloat();
    const auto w = bounds.getWidth(), h = bounds.getHeight();
    g.fillAll(chassis);
    // Outer shell: a narrow silver lip, black gasket, and a separate inside rail.
    const auto frame = chamferedPath(bounds.reduced(3), 13);
    {
        juce::Graphics::ScopedSaveState state(g);
        // The opaque well below covers this interior completely. Avoid
        // rasterising hidden grain when rebuilding the frame during resizing.
        g.excludeClipRegion(componentBounds.reduced(24));
        drawMetalPiece(g, frame, 1.0f, 0.018f);
    }
    drawWell(g, bounds.reduced(9), chassisWellFloor, 1.0f, 13);
    drawMetalRail(g, {22, 3, w * 0.23f, 5});
    drawMetalRail(g, {w * 0.25f, 3, w * 0.49f, 4});
    drawMetalRail(g, {w * 0.78f, 3, w * 0.20f - 15, 5});
    drawMetalRail(g, {24, h - 8, w * 0.19f, 5}, true);
    drawMetalRail(g, {w * 0.22f, h - 7, w * 0.70f, 4}, true);

    const auto split = static_cast<float>(headerSplit(componentBounds.getWidth()));
    drawMetalPiece(g, metalPolygon({{38, 11}, {split - 22, 11}, {split - 8, 25},
                                    {split - 8, 78}, {split - 20, 90}, {17, 90}, {12, 85}, {12, 35}}));
    drawMetalPiece(g, metalPolygon({{17, 13}, {39, 13}, {29, 28}, {29, 82},
                                    {18, 82}, {13, 76}, {13, 20}}));
    drawEdgeWear(g, {37, 11, split - 50, 79}, 297);
    drawRivet(g, {split - 19, 19}, 2.7f, 0.9f);
    drawRivet(g, {20, 74}, 2.5f, 0.8f);

    const auto tabs = tabBounds(0, componentBounds.getWidth()).getUnion(
        tabBounds(tabCount - 1, componentBounds.getWidth())).toFloat();
    drawWell(g, tabs.expanded(6, 5), tabWellFloor, 1.0f, 6);
    const auto wordmarkWidth = tabs.getX() - 68;
    // The wordmark is the only thing on this plate now that the tagline under
    // it is gone, so it sits on the plate's own centre line rather than in the
    // upper half of a two-line block: the plate runs y 11..90 below, and a
    // 43-tall wordmark centred in it starts at 29.
    drawWordmark(g, {48, 29, wordmarkWidth, 43});
    drawIdentityPlate(g, identityPlateBounds(componentBounds));
    drawUnitMark(g, unitMarkBounds(componentBounds));

    // The segmented rail under the header ties into the outer uprights. The
    // shallow tongue between the oscillators is visible in the reference.
    drawMetalPiece(g, metalPolygon({{19, 95}, {w * 0.40f, 95}, {w * 0.405f, 100},
                                    {w * 0.495f, 100}, {w * 0.50f, 95}, {w - 19, 95},
                                    {w - 25, 100}, {w * 0.51f, 100}, {w * 0.503f, 104},
                                    {w * 0.40f, 104}, {w * 0.395f, 100}, {25, 100}}));
    for (const auto side : {7.0f, w - 7.0f})
    {
        g.setColour(sideRailLit);
        g.drawLine(side, 109, side, h - 120, 1.0f);
        g.setColour(sideRailDark);
        g.drawLine(side + 2, 114, side + 2, h - 124, 1.0f);
        drawRivet(g, {side, 97}, 2.2f, 1.0f);
    }
    drawWell(g, keyboardBounds(componentBounds).toFloat().expanded(4, 3), keyboardWellFloor, 1, 4);
    drawDeckPlates(g, componentBounds);
    drawArpPlateHousing(g, componentBounds);
    drawNoteGuideHousing(g, componentBounds);
    for (const auto point : {juce::Point<float>(16, 16), {w - 16, 16}, {16, h - 15}, {w - 16, h - 15}})
        drawScrew(g, point, 7.0f, 1.0f);
}
}
