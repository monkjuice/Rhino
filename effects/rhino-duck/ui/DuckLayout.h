#pragma once

#include <juce_graphics/juce_graphics.h>

#include <array>

// Where everything on the panel lands, as pure arithmetic over the panel's
// bounds, so the layout test can check it without a window.
//
// Left, the rail: the name, the duck that sinks with the gain reduction, and
// Mix. Right, the clock and the curve: the Length and Trigger chips over the
// scope, the Shift arrows under it, the sixteen curves in two rows of eight,
// and below them the strip that bends the chosen curve (Attack, Hold,
// Release, Depth) beside the band split. Across the foot, the preset bar
// steps through the sixteen curves.
namespace rhino::duck::ui
{
constexpr int panelWidth = 1000;
constexpr int panelHeight = 620;
constexpr int paramCount = 4;   // Attack, Hold, Release, Depth, in that order

struct Layout
{
    // The plates the editor paints behind the controls.
    juce::Rectangle<int> rail, mainPlate, strip, presetBar;

    juce::Rectangle<int> brand, meter;
    juce::Rectangle<int> mixTitle, knob, dryLabel, wetLabel, mixReading;

    std::array<juce::Rectangle<int>, 4> lengthChips;
    std::array<juce::Rectangle<int>, 3> triggerChips;
    juce::Rectangle<int> loopChip, menuButton;
    juce::Rectangle<int> scope;
    std::array<juce::Rectangle<int>, 4> shiftButtons;   // coarse left, fine left, fine right, coarse right
    juce::Rectangle<int> curveCaption;
    std::array<juce::Rectangle<int>, 16> curveCells;

    std::array<juce::Rectangle<int>, paramCount> paramTitles, paramKnobs, paramLows, paramHighs, paramValues;
    juce::Rectangle<int> bandTitle, soloChip, cutoffSlider, cutoffLow, cutoffHigh, cutoffReading, wholeSwitch;

    juce::Rectangle<int> presetTitle, presetBack, presetForward, presetBox, footer;
};

// The curve a cell holds. The first eight fill the left four columns in two
// rows; each of the other four columns holds a family's pair.
inline int curveAtCell(int column, int row) noexcept
{
    if (column < 4) return row * 4 + column;
    return 8 + (column - 4) * 2 + row;
}

inline Layout layoutFor(juce::Rectangle<int> bounds)
{
    Layout layout;
    constexpr int margin = 12;
    constexpr int gap = 10;
    auto area = bounds.reduced(margin);
    layout.presetBar = area.removeFromBottom(44);
    area.removeFromBottom(gap);
    layout.rail = area.removeFromLeft(224);
    area.removeFromLeft(gap);
    layout.strip = area.removeFromBottom(120);
    area.removeFromBottom(gap);
    layout.mainPlate = area;

    // The rail.
    auto rail = layout.rail.reduced(14);
    layout.brand = rail.removeFromTop(86);
    rail.removeFromTop(10);
    layout.meter = rail.removeFromTop(262);
    rail.removeFromTop(12);
    layout.mixTitle = rail.removeFromTop(16);
    const auto knobSize = 100;
    layout.knob = rail.removeFromTop(knobSize).withSizeKeepingCentre(knobSize, knobSize);
    layout.dryLabel = { layout.knob.getX() - 40, layout.knob.getBottom() - 18, 36, 16 };
    layout.wetLabel = { layout.knob.getRight() + 4, layout.knob.getBottom() - 18, 36, 16 };
    layout.mixReading = rail.removeFromTop(22);

    // Over the scope: Length on the left, Trigger, Loop and the menu right.
    auto main = layout.mainPlate.reduced(12);
    auto top = main.removeFromTop(30);
    auto lengths = top;
    for (auto& chip : layout.lengthChips)
    {
        chip = lengths.removeFromLeft(56);
        lengths.removeFromLeft(6);
    }
    layout.menuButton = top.removeFromRight(34);
    top.removeFromRight(8);
    layout.loopChip = top.removeFromRight(52);
    top.removeFromRight(8);
    for (int i = 2; i >= 0; --i)
    {
        layout.triggerChips[static_cast<size_t>(i)] = top.removeFromRight(72);
        top.removeFromRight(6);
    }
    main.removeFromTop(10);

    // The sixteen cells along the bottom, the Shift row over them, and the
    // scope takes what is left: it is the panel's reason for being.
    auto cells = main.removeFromBottom(82);
    main.removeFromBottom(8);
    auto shiftRow = main.removeFromBottom(26);
    main.removeFromBottom(8);
    layout.scope = main;

    layout.shiftButtons[0] = shiftRow.removeFromLeft(44);
    shiftRow.removeFromLeft(6);
    layout.shiftButtons[1] = shiftRow.removeFromLeft(44);
    layout.shiftButtons[3] = shiftRow.removeFromRight(44);
    shiftRow.removeFromRight(6);
    layout.shiftButtons[2] = shiftRow.removeFromRight(44);
    layout.curveCaption = shiftRow.reduced(8, 0);

    const auto cellGap = 6;
    const auto rowGap = 6;
    const auto cellHeight = (cells.getHeight() - rowGap) / 2;
    const auto cellWidth = (cells.getWidth() - 7 * cellGap) / 8;
    const auto spare = cells.getWidth() - (cellWidth * 8 + cellGap * 7);
    for (int row = 0; row < 2; ++row)
    {
        auto x = cells.getX() + spare / 2;
        const auto y = cells.getY() + row * (cellHeight + rowGap);
        for (int column = 0; column < 8; ++column)
        {
            layout.curveCells[static_cast<size_t>(curveAtCell(column, row))] = { x, y, cellWidth, cellHeight };
            x += cellWidth + cellGap;
        }
    }

    // The strip: four knobs, each a title, the knob, its range's two ends
    // and a reading, then the band split.
    auto strip = layout.strip.reduced(14, 10);
    for (size_t i = 0; i < paramCount; ++i)
    {
        auto group = strip.removeFromLeft(100);
        strip.removeFromLeft(12);
        layout.paramTitles[i] = group.removeFromTop(14);
        group.removeFromTop(2);
        layout.paramKnobs[i] = group.removeFromTop(52).withSizeKeepingCentre(52, 52);
        auto ends = group.removeFromTop(12);
        layout.paramLows[i] = ends.removeFromLeft(48);
        layout.paramHighs[i] = ends.removeFromRight(48);
        layout.paramValues[i] = group.removeFromTop(20).withSizeKeepingCentre(76, 20);
    }
    auto band = strip;
    auto bandTop = band.removeFromTop(22);
    layout.soloChip = bandTop.removeFromRight(64);
    layout.bandTitle = bandTop.removeFromLeft(80);
    band.removeFromTop(6);
    layout.cutoffSlider = band.removeFromTop(20);
    band.removeFromTop(4);
    auto cutoffRow = band.removeFromTop(20);
    layout.cutoffLow = cutoffRow.removeFromLeft(48);
    layout.cutoffHigh = cutoffRow.removeFromRight(56);
    layout.cutoffReading = cutoffRow.withSizeKeepingCentre(std::min(cutoffRow.getWidth() - 8, 128), 20);
    band.removeFromTop(6);
    layout.wholeSwitch = band.removeFromTop(22).withWidth(150);

    // The preset bar: the title, the two arrows and the box that opens the
    // list, and the maker's mark at the far end.
    auto bar = layout.presetBar.reduced(12, 7);
    layout.presetTitle = bar.removeFromLeft(64);
    bar.removeFromLeft(8);
    layout.presetBack = bar.removeFromLeft(34);
    bar.removeFromLeft(6);
    layout.presetForward = bar.removeFromLeft(34);
    bar.removeFromLeft(10);
    layout.presetBox = bar.removeFromLeft(280);
    layout.footer = bar.removeFromRight(150);
    return layout;
}
}
