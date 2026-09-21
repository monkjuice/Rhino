#pragma once

#include "ForgeChrome.h"
#include "ForgePlacement.h"
#include "ForgeType.h"

// Static chassis furniture. Editor caches this layer at the display's pixel
// scale; the irregular plates, engraving and wear are never rebuilt per frame.
namespace rhino::forge::ui
{
inline const juce::String unitMark = juce::String::fromUTF8("\xe5\x88\x9d\xe5\x8f\xb7\xe6\xa9\x9f");
inline const auto markRed = juce::Colour(0xffdf503b);

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

inline void drawLegendRecess(juce::Graphics& g, juce::Rectangle<int> area,
                             const juce::String& name, const juce::String& code)
{
    auto footer = plateFooterBounds(area).toFloat();
    const auto x = footer.getX(), y = footer.getY(), r = footer.getRight(), b = footer.getBottom() - 3;
    const auto shape = metalPolygon({{x - 4, y}, {r - 12, y}, {r - 16, b}, {x + 1, b}, {x - 4, b - 5}});
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff090c0f), x, y,
                                          juce::Colour(0xff121619), x, b, false));
    g.fillPath(shape);
    g.setColour(juce::Colour(0xff363c45));
    g.drawLine(x - 3, y, r - 13, y, 0.7f);
    footer.removeFromRight(21);
    stamp(g, name, footer, 12.5f, 0.9f);
    stamp(g, code, footer, 12.5f, 0.5f, juce::Justification::centredRight);
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
    const auto box = area.toFloat();
    const auto id = juce::String(module.id);
    const auto grouped = module.group != nullptr;
    const auto lower = module.row > 0;
    if (grouped)
    {
        const auto inner = box.withTrimmedBottom(plateFooterHeight).reduced(4.0f, 3.0f);
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
        const auto badgeWidth = juce::jmin(box.getWidth() - 14, module.enableId ? 124.0f : 100.0f);
        const auto x = box.getX() + 6, y = box.getY() + 7;
        const auto badge = metalPolygon({{x + 3, y}, {x + badgeWidth, y},
                                         {x + badgeWidth - 13, y + 23}, {x, y + 23}, {x, y + 3}});
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff22262c), x, y,
                                              juce::Colour(0xff111417), x, y + 23, false));
        g.fillPath(badge);
        g.setColour(plateEdgeLit.withAlpha(0.18f));
        g.drawLine(x + 2, y + 23, x + badgeWidth - 13, y + 23, 0.65f);
        auto header = area.withHeight(headerHeight).reduced(grouped ? 8 : 12, 0);
        if (module.enableId != nullptr) header.removeFromLeft(headerHeight);
        g.setColour(text.withAlpha(on ? 1.0f : 0.55f));
        g.setFont(panelFont(Face::header, 14.5f));
        g.drawFittedText(module.title, header, juce::Justification::centredLeft, 1, 0.85f);
    }

    if (!grouped)
    {
        if (module.plateName != nullptr)
            drawLegendRecess(g, area, module.plateName, code);
        else if (lower || id == "filter")
        {
            stamp(g, code, box.withHeight(headerHeight).reduced(14, 0), 12.5f, 0.7f,
                  juce::Justification::centredRight);
            if (id == "filter")
            {
                const auto foot = plateFooterBounds(area).toFloat().reduced(2, 5);
                drawMetalRail(g, foot.withTrimmedLeft(22).withTrimmedRight(22));
                drawHatch(g, foot.withWidth(20), legendText.withAlpha(0.4f), 0.9f, 3.0f);
            }
        }
    }
    if (id == "global")
    {
        const auto body = controlArea(area, module).toFloat();
        const auto y = body.getY() + body.getHeight() * 0.66f;
        g.setColour(plateEdgeLit.withAlpha(0.24f));
        g.drawLine(box.getX() + 12, y, box.getRight() - 12, y, 0.7f);
    }
}

inline void drawGroupPlate(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& legend,
                           const juce::String& code)
{
    drawHousing(g, area.toFloat());
    drawLegendRecess(g, area, legend, code);
}

inline void drawWordmark(juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto logo = juce::ImageCache::getFromMemory(BinaryData::forge_logo_png, BinaryData::forge_logo_pngSize);
    juce::Graphics::ScopedSaveState state(g);
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.drawImage(logo, area, juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid);
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

    // The vertical serial marker is aligned with the three lines of lettering.
    g.setColour(juce::Colour(0xff444b5a));
    g.fillRect(x + 9, y + 14, 7.0f, 45.0f);
    g.setColour(juce::Colour(0xff717784));
    g.fillRect(x + 9, y + 14, 1.0f, 45.0f);
    const auto face = juce::Rectangle<float>(x + 29, y + 12, 169, 19);
    g.setFont(panelFont(Face::reading, 15.0f));
    g.setColour(juce::Colour(0xff9ba5bc));
    drawTrackedText(g, "RHINO FORGE", face, 2.2f, juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xff68758a));
    g.drawLine(face.getX(), face.getBottom() + 1, face.getX() + 130, face.getBottom() + 1, 0.7f);
    stamp(g, "SYNTHESIZER UNIT 01", face.translated(0, 21).withHeight(13), 11, 0.75f);
    stamp(g, "TOKYO-3 AUDIO RESEARCH", face.translated(0, 34).withHeight(13), 10.5f, 0.65f);
    const auto hatchX = x + 185;
    const auto hatchRight = knee + 7;
    if (hatchRight - hatchX > 20)
        drawHatch(g, {hatchX, y + 40, hatchRight - hatchX, 23}, juce::Colour(0xff545d70), 6.0f, 13.0f);
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
    const auto left = deckLeftBounds(bounds).toFloat();
    const auto right = deckRightBounds(bounds).toFloat();
    const auto x = left.getX(), y = left.getY(), r = left.getRight(), b = left.getBottom();
    drawMetalPiece(g, metalPolygon({{x + 5, y - 5}, {r - 16, y - 5}, {r, y + 11},
                                    {r, b + 8}, {x + 14, b + 8}, {x, b - 6}, {x, y}}));
    drawMetalPiece(g, housingOutline(right.withTrimmedTop(-5).withTrimmedBottom(-8), 26));
    drawRivet(g, {x + 12, y + 7}, 4.2f, 1.0f);
    drawRivet(g, {right.getRight() - 12, y + 7}, 4.2f, 1.0f);
    drawEdgeWear(g, left, 41);
    drawEdgeWear(g, right, 63);
    const auto face = left.reduced(13, 0).translated(0, 23);
    g.setColour(legendText);
    g.setFont(panelFont(Face::header, 14));
    drawTrackedText(g, "NERV", face.withHeight(16), 0.8f, juce::Justification::centredLeft);
    stamp(g, "SYNTH INTERFACE", face.translated(0, 19).withHeight(10), 8, 0.7f);
    stamp(g, "TOKYO-3  //  TC003", face.translated(0, 29).withHeight(10), 7, 0.6f);
    drawHatch(g, {x + 16, b - 10, left.getWidth() - 30, 13}, legendText.withAlpha(0.7f), 4, 9);
    const auto motto = right.reduced(12, 0).translated(0, 34);
    stamp(g, "FOR A MORE", motto.withHeight(12), 9, 0.7f);
    stamp(g, "HUMAN TOMORROW", motto.translated(0, 13).withHeight(12), 8, 0.6f);
    const auto at = juce::Point<float>(right.getRight() - 21, right.getY() + 34);
    g.setColour(markRed);
    g.drawLine(at.x - 5, at.y, at.x + 5, at.y, 1.2f);
    g.drawLine(at.x, at.y - 5, at.x, at.y + 5, 1.2f);
}

// The ARP switch on the shelf beside the keys, standing in the octave the
// keyboard gave up for it.
//
// One plate doing two jobs, exactly as Serum's is: the circle switches the
// arpeggiator on, and the rest of it opens the six panes over the modulators.
// The two are drawn as different things — a lit lamp against a raised face —
// because an arp running with its settings put away and an arp on screen that
// is switched off are both ordinary states, and a single highlight covering
// both would leave you unable to tell which you were in.
//
// It is drawn in two halves, for the same reason the chassis and the module
// plates are two layers. The metal never changes and goes into the cached
// chassis with the rest of the furniture; only the lamp, the lit face and the
// word under the name follow the arp's state, and only those are on the frame
// path. Drawn whole in the live pass it cost about three milliseconds a frame
// — `drawMetalPiece` and `drawEdgeWear` are the expensive primitives here, and
// they are exactly what the cached layer exists to pay for once.
inline void drawArpPlateHousing(juce::Graphics& g, juce::Rectangle<int> componentBounds)
{
    const auto plate = arpPlateBounds(componentBounds).toFloat();
    drawMetalPiece(g, housingOutline(plate, 12.0f));
    drawEdgeWear(g, plate, 137);
    drawRivet(g, {plate.getRight() - 9, plate.getY() + 9}, 3.2f, 0.9f);
    drawRivet(g, {plate.getX() + 8, plate.getBottom() - 9}, 2.4f, 0.8f);
    drawWell(g, plate.reduced(7.0f, 9.0f), juce::Colour(0xff05070c), 1.0f, 5);
}

inline void drawArpPlateState(juce::Graphics& g, juce::Rectangle<int> componentBounds,
                              bool on, bool open)
{
    const auto face = arpPlateBounds(componentBounds).toFloat().reduced(7.0f, 9.0f);
    // Lit while the panes are showing, which is what makes the plate read as
    // the door it is rather than as a second power switch.
    if (open)
    {
        g.setColour(signalViolet.withAlpha(0.16f));
        g.fillRoundedRectangle(face, 5.0f);
        g.setColour(signalViolet.withAlpha(0.75f));
        g.drawRoundedRectangle(face.reduced(0.5f), 5.0f, 1.0f);
    }

    // The same lamp the module headers wear, drawn here rather than borrowed,
    // because this one is on the chassis instead of on a module and has no
    // component of its own to carry it.
    const auto led = arpPlateLedBounds(componentBounds).toFloat().reduced(5.0f);
    if (on)
    {
        g.setColour(signalViolet.withAlpha(0.22f));
        g.fillEllipse(led.expanded(3.5f));
    }
    g.setColour(on ? signalViolet : juce::Colour(0xff232838));
    g.fillEllipse(led);
    g.setColour(on ? juce::Colours::white.withAlpha(0.5f) : mutedText.withAlpha(0.45f));
    g.drawEllipse(led, 1.0f);

    auto name = face.withTrimmedLeft(arpPlateLedBounds(componentBounds).toFloat().getRight()
                                     - face.getX() + 6.0f);
    g.setFont(panelFont(Face::header, 14.0f));
    g.setColour(on ? text : mutedText);
    drawTrackedText(g, "ARP", name.removeFromTop(name.getHeight() * 0.62f), 1.8f,
                    juce::Justification::centredLeft);
    stamp(g, open ? "CLOSE" : "SETTINGS", name, 8.0f, 0.7f);
}

inline void drawBackdrop(juce::Graphics& g, juce::Rectangle<int> componentBounds)
{
    const auto bounds = componentBounds.toFloat();
    const auto w = bounds.getWidth(), h = bounds.getHeight();
    g.fillAll(chassis);
    // Outer shell: a narrow silver lip, black gasket, and a separate inside rail.
    const auto frame = chamferedPath(bounds.reduced(3), 13);
    drawMetalPiece(g, frame, 1.0f, 0.018f);
    drawWell(g, bounds.reduced(9), juce::Colour(0xff050709), 1.0f, 13);
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
    drawWell(g, tabs.expanded(6, 5), juce::Colour(0xff030609), 1.0f, 6);
    const auto wordmarkWidth = tabs.getX() - 68;
    drawWordmark(g, {48, 21, wordmarkWidth, 43});
    // Not stamped: the line under the wordmark is the product saying its own
    // name, and the reference draws it as brightly as anything on the panel.
    // stamp() is for a part number engraved into a plate, which is the opposite
    // kind of lettering and the opposite colour.
    g.setFont(panelFont(Face::label, 13.5f));
    g.setColour(labelText);
    drawTrackedText(g, "SYNTHETIC SIGNAL FORGE // UNIT 01", {49, 67, wordmarkWidth + 3, 14}, 0.9f,
                    juce::Justification::centredLeft);
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
        g.setColour(juce::Colour(0xff394047));
        g.drawLine(side, 109, side, h - 120, 1.0f);
        g.setColour(juce::Colour(0xff13171d));
        g.drawLine(side + 2, 114, side + 2, h - 124, 1.0f);
        drawRivet(g, {side, 97}, 2.2f, 1.0f);
    }
    drawWell(g, keyboardBounds(componentBounds).toFloat().expanded(4, 3), juce::Colour(0xff010203), 1, 4);
    drawDeckPlates(g, componentBounds);
    drawArpPlateHousing(g, componentBounds);
    for (const auto point : {juce::Point<float>(16, 16), {w - 16, 16}, {16, h - 15}, {w - 16, h - 15}})
        drawScrew(g, point, 7.0f, 1.0f);
}
}
