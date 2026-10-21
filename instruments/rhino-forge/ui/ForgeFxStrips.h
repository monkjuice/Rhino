#pragma once

#include "ForgeControlBlock.h"

#include <array>

// Where the controls of one FX rack slot land, decided by the effect the slot
// holds.
//
// Every slot declares the same twelve controls (ForgeModules.h, and see
// wiki/pages/forge-fx-slots-have-generic-parameters.md for why), but the
// effects are not the same shape. A distortion's filter mode, FREQ and Q are
// one call into the DSP and want to sit together beside the curve they draw; a
// phaser's comb wants its stage count seated on top of it. So the declaration
// stays the inventory -- ids, styles, attachments, banks -- and this header
// decides the geometry, per type, from the strip the slot is given.
//
// It is pure: a type and a rectangle in, rectangles out, nothing read from the
// processor. The editor, the shelf painter and the layout tests all ask the
// same function, so the three cannot disagree about where anything is.
//
// Every strip shares one frame. On the left, the identity card: the type's
// mark and name, which is also the switch that bypasses it, with the slot's
// automation name under it. On the right, a hatch and then MIX and LEVEL. The
// body between them is the type's own, on a grid of 48 units of the strip's
// width: 5 for the card, 38 for the body, 5 for the tail. Inside the strip
// every knob stands on one grid as well -- its caption on one line, its centre
// on one line and its reading on one line, whatever its size -- so a hero knob
// is larger than its neighbours without its words leaving theirs.
namespace rhino::forge::ui
{
// The twelve controls of a slot, in the order its row declares them. A
// control's role is its index within its bank of the row.
enum class FxRole
{
    type, modeA, modeB, knob1, knob2, knob3, knob4, knob5, knob6, mix, level, bypass
};
inline constexpr int fxRoleCount = 12;

inline FxRole fxRoleOf(int indexInRow) { return static_cast<FxRole>(indexInRow % fxRoleCount); }
inline int fxRoleIndex(FxRole role) { return static_cast<int>(role); }
inline FxRole fxKnobRole(int knob) { return static_cast<FxRole>(fxRoleIndex(FxRole::knob1) + knob); }

// --- Sizes -----------------------------------------------------------------------

// The grid a strip is divided on, and how much of it the frame takes.
inline constexpr int fxGridUnits = 48;
inline constexpr int fxCardUnits = 5;
inline constexpr int fxTailUnits = 5;
inline constexpr int fxBodyUnits = fxGridUnits - fxCardUnits - fxTailUnits;

// The lines every knob in a strip stands on, measured down from the strip's
// top. A caption is centred in its box, so its text sits a few pixels lower
// than the box's top; the knob's centre is halfway between the foot of that
// text and the top of the reading's.
inline constexpr int fxCaptionTop = 6;
inline constexpr int fxCaptionHeight = knobLabelHeight;
inline constexpr int fxKnobCentre = 39;
inline constexpr int fxReadingTop = 59;
inline constexpr int fxReadingHeight = 11;

// The three sizes a knob comes in. A hero is the panel's shared diameter: the
// one control a strip is about, drawn as large as the oscillators' knobs and
// no larger. An ordinary knob is the rack's compact share of it, as every
// rack knob was before. A small one sits on a line beside its caption.
inline constexpr int fxHeroKnobMax = 48;
inline constexpr int fxKnobMax = 40;
inline constexpr int fxMiniKnob = 26;

struct FxKnobSizes
{
    int shared = 46;
    int hero = 46;
    int normal = 36;
    int mini = fxMiniKnob;
};

inline FxKnobSizes fxKnobSizesFor(juce::Rectangle<int> panelBounds)
{
    FxKnobSizes sizes;
    sizes.shared = uniformKnobDiameter(panelBounds);
    sizes.hero = juce::jmin(sizes.shared, fxHeroKnobMax);
    sizes.normal = juce::jmin(sizes.shared * compactKnobPercent / 100, fxKnobMax);
    sizes.mini = fxMiniKnob;
    return sizes;
}

// --- What a strip holds ------------------------------------------------------------

enum class FxKnobSize { none, normal, hero, mini };

// How a mode field is drawn. A slot's mode is one float whose choices the type
// names, so the field is the same parameter whatever it looks like; what it
// looks like is the type's call. See FxSelector.
enum class FxPresentation
{
    none,
    stack,          // two or three choices stacked, the live one lit
    list,           // one name between two arrows; the middle opens the list
    chip,           // one latching button, lit on its second choice
    captionSwitch,  // a knob's caption that is its mode: clicking it steps the choice
    glyphs,         // two or three choices stacked as pictures rather than words
    segmented,      // the choices side by side, seated along a well
    route           // the distortion's filter: OFF / PRE / POST beside LP / HP
};

// Where one control goes. `component` is its component's rectangle;
// `caption` its label's, or empty when it shows none; `reading` where its
// value is printed at rest, or empty. All in the strip's own coordinates.
struct FxSpot
{
    juce::Rectangle<int> component, caption, reading;
    FxKnobSize knob = FxKnobSize::none;
    FxPresentation presentation = FxPresentation::none;
    // A small knob's caption sits to its left rather than above it.
    bool captionLeft = false;
    // The well this control is seated in, or -1. A seated control is the one
    // thing allowed to stand on a picture.
    int well = -1;
    bool placed = false;
};

// What a well draws. Each type's pictures are named for what they are; the
// generic well draws whatever drawFxDisplay draws for the type.
enum class FxWellKind
{
    generic
};

struct FxWell
{
    juce::Rectangle<int> area;
    // The picture itself: the area less any strip a field is seated in.
    juce::Rectangle<int> plot;
    FxWellKind kind = FxWellKind::generic;
};

// A group of controls the DSP couples, framed, with its caption engraved up
// one edge so it costs the controls no height.
struct FxPanel
{
    juce::Rectangle<int> frame, lane;
    const char* caption = "";
    bool lamp = false;
};

inline constexpr int maxFxWells = 2;
inline constexpr int maxFxPanels = 4;

struct FxStrip
{
    juce::Rectangle<int> strip;
    // The frame: the identity card, the slot's automation name under it, the
    // body between card and tail, and the hatch the tail starts with.
    juce::Rectangle<int> card, automation, body, hatch;
    std::array<FxSpot, fxRoleCount> spots {};
    std::array<FxWell, maxFxWells> wells {};
    int wellCount = 0;
    std::array<FxPanel, maxFxPanels> panels {};
    int panelCount = 0;

    const FxSpot& spot(FxRole role) const { return spots[static_cast<size_t>(fxRoleIndex(role))]; }
    FxSpot& spot(FxRole role) { return spots[static_cast<size_t>(fxRoleIndex(role))]; }
};

// --- Building one -------------------------------------------------------------------

// The strip's grid: a position in units to a pixel column. Rounded per edge
// rather than per width, so neighbouring spans share their edges exactly.
struct FxGrid
{
    juce::Rectangle<int> strip;

    int x(float units) const
    {
        return strip.getX() + juce::roundToInt(units * static_cast<float>(strip.getWidth())
                                               / static_cast<float>(fxGridUnits));
    }

    juce::Rectangle<int> span(float from, float units) const
    {
        return juce::Rectangle<int>::leftTopRightBottom(x(from), strip.getY(), x(from + units), strip.getBottom());
    }
};

inline int fxDiameter(FxKnobSize size, const FxKnobSizes& sizes)
{
    switch (size)
    {
        case FxKnobSize::hero: return sizes.hero;
        case FxKnobSize::mini: return sizes.mini;
        case FxKnobSize::normal: return sizes.normal;
        case FxKnobSize::none: break;
    }
    return 0;
}

// A knob in a column: its caption on the caption line, its centre on the knob
// line and its reading on the reading line.
inline void placeFxKnob(FxStrip& strip, FxRole role, juce::Rectangle<int> column, FxKnobSize size,
                        const FxKnobSizes& sizes, bool caption = true, bool reading = true)
{
    auto& spot = strip.spot(role);
    const auto diameter = juce::jmax(24, juce::jmin(fxDiameter(size, sizes), column.getWidth() - 6));
    spot.component = juce::Rectangle<int>(diameter, diameter)
                         .withCentre({column.getCentreX(), column.getY() + fxKnobCentre});
    spot.caption = caption ? juce::Rectangle<int>(column.getX(), column.getY() + fxCaptionTop,
                                                  column.getWidth(), fxCaptionHeight)
                           : juce::Rectangle<int>();
    spot.reading = reading ? juce::Rectangle<int>(column.getX(), column.getY() + fxReadingTop,
                                                  column.getWidth(), fxReadingHeight)
                           : juce::Rectangle<int>();
    spot.knob = size;
    spot.placed = true;
}

// A small knob on one line, centred at `centreY`, between its caption on the
// left and its reading on the right. Two of these stack in a strip, which is
// how a filter's FREQ and Q sit beside the curve they move.
inline constexpr int fxMiniCaptionWidth = 36;

inline void placeFxMiniRow(FxStrip& strip, FxRole role, juce::Rectangle<int> column, int centreY,
                           const FxKnobSizes& sizes, int captionWidth = fxMiniCaptionWidth)
{
    auto& spot = strip.spot(role);
    const auto diameter = sizes.mini;
    const auto left = column.getX();
    spot.caption = {left, centreY - fxCaptionHeight / 2, captionWidth, fxCaptionHeight};
    spot.component = juce::Rectangle<int>(diameter, diameter)
                         .withCentre({left + captionWidth + diameter / 2, centreY});
    const auto readingLeft = spot.component.getRight() + 2;
    spot.reading = {readingLeft, centreY - fxReadingHeight / 2, juce::jmax(0, column.getRight() - readingLeft),
                    fxReadingHeight};
    spot.knob = FxKnobSize::mini;
    spot.captionLeft = true;
    spot.placed = true;
}

// How many choices a field shows at once before it becomes a list. The same
// number FxSelector::inlineLimit draws by.
inline constexpr int fxInlineChoiceLimit = 3;

// A mode field, stacked, with its caption over it: the generic body's way.
inline void placeFxModeStack(FxStrip& strip, FxRole role, juce::Rectangle<int> column, int choices)
{
    auto& spot = strip.spot(role);
    const auto width = juce::jmin(column.getWidth() - 10, maxSelectorWidth);
    auto block = juce::Rectangle<int>(width, stepperLabelHeight + selectorHeight)
                     .withCentre(column.getCentre());
    spot.caption = block.removeFromTop(stepperLabelHeight);
    spot.component = block;
    spot.presentation = choices > fxInlineChoiceLimit ? FxPresentation::list : FxPresentation::stack;
    spot.placed = true;
}

// A latching button standing on the knob line, against the knob it changes.
inline void placeFxChip(FxStrip& strip, FxRole role, juce::Rectangle<int> column, int width = 44)
{
    auto& spot = strip.spot(role);
    spot.component = juce::Rectangle<int>(juce::jmin(width, column.getWidth() - 4), chipHeight - 2)
                         .withCentre({column.getCentreX(), column.getY() + fxKnobCentre});
    spot.presentation = FxPresentation::chip;
    spot.placed = true;
}

// A picture across a column, inset from the strip's edges the way the rack's
// displays always were.
inline int addFxWell(FxStrip& strip, juce::Rectangle<int> column, FxWellKind kind)
{
    if (strip.wellCount >= maxFxWells) return -1;
    auto& well = strip.wells[static_cast<size_t>(strip.wellCount)];
    well.area = column.withTrimmedTop(6).withTrimmedBottom(6).reduced(3, 0);
    well.plot = well.area;
    well.kind = kind;
    return strip.wellCount++;
}

// The frame: card, automation name, hatch, MIX and LEVEL. Returns the body.
inline juce::Rectangle<int> placeFxFrame(FxStrip& strip, const FxKnobSizes& sizes)
{
    const FxGrid grid {strip.strip};
    const auto cardColumn = grid.span(0.0f, static_cast<float>(fxCardUnits));
    strip.card = juce::Rectangle<int>::leftTopRightBottom(cardColumn.getX(), cardColumn.getY() + 4,
                                                          cardColumn.getRight() - 6, cardColumn.getY() + 58);
    strip.automation = juce::Rectangle<int>::leftTopRightBottom(strip.card.getX(), strip.card.getBottom() + 2,
                                                                strip.card.getRight(), strip.card.getBottom() + 14);
    auto& type = strip.spot(FxRole::type);
    type.component = strip.card;
    type.placed = true;
    // The bypass is the card: its own component is never shown, and stands
    // where the card draws its power light so anything that does ask for it
    // finds it there.
    auto& bypass = strip.spot(FxRole::bypass);
    bypass.component = juce::Rectangle<int>(18, 18).withPosition(strip.card.getRight() - 20, strip.card.getY() + 2);
    bypass.placed = true;

    const auto tail = grid.span(static_cast<float>(fxCardUnits + fxBodyUnits), static_cast<float>(fxTailUnits));
    strip.hatch = tail.withWidth(7).withTrimmedTop(4).withTrimmedBottom(4);
    const auto knobs = tail.withTrimmedLeft(7);
    const auto half = knobs.getWidth() / 2;
    placeFxKnob(strip, FxRole::mix, knobs.withWidth(half), FxKnobSize::normal, sizes);
    placeFxKnob(strip, FxRole::level, knobs.withTrimmedLeft(half), FxKnobSize::normal, sizes);

    strip.body = grid.span(static_cast<float>(fxCardUnits), static_cast<float>(fxBodyUnits));
    return strip.body;
}

// --- The generic body ----------------------------------------------------------------
//
// What a type gets until it has a strip of its own: its mode fields, then its
// display, then its knobs sharing what is left. It is also the honest fallback
// for a type added later.
inline FxStrip fxGenericStrip(int type, juce::Rectangle<int> stripBounds, const FxKnobSizes& sizes)
{
    FxStrip strip;
    strip.strip = stripBounds;
    placeFxFrame(strip, sizes);
    const auto& info = fxTypeInfo(static_cast<float>(type));
    const FxGrid grid {stripBounds};
    auto at = static_cast<float>(fxCardUnits);
    for (const auto role : {FxRole::modeA, FxRole::modeB})
    {
        const auto& mode = role == FxRole::modeA ? info.modeA : info.modeB;
        if (mode.count <= 0) continue;
        placeFxModeStack(strip, role, grid.span(at, 4.0f), mode.count);
        at += 4.0f;
    }
    addFxWell(strip, grid.span(at, 9.0f), FxWellKind::generic);
    at += 9.0f;
    auto knobs = 0;
    for (const auto* label : info.knobs) if (label != nullptr) ++knobs;
    const auto end = static_cast<float>(fxCardUnits + fxBodyUnits);
    const auto each = knobs > 0 ? (end - at) / static_cast<float>(knobs) : 0.0f;
    for (int knob = 0; knob < fxKnobCount; ++knob)
    {
        if (info.knobs[static_cast<size_t>(knob)] == nullptr) continue;
        placeFxKnob(strip, fxKnobRole(knob), grid.span(at, each), FxKnobSize::normal, sizes);
        at += each;
    }
    return strip;
}

// One slot's strip, for the effect type it holds. OFF is a slot with nothing in
// it and is never shown; it gets the frame so that nothing asking for it gets
// nothing.
inline FxStrip fxStripLayout(int type, juce::Rectangle<int> strip, const FxKnobSizes& sizes)
{
    return fxGenericStrip(type, strip, sizes);
}
}
