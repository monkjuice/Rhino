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

// The knob sizes a strip draws with, worked out once per layout from the
// panel's shared diameter.
struct FxKnobSizes
{
    // The panel's shared diameter; what an ordinary module's knob is drawn at.
    int shared = 46;
};

inline FxKnobSizes fxKnobSizesFor(juce::Rectangle<int> panelBounds)
{
    return {uniformKnobDiameter(panelBounds)};
}

// Where one control goes. `component` is the rectangle its component takes;
// `caption` is its label's, or empty when it shows none. Both are in the same
// coordinates as the strip that was laid out.
struct FxSpot
{
    juce::Rectangle<int> component;
    juce::Rectangle<int> caption;
};

// The picture strips of a slot. Today every type has one, standing after its
// plate and mode fields.
inline constexpr int maxFxWells = 2;

struct FxStrip
{
    std::array<FxSpot, fxRoleCount> spots {};
    std::array<juce::Rectangle<int>, maxFxWells> wells {};
    int wellCount = 0;

    const FxSpot& spot(FxRole role) const { return spots[static_cast<size_t>(fxRoleIndex(role))]; }
};

// --- The row every slot had before types had layouts of their own ---------------
//
// Twelve cells by weight -- the plate and the two mode fields two each, the six
// knobs, MIX, LEVEL and the bypass one each -- with a display three wide after
// the third. The arithmetic is cellBounds' own, integer division per cell
// included, so a strip laid out this way lands on exactly the pixels the
// declared row did.
namespace fxlegacy
{
inline constexpr std::array<int, fxRoleCount> weights {2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1};
inline constexpr int displayWeight = 3;
inline constexpr int displayAfter = 3;

inline int total()
{
    auto sum = displayWeight;
    for (const auto weight : weights) sum += weight;
    return sum;
}

inline juce::Rectangle<int> cell(juce::Rectangle<int> strip, int role)
{
    const auto all = total();
    auto x = strip.getX();
    for (int i = 0; i < role; ++i)
        x += strip.getWidth() * weights[static_cast<size_t>(i)] / all;
    if (role >= displayAfter) x += strip.getWidth() * displayWeight / all;
    return {x, strip.getY(), strip.getWidth() * weights[static_cast<size_t>(role)] / all, strip.getHeight()};
}

inline juce::Rectangle<int> display(juce::Rectangle<int> strip)
{
    const auto all = total();
    auto x = strip.getX();
    for (int i = 0; i < displayAfter; ++i)
        x += strip.getWidth() * weights[static_cast<size_t>(i)] / all;
    return {x, strip.getY(), strip.getWidth() * displayWeight / all, strip.getHeight()};
}

inline FxStrip layout(juce::Rectangle<int> strip, const FxKnobSizes& sizes)
{
    FxStrip result;
    for (int role = 0; role < fxRoleCount; ++role)
    {
        const auto box = cell(strip, role);
        auto& spot = result.spots[static_cast<size_t>(role)];
        const auto which = static_cast<FxRole>(role);
        if (which == FxRole::type)
        {
            spot.component = juce::Rectangle<int>(juce::jmin(box.getWidth() - 10, maxPlateWidth),
                                                  juce::jmin(box.getHeight() - 8, maxPlateHeight))
                                 .withCentre(box.getCentre());
        }
        else if (which == FxRole::modeA || which == FxRole::modeB)
        {
            auto block = juce::Rectangle<int>(juce::jmin(box.getWidth() - 10, maxSelectorWidth),
                                              juce::jmin(box.getHeight(), stepperLabelHeight + selectorHeight))
                             .withCentre(box.getCentre());
            spot.caption = block.removeFromTop(stepperLabelHeight);
            spot.component = block;
        }
        else if (which == FxRole::bypass)
        {
            spot.component = juce::Rectangle<int>(juce::jmin(box.getWidth() - 8, maxChipWidth),
                                                  juce::jmin(box.getHeight(), chipHeight))
                                 .withCentre(box.getCentre());
        }
        else
        {
            // A compact module's knob, as knobDiameterFor sizes one.
            const auto diameter = juce::jmax(24, juce::jmin(box.getWidth() - 6,
                                                            box.getHeight() - knobLabelHeight - compactKnobGap,
                                                            sizes.shared * compactKnobPercent / 100));
            const auto block = juce::Rectangle<int>(diameter, knobLabelHeight + diameter)
                                   .withCentre(box.getCentre());
            spot.caption = knobLabelBounds(block, box);
            spot.component = block.withTrimmedTop(knobLabelHeight);
        }
    }
    result.wells[0] = display(strip);
    result.wellCount = 1;
    return result;
}
}

// One slot's strip, for the effect type it holds.
inline FxStrip fxStripLayout(int type, juce::Rectangle<int> strip, const FxKnobSizes& sizes)
{
    juce::ignoreUnused(type);
    return fxlegacy::layout(strip, sizes);
}
}
