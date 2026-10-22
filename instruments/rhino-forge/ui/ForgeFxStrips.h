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
// top, for a strip fxSlotHeight tall. A caption is centred in its box, so its
// text sits a few pixels lower than the box's top; the knob's centre is
// halfway between the foot of that text and the top of the reading's, and the
// largest knob clears both.
inline constexpr int fxCaptionTop = 6;
inline constexpr int fxCaptionHeight = knobLabelHeight;
inline constexpr int fxKnobCentre = 46;
inline constexpr int fxReadingTop = 72;
inline constexpr int fxReadingHeight = 14;
// The two lines a pair of small knobs stands on, one above the other.
inline constexpr int fxMiniRowTop = 28;
inline constexpr int fxMiniRowBottom = 62;
// The identity card, from the strip's top; the automation name stands under it.
inline constexpr int fxCardTop = 5;
inline constexpr int fxCardBottom = 71;
// A field of two choices standing on the knob line, and of three.
inline constexpr int fxStackHeight = 58;
inline constexpr int fxGlyphStackHeight = 66;

// The three sizes a knob comes in. A hero is the panel's shared diameter: the
// one control a strip is about, drawn as large as the oscillators' knobs and
// no larger. An ordinary knob is the rack's compact share of it, as every
// rack knob was before. A small one sits on a line beside its caption.
inline constexpr int fxHeroKnobMax = 50;
inline constexpr int fxKnobMax = 42;
inline constexpr int fxMiniKnob = 28;

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
    // What a knob says about its own travel beyond the pointer: a detent at
    // twelve o'clock for a control that is bipolar, a zone in red where it
    // starts to scream, marks engraved at positions that mean something, and
    // a second LED for the other channel of a stereo pair.
    bool bipolar = false;
    float redZone = 0.0f;
    std::array<float, 4> marks {};
    int markCount = 0;
    bool secondPointer = false;
    // The well this control is seated in, or -1. A seated control is the one
    // thing allowed to stand on a picture.
    int well = -1;
    bool placed = false;
};

// What a well draws. Each type's pictures are named for what they are; the
// generic well draws whatever drawFxDisplay draws for the type.
enum class FxWellKind
{
    generic,
    distortionTone,   // the filter's response, with the node that is FREQ and Q
    distortionShape,  // the transfer curve DRIVE bends, under the SHAPE field
    filterResponse,   // the filter's response at the cutoff the voice hears
    delayTape,        // every repeat: when, on which side, how loud and how dark
    delayLoop,        // the filter the repeats pass through on every trip
    reverbTail,       // the decay on a fixed time axis, after the pre-delay
    reverbDecay,      // how long each frequency rings, and the band the cuts keep
    eqResponse,       // the two bands and their sum
    compressorCurve,  // what comes out for what goes in
    compressorMeter,  // the gain being taken off, on a needle
    chorusTaps,       // each tap's delay across a few seconds of its sweep
    phaserComb,       // the notches, where the sweep has them now
    flangerComb       // the comb, where the sweep has the tap now
};

// What lights a panel's lamp: nothing, one of the slot's knobs, or something
// only the running effect knows -- its LFO, its level -- which the engine
// publishes once a block.
enum class FxLamp
{
    none,
    knob,
    rate,       // pulses with the LFO the slot sweeps by
    clip,       // lights as the shaper is driven onto its rails
    repeats,    // flashes with what is coming back round the loop
    lowBand,    // an equaliser band doing something
    highBand
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
    bool legendRight = false;
    // What lights the panel's lamp; for FxLamp::knob, which knob.
    FxLamp lamp = FxLamp::none;
    int lampKnob = -1;
    // A mode that switches the whole panel out, and the choice that does: the
    // distortion's filter at OFF dims its TONE panel, curve and knobs together.
    FxRole gate = FxRole::type;
    int gateOff = -1;
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
    // Which way the signal runs between two panels, where a type has one to
    // say: the distortion's filter before or after its shaper.
    juce::Rectangle<int> chevron;

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
    spot.component = juce::Rectangle<int>(juce::jmin(width, column.getWidth() - 4), chipHeight)
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

// A framed group across a column. Its caption is engraved up one edge, in a
// lane of its own, so the group's controls keep the strip's whole height.
inline constexpr int fxPanelLaneWidth = 15;

inline FxPanel& addFxPanel(FxStrip& strip, juce::Rectangle<int> column, const char* caption,
                           bool legendRight = false)
{
    auto& group = strip.panels[static_cast<size_t>(juce::jmin(strip.panelCount, maxFxPanels - 1))];
    strip.panelCount = juce::jmin(strip.panelCount + 1, maxFxPanels);
    group.frame = column.reduced(2, 0).withTrimmedTop(3).withTrimmedBottom(3);
    group.lane = legendRight ? group.frame.withLeft(group.frame.getRight() - fxPanelLaneWidth)
                             : group.frame.withWidth(fxPanelLaneWidth);
    group.caption = caption;
    group.legendRight = legendRight;
    return group;
}

// What a group leaves for its controls: the column less its frame and lane,
// at the strip's full height, so knobs in it stand on the strip's own lines.
inline juce::Rectangle<int> fxPanelContent(const FxStrip& strip, const FxPanel& group)
{
    const auto left = group.legendRight ? group.frame.getX() + 2 : group.lane.getRight() + 2;
    const auto right = group.legendRight ? group.lane.getX() - 2 : group.frame.getRight() - 2;
    return juce::Rectangle<int>::leftTopRightBottom(left, strip.strip.getY(), right, strip.strip.getBottom());
}

// A mode field seated along the top of a well: the picture loses the strip it
// stands in, and the field is the one control allowed on the picture.
inline constexpr int fxSeatHeight = 19;

inline void seatFxField(FxStrip& strip, int well, FxRole role, FxPresentation presentation,
                        int width = 0)
{
    if (well < 0 || well >= strip.wellCount) return;
    auto& picture = strip.wells[static_cast<size_t>(well)];
    auto& spot = strip.spot(role);
    const auto seat = picture.area.reduced(3, 0).withTrimmedTop(3).withHeight(fxSeatHeight);
    spot.component = width > 0 ? seat.withWidth(juce::jmin(width, seat.getWidth())) : seat;
    spot.presentation = presentation;
    spot.well = well;
    spot.placed = true;
    picture.plot = picture.area.withTrimmedTop(fxSeatHeight + 6);
}

// The frame: card, automation name, hatch, MIX and LEVEL. Returns the body.
inline juce::Rectangle<int> placeFxFrame(FxStrip& strip, const FxKnobSizes& sizes)
{
    const FxGrid grid {strip.strip};
    const auto cardColumn = grid.span(0.0f, static_cast<float>(fxCardUnits));
    strip.card = juce::Rectangle<int>::leftTopRightBottom(cardColumn.getX(), cardColumn.getY() + fxCardTop,
                                                          cardColumn.getRight() - 6, cardColumn.getY() + fxCardBottom);
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

// A column of the strip between two x positions, at the strip's full height.
inline juce::Rectangle<int> fxColumn(const FxStrip& strip, int left, int right)
{
    return juce::Rectangle<int>::leftTopRightBottom(left, strip.strip.getY(), right, strip.strip.getBottom());
}

// A field of two or three choices standing on the knob line with no caption:
// the choices name themselves (PLATE, HALL; PEAK, RMS).
inline void placeFxStack(FxStrip& strip, FxRole role, juce::Rectangle<int> column, int height,
                         FxPresentation presentation = FxPresentation::stack)
{
    auto& spot = strip.spot(role);
    spot.component = juce::Rectangle<int>(column.getWidth() - 4, height)
                         .withCentre({column.getCentreX(), column.getY() + fxKnobCentre});
    spot.presentation = presentation;
    spot.placed = true;
}

// A mode that is its knob's caption: the pill stands where the caption would,
// and the knob shows no other.
inline void placeFxCaptionSwitch(FxStrip& strip, FxRole role, FxRole knob)
{
    auto& owner = strip.spot(knob);
    auto& spot = strip.spot(role);
    spot.component = owner.caption.withSizeKeepingCentre(juce::jmin(46, owner.caption.getWidth() - 4),
                                                         fxCaptionHeight);
    // The caption row and a full-size knob share a pixel, which a word can
    // afford and a button cannot: it would take the knob's clicks there.
    if (owner.component.getY() < spot.component.getBottom()) spot.component.setBottom(owner.component.getY());
    spot.presentation = FxPresentation::captionSwitch;
    spot.placed = true;
    owner.caption = {};
}

// Where a scale's marks go on a knob whose law is a logarithm or a power.
inline float fxLogMark(float value, float low, float high) { return std::log(value / low) / std::log(high / low); }
inline float fxPowerMark(float value, float low, float high, float skew)
{
    return std::pow(juce::jlimit(0.0f, 1.0f, (value - low) / (high - low)), 1.0f / skew);
}

inline void setFxMarks(FxSpot& spot, std::initializer_list<float> marks)
{
    spot.markCount = 0;
    for (const auto mark : marks)
        if (spot.markCount < static_cast<int>(spot.marks.size()))
            spot.marks[static_cast<size_t>(spot.markCount++)] = mark;
}

// --- DIST: the amp head -------------------------------------------------------------
//
// Two blocks, in the order the signal can meet them. TONE is the filter: its
// placement and tap (one five-way field, OFF / PRE / POST beside LP / HP), its
// response with FREQ and Q beside it -- one call into the DSP, framed as one
// thing, and dimmed as one when the filter is switched out. SHAPE is the
// shaper: DRIVE as the hero, and the transfer curve it bends with the shape
// seated on top of it. A chevron between them says which way the signal runs.
inline FxStrip fxDistortionStrip(juce::Rectangle<int> stripBounds, const FxKnobSizes& sizes)
{
    FxStrip strip;
    strip.strip = stripBounds;
    placeFxFrame(strip, sizes);
    const FxGrid grid {stripBounds};

    auto& tone = addFxPanel(strip, grid.span(5.0f, 20.0f), "TONE");
    tone.gate = FxRole::modeB;
    tone.gateOff = 0;
    const auto toneContent = fxPanelContent(strip, tone);
    auto& route = strip.spot(FxRole::modeB);
    route.component = juce::Rectangle<int>::leftTopRightBottom(toneContent.getX(), stripBounds.getY() + 9,
                                                               grid.x(9.0f) - 3, stripBounds.getBottom() - 9);
    route.presentation = FxPresentation::route;
    route.placed = true;
    addFxWell(strip, juce::Rectangle<int>::leftTopRightBottom(grid.x(9.0f), stripBounds.getY(), grid.x(19.0f),
                                                              stripBounds.getBottom()),
              FxWellKind::distortionTone);
    const auto rows = juce::Rectangle<int>::leftTopRightBottom(grid.x(19.0f) + 2, stripBounds.getY(),
                                                               toneContent.getRight(), stripBounds.getBottom());
    placeFxMiniRow(strip, FxRole::knob2, rows, stripBounds.getY() + fxMiniRowTop, sizes, 32);
    placeFxMiniRow(strip, FxRole::knob3, rows, stripBounds.getY() + fxMiniRowBottom, sizes, 32);

    strip.chevron = juce::Rectangle<int>::leftTopRightBottom(grid.x(25.0f), stripBounds.getY() + 26,
                                                             grid.x(26.0f), stripBounds.getBottom() - 20);

    auto& shape = addFxPanel(strip, grid.span(26.0f, 17.0f), "SHAPE");
    shape.lamp = FxLamp::clip;
    const auto shapeContent = fxPanelContent(strip, shape);
    const auto drive = shapeContent.withRight(grid.x(30.0f));
    placeFxKnob(strip, FxRole::knob1, drive, FxKnobSize::hero, sizes);
    const auto well = addFxWell(strip, juce::Rectangle<int>::leftTopRightBottom(drive.getRight(), stripBounds.getY(),
                                                                                shapeContent.getRight(),
                                                                                stripBounds.getBottom()),
                                FxWellKind::distortionShape);
    seatFxField(strip, well, FxRole::modeA, FxPresentation::list);
    return strip;
}

// --- FILTER: the big cutoff ---------------------------------------------------------
//
// CORNER is the filter: CUTOFF as the hero on the left of its response, RES on
// the right with its scream in red, the model seated on top. COLOUR is what the
// filter is fed and what is blended back -- DRIVE and FAT -- with a lamp that
// glows with the drive. PAN, which only ever turns one side down, stands alone.
inline FxStrip fxFilterStrip(juce::Rectangle<int> stripBounds, const FxKnobSizes& sizes)
{
    FxStrip strip;
    strip.strip = stripBounds;
    placeFxFrame(strip, sizes);
    const FxGrid grid {stripBounds};
    const auto top = stripBounds.getY();

    auto& colour = addFxPanel(strip, grid.span(5.0f, 7.0f), "COLOUR");
    colour.lamp = FxLamp::knob;
    colour.lampKnob = 2;
    const auto colourContent = fxPanelContent(strip, colour);
    placeFxMiniRow(strip, FxRole::knob3, colourContent, top + fxMiniRowTop, sizes, 34);
    placeFxMiniRow(strip, FxRole::knob4, colourContent, top + fxMiniRowBottom, sizes, 34);

    auto& corner = addFxPanel(strip, grid.span(12.0f, 28.0f), "CORNER");
    const auto content = fxPanelContent(strip, corner);
    placeFxKnob(strip, FxRole::knob1, fxColumn(strip, content.getX(), grid.x(17.0f)), FxKnobSize::hero, sizes);
    setFxMarks(strip.spot(FxRole::knob1), {fxLogMark(100.0f, 30.0f, 18000.0f), fxLogMark(1000.0f, 30.0f, 18000.0f),
                                           fxLogMark(10000.0f, 30.0f, 18000.0f)});
    const auto well = addFxWell(strip, fxColumn(strip, grid.x(17.0f), grid.x(35.0f)), FxWellKind::filterResponse);
    seatFxField(strip, well, FxRole::modeA, FxPresentation::list, 132);
    placeFxKnob(strip, FxRole::knob2, fxColumn(strip, grid.x(35.0f), content.getRight()), FxKnobSize::normal, sizes);
    strip.spot(FxRole::knob2).redZone = 0.92f;

    placeFxKnob(strip, FxRole::knob5, grid.span(40.0f, 3.0f), FxKnobSize::normal, sizes);
    strip.spot(FxRole::knob5).bipolar = true;
    return strip;
}

// --- DELAY: the echo machine ---------------------------------------------------------
//
// TIME holds what decides when a repeat lands: the time, its unit as a BPM
// chip against it, and OFFSET with TRIP and DOT engraved where it lands on them.
// The tape between shows every repeat. LOOP holds everything that goes round
// again -- FEEDBACK as the hero, the filter the repeats pass through, FREQ and
// Q -- because the filter is inside the loop and darkens every trip.
inline FxStrip fxDelayStrip(juce::Rectangle<int> stripBounds, const FxKnobSizes& sizes)
{
    FxStrip strip;
    strip.strip = stripBounds;
    placeFxFrame(strip, sizes);
    const FxGrid grid {stripBounds};
    const auto top = stripBounds.getY();

    auto& time = addFxPanel(strip, grid.span(5.0f, 9.0f), "TIME");
    const auto timeContent = fxPanelContent(strip, time);
    const auto width = timeContent.getWidth();
    const auto left = timeContent.getX();
    placeFxKnob(strip, FxRole::knob1, fxColumn(strip, left, left + width * 38 / 100), FxKnobSize::normal, sizes);
    placeFxChip(strip, FxRole::modeB, fxColumn(strip, left + width * 38 / 100, left + width * 62 / 100));
    placeFxKnob(strip, FxRole::knob2, fxColumn(strip, left + width * 62 / 100, timeContent.getRight()),
                FxKnobSize::normal, sizes);
    setFxMarks(strip.spot(FxRole::knob2), {(4.0f / 3.0f) - 0.5f, 1.0f});

    const auto tape = addFxWell(strip, grid.span(14.0f, 13.0f), FxWellKind::delayTape);
    seatFxField(strip, tape, FxRole::modeA, FxPresentation::segmented, 150);

    auto& loop = addFxPanel(strip, grid.span(27.0f, 16.0f), "LOOP");
    loop.lamp = FxLamp::repeats;
    const auto loopContent = fxPanelContent(strip, loop);
    placeFxKnob(strip, FxRole::knob3, fxColumn(strip, loopContent.getX(), grid.x(31.0f)), FxKnobSize::hero, sizes);
    addFxWell(strip, fxColumn(strip, grid.x(31.0f), grid.x(37.0f)), FxWellKind::delayLoop);
    const auto rows = fxColumn(strip, grid.x(37.0f) + 2, loopContent.getRight());
    placeFxMiniRow(strip, FxRole::knob4, rows, top + fxMiniRowTop, sizes, 28);
    placeFxMiniRow(strip, FxRole::knob5, rows, top + fxMiniRowBottom, sizes, 28);
    return strip;
}

// --- REVERB: the space ------------------------------------------------------------------
//
// ROOM is TYPE and SIZE, one function between them: SIZE is the hero and reads
// the decay time the two make. PRE-DLY stands before the tail it delays, drawn
// on a fixed time axis so SIZE and TYPE visibly lengthen it. TONE is the three
// one-poles on the tail -- DAMP inside the loop, the cuts after it -- beside how
// long each frequency rings.
inline FxStrip fxReverbStrip(juce::Rectangle<int> stripBounds, const FxKnobSizes& sizes)
{
    FxStrip strip;
    strip.strip = stripBounds;
    placeFxFrame(strip, sizes);
    const FxGrid grid {stripBounds};
    const auto top = stripBounds.getY();

    auto& room = addFxPanel(strip, grid.span(5.0f, 8.0f), "ROOM");
    const auto roomContent = fxPanelContent(strip, room);
    const auto split = roomContent.getX() + roomContent.getWidth() * 42 / 100;
    placeFxStack(strip, FxRole::modeA, fxColumn(strip, roomContent.getX(), split), fxStackHeight);
    placeFxKnob(strip, FxRole::knob1, fxColumn(strip, split, roomContent.getRight()), FxKnobSize::hero, sizes);

    placeFxKnob(strip, FxRole::knob2, grid.span(13.0f, 3.0f), FxKnobSize::normal, sizes);
    addFxWell(strip, grid.span(16.0f, 10.0f), FxWellKind::reverbTail);

    auto& tone = addFxPanel(strip, grid.span(26.0f, 14.0f), "TONE");
    const auto toneContent = fxPanelContent(strip, tone);
    placeFxKnob(strip, FxRole::knob3, fxColumn(strip, toneContent.getX(), grid.x(29.0f)), FxKnobSize::normal, sizes);
    addFxWell(strip, fxColumn(strip, grid.x(29.0f), grid.x(33.0f)), FxWellKind::reverbDecay);
    const auto rows = fxColumn(strip, grid.x(33.0f) + 2, toneContent.getRight());
    placeFxMiniRow(strip, FxRole::knob5, rows, top + fxMiniRowTop, sizes, 36);
    placeFxMiniRow(strip, FxRole::knob6, rows, top + fxMiniRowBottom, sizes, 36);

    placeFxKnob(strip, FxRole::knob4, grid.span(40.0f, 3.0f), FxKnobSize::normal, sizes);
    return strip;
}

// --- EQ: the console ---------------------------------------------------------------------
//
// Two bands mirrored about their sum: each band's knobs outside, its shape as
// glyphs hugging the curve, its gain bipolar. A band's lamp lights as far as it
// is from flat.
inline FxStrip fxEqualiserStrip(juce::Rectangle<int> stripBounds, const FxKnobSizes& sizes)
{
    FxStrip strip;
    strip.strip = stripBounds;
    placeFxFrame(strip, sizes);
    const FxGrid grid {stripBounds};

    const auto band = [&] (float from, const char* caption, bool right, FxLamp lamp, std::array<FxRole, 3> roles)
    {
        auto& group = addFxPanel(strip, grid.span(from, 9.0f), caption, right);
        group.lamp = lamp;
        const auto content = fxPanelContent(strip, group);
        const auto third = content.getWidth() / 3;
        for (int i = 0; i < 3; ++i)
        {
            const auto column = fxColumn(strip, content.getX() + i * third,
                                         i == 2 ? content.getRight() : content.getX() + (i + 1) * third);
            placeFxKnob(strip, roles[static_cast<size_t>(i)], column, FxKnobSize::normal, sizes);
        }
    };
    band(5.0f, "LOW", false, FxLamp::lowBand, {FxRole::knob1, FxRole::knob2, FxRole::knob3});
    strip.spot(FxRole::knob3).bipolar = true;
    placeFxStack(strip, FxRole::modeA, grid.span(14.0f, 2.0f), fxGlyphStackHeight, FxPresentation::glyphs);
    addFxWell(strip, grid.span(16.0f, 16.0f), FxWellKind::eqResponse);
    placeFxStack(strip, FxRole::modeB, grid.span(32.0f, 2.0f), fxGlyphStackHeight, FxPresentation::glyphs);
    band(34.0f, "HIGH", true, FxLamp::highBand, {FxRole::knob6, FxRole::knob5, FxRole::knob4});
    strip.spot(FxRole::knob6).bipolar = true;
    return strip;
}

// --- COMP: the dynamics unit ---------------------------------------------------------------
//
// CURVE is the gain computer -- THRESH as the hero, the curve, RATIO and KNEE.
// TIMING is the detector and its two times, engraved in decades. Then the
// needle, and OUT: MAKEUP with the AUTO chip against it.
inline FxStrip fxCompressorStrip(juce::Rectangle<int> stripBounds, const FxKnobSizes& sizes)
{
    FxStrip strip;
    strip.strip = stripBounds;
    placeFxFrame(strip, sizes);
    const FxGrid grid {stripBounds};

    auto& curve = addFxPanel(strip, grid.span(5.0f, 19.0f), "CURVE");
    const auto curveContent = fxPanelContent(strip, curve);
    placeFxKnob(strip, FxRole::knob1, fxColumn(strip, curveContent.getX(), grid.x(9.0f)), FxKnobSize::hero, sizes);
    addFxWell(strip, fxColumn(strip, grid.x(9.0f), grid.x(18.0f)), FxWellKind::compressorCurve);
    placeFxKnob(strip, FxRole::knob2, fxColumn(strip, grid.x(18.0f), grid.x(21.0f)), FxKnobSize::normal, sizes);
    placeFxKnob(strip, FxRole::knob6, fxColumn(strip, grid.x(21.0f), curveContent.getRight()), FxKnobSize::normal, sizes);

    auto& timing = addFxPanel(strip, grid.span(24.0f, 8.0f), "TIMING");
    const auto timingContent = fxPanelContent(strip, timing);
    const auto stackRight = timingContent.getX() + juce::jmax(30, grid.x(26.0f) - grid.x(24.0f) - 4);
    placeFxStack(strip, FxRole::modeA, fxColumn(strip, timingContent.getX(), stackRight), fxStackHeight);
    const auto half = (timingContent.getRight() - stackRight) / 2;
    placeFxKnob(strip, FxRole::knob3, fxColumn(strip, stackRight, stackRight + half), FxKnobSize::normal, sizes);
    placeFxKnob(strip, FxRole::knob4, fxColumn(strip, stackRight + half, timingContent.getRight()),
                FxKnobSize::normal, sizes);
    setFxMarks(strip.spot(FxRole::knob3), {fxPowerMark(0.001f, 0.0001f, 0.1f, 2.0f), fxPowerMark(0.01f, 0.0001f, 0.1f, 2.0f)});
    setFxMarks(strip.spot(FxRole::knob4), {fxPowerMark(0.03f, 0.01f, 1.0f, 2.0f), fxPowerMark(0.1f, 0.01f, 1.0f, 2.0f),
                                           fxPowerMark(0.3f, 0.01f, 1.0f, 2.0f)});

    addFxWell(strip, grid.span(32.0f, 5.0f), FxWellKind::compressorMeter);

    auto& out = addFxPanel(strip, grid.span(37.0f, 6.0f), "OUT");
    const auto outContent = fxPanelContent(strip, out);
    const auto makeupRight = outContent.getX() + outContent.getWidth() / 2;
    placeFxKnob(strip, FxRole::knob5, fxColumn(strip, outContent.getX(), makeupRight), FxKnobSize::normal, sizes);
    placeFxChip(strip, FxRole::modeB, fxColumn(strip, makeupRight, outContent.getRight()));
    return strip;
}

// --- CHORUS: the stereo box ------------------------------------------------------------------
//
// SWEEP is the LFO -- RATE, its BPM chip, DEPTH -- with a lamp that breathes at
// the rate. TAPS is the two delays and the picture of them swinging. FILTER's
// caption is its LPF/HPF switch.
inline FxStrip fxChorusStrip(juce::Rectangle<int> stripBounds, const FxKnobSizes& sizes)
{
    FxStrip strip;
    strip.strip = stripBounds;
    placeFxFrame(strip, sizes);
    const FxGrid grid {stripBounds};

    auto& sweep = addFxPanel(strip, grid.span(5.0f, 9.0f), "SWEEP");
    sweep.lamp = FxLamp::rate;
    const auto sweepContent = fxPanelContent(strip, sweep);
    const auto width = sweepContent.getWidth();
    const auto left = sweepContent.getX();
    placeFxKnob(strip, FxRole::knob1, fxColumn(strip, left, left + width * 38 / 100), FxKnobSize::normal, sizes);
    placeFxChip(strip, FxRole::modeA, fxColumn(strip, left + width * 38 / 100, left + width * 62 / 100));
    placeFxKnob(strip, FxRole::knob4, fxColumn(strip, left + width * 62 / 100, sweepContent.getRight()),
                FxKnobSize::normal, sizes);

    auto& taps = addFxPanel(strip, grid.span(14.0f, 23.0f), "TAPS");
    const auto tapsContent = fxPanelContent(strip, taps);
    addFxWell(strip, fxColumn(strip, tapsContent.getX(), grid.x(31.0f)), FxWellKind::chorusTaps);
    placeFxKnob(strip, FxRole::knob2, fxColumn(strip, grid.x(31.0f), grid.x(34.0f)), FxKnobSize::normal, sizes);
    placeFxKnob(strip, FxRole::knob3, fxColumn(strip, grid.x(34.0f), tapsContent.getRight()), FxKnobSize::normal, sizes);

    placeFxKnob(strip, FxRole::knob5, grid.span(37.0f, 3.0f), FxKnobSize::normal, sizes);
    placeFxKnob(strip, FxRole::knob6, grid.span(40.0f, 3.0f), FxKnobSize::normal, sizes);
    placeFxCaptionSwitch(strip, FxRole::modeB, FxRole::knob6);
    return strip;
}

// --- PHASER: the swirl -----------------------------------------------------------------------
//
// MOTION is RATE, the hero as on every phase pedal, with its BPM chip and a lamp
// that pulses with the LFO. SWEEP is the comb and the three things that place
// it: the stage count seated on top, FREQ and DEPTH beside it. FEEDBACK is
// bipolar. STEREO is SPREAD, which draws both channels' LFOs, and WIDTH.
inline FxStrip fxPhaserStrip(juce::Rectangle<int> stripBounds, const FxKnobSizes& sizes)
{
    FxStrip strip;
    strip.strip = stripBounds;
    placeFxFrame(strip, sizes);
    const FxGrid grid {stripBounds};

    auto& motion = addFxPanel(strip, grid.span(5.0f, 7.0f), "MOTION");
    motion.lamp = FxLamp::rate;
    const auto motionContent = fxPanelContent(strip, motion);
    const auto rateRight = motionContent.getX() + motionContent.getWidth() * 62 / 100;
    placeFxKnob(strip, FxRole::knob1, fxColumn(strip, motionContent.getX(), rateRight), FxKnobSize::hero, sizes);
    placeFxChip(strip, FxRole::modeA, fxColumn(strip, rateRight, motionContent.getRight()));

    auto& sweep = addFxPanel(strip, grid.span(12.0f, 22.0f), "SWEEP");
    const auto sweepContent = fxPanelContent(strip, sweep);
    const auto comb = addFxWell(strip, fxColumn(strip, sweepContent.getX(), grid.x(28.0f)), FxWellKind::phaserComb);
    seatFxField(strip, comb, FxRole::modeB, FxPresentation::segmented, 112);
    placeFxKnob(strip, FxRole::knob3, fxColumn(strip, grid.x(28.0f), grid.x(31.0f)), FxKnobSize::normal, sizes);
    placeFxKnob(strip, FxRole::knob2, fxColumn(strip, grid.x(31.0f), sweepContent.getRight()), FxKnobSize::normal, sizes);

    placeFxKnob(strip, FxRole::knob4, grid.span(34.0f, 3.0f), FxKnobSize::normal, sizes);
    strip.spot(FxRole::knob4).bipolar = true;

    auto& stereo = addFxPanel(strip, grid.span(37.0f, 6.0f), "STEREO");
    const auto stereoContent = fxPanelContent(strip, stereo);
    const auto half = stereoContent.getX() + stereoContent.getWidth() / 2;
    placeFxKnob(strip, FxRole::knob5, fxColumn(strip, stereoContent.getX(), half), FxKnobSize::normal, sizes);
    strip.spot(FxRole::knob5).secondPointer = true;
    strip.spot(FxRole::knob5).bipolar = true;
    placeFxKnob(strip, FxRole::knob6, fxColumn(strip, half, stereoContent.getRight()), FxKnobSize::normal, sizes);
    return strip;
}

// --- FLANGER: the jet ---------------------------------------------------------------------
//
// MOTION as on the phaser: RATE the hero, its BPM chip, a lamp at the LFO's
// pace. Then JET: the comb as it stands, with DELAY, where the sweep starts,
// and DEPTH, how far it travels, beside it. FEEDBACK, bipolar, and SPREAD, the
// two sides' LFOs set apart, close the body.
inline FxStrip fxFlangerStrip(juce::Rectangle<int> stripBounds, const FxKnobSizes& sizes)
{
    FxStrip strip;
    strip.strip = stripBounds;
    placeFxFrame(strip, sizes);
    const FxGrid grid {stripBounds};

    auto& motion = addFxPanel(strip, grid.span(5.0f, 7.0f), "MOTION");
    motion.lamp = FxLamp::rate;
    const auto motionContent = fxPanelContent(strip, motion);
    const auto rateRight = motionContent.getX() + motionContent.getWidth() * 62 / 100;
    placeFxKnob(strip, FxRole::knob1, fxColumn(strip, motionContent.getX(), rateRight), FxKnobSize::hero, sizes);
    placeFxChip(strip, FxRole::modeA, fxColumn(strip, rateRight, motionContent.getRight()));

    auto& jet = addFxPanel(strip, grid.span(12.0f, 25.0f), "JET");
    const auto jetContent = fxPanelContent(strip, jet);
    addFxWell(strip, fxColumn(strip, jetContent.getX(), grid.x(31.0f)), FxWellKind::flangerComb);
    placeFxKnob(strip, FxRole::knob4, fxColumn(strip, grid.x(31.0f), grid.x(34.0f)), FxKnobSize::normal, sizes);
    placeFxKnob(strip, FxRole::knob2, fxColumn(strip, grid.x(34.0f), jetContent.getRight()), FxKnobSize::normal, sizes);

    placeFxKnob(strip, FxRole::knob3, grid.span(37.0f, 3.0f), FxKnobSize::normal, sizes);
    strip.spot(FxRole::knob3).bipolar = true;
    placeFxKnob(strip, FxRole::knob5, grid.span(40.0f, 3.0f), FxKnobSize::normal, sizes);
    strip.spot(FxRole::knob5).secondPointer = true;
    strip.spot(FxRole::knob5).bipolar = true;
    return strip;
}

// One slot's strip, for the effect type it holds. OFF is a slot with nothing in
// it and is never shown; it gets the generic frame so that nothing asking for
// it gets nothing.
inline FxStrip fxStripLayout(int type, juce::Rectangle<int> strip, const FxKnobSizes& sizes)
{
    switch (fxTypeOf(static_cast<float>(type)))
    {
        case FxType::distortion: return fxDistortionStrip(strip, sizes);
        case FxType::filter:     return fxFilterStrip(strip, sizes);
        case FxType::delay:      return fxDelayStrip(strip, sizes);
        case FxType::reverb:     return fxReverbStrip(strip, sizes);
        case FxType::equaliser:  return fxEqualiserStrip(strip, sizes);
        case FxType::compressor: return fxCompressorStrip(strip, sizes);
        case FxType::chorus:     return fxChorusStrip(strip, sizes);
        case FxType::phaser:     return fxPhaserStrip(strip, sizes);
        case FxType::flanger:    return fxFlangerStrip(strip, sizes);
        case FxType::off:
            break;
    }
    return fxGenericStrip(type, strip, sizes);
}
}
