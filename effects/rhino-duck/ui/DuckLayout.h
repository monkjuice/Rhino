#pragma once

#include <juce_graphics/juce_graphics.h>

#include <array>

// Where everything on the panel lands, as pure arithmetic over the panel's
// bounds, so the layout test can check it without a window.
//
// Left, the identity: the name, the Mix knob and its reading, the band split.
// Right, the clock: the Length and Trigger chips over the scope, the Shift
// arrows under it, and the sixteen curves in two rows of eight.
namespace rhino::duck::ui
{
constexpr int panelWidth = 840;
constexpr int panelHeight = 452;

struct Layout
{
    juce::Rectangle<int> title, tagline;
    juce::Rectangle<int> knob, mixReading;
    juce::Rectangle<int> bandChip, soloChip, cutoffSlider, cutoffReading;
    std::array<juce::Rectangle<int>, 4> lengthChips;
    std::array<juce::Rectangle<int>, 3> triggerChips;
    juce::Rectangle<int> loopChip, menuButton;
    juce::Rectangle<int> scope;
    std::array<juce::Rectangle<int>, 4> shiftButtons;   // coarse left, fine left, fine right, coarse right
    juce::Rectangle<int> curveCaption;
    std::array<juce::Rectangle<int>, 16> curveCells;
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
    constexpr int margin = 16;
    constexpr int leftWidth = 240;
    constexpr int gap = 16;
    auto left = bounds.reduced(margin);
    auto right = left.removeFromRight(left.getWidth() - leftWidth - gap);
    left.removeFromRight(gap);

    layout.title = left.removeFromTop(30);
    layout.tagline = left.removeFromTop(16);
    left.removeFromTop(18);
    const auto knobSize = 176;
    layout.knob = left.removeFromTop(knobSize).withSizeKeepingCentre(knobSize, knobSize);
    left.removeFromTop(8);
    layout.mixReading = left.removeFromTop(24);
    left.removeFromTop(26);
    auto bandRow = left.removeFromTop(28);
    layout.bandChip = bandRow.removeFromLeft(76);
    bandRow.removeFromLeft(10);
    layout.soloChip = bandRow.removeFromLeft(76);
    left.removeFromTop(16);
    layout.cutoffSlider = left.removeFromTop(22);
    left.removeFromTop(4);
    layout.cutoffReading = left.removeFromTop(18);

    auto top = right.removeFromTop(28);
    auto lengths = top;
    for (auto& chip : layout.lengthChips)
    {
        chip = lengths.removeFromLeft(52);
        lengths.removeFromLeft(6);
    }
    layout.menuButton = top.removeFromRight(28);
    top.removeFromRight(8);
    layout.loopChip = top.removeFromRight(48);
    top.removeFromRight(8);
    for (int i = 2; i >= 0; --i)
    {
        layout.triggerChips[static_cast<size_t>(i)] = top.removeFromRight(66);
        top.removeFromRight(6);
    }
    right.removeFromTop(12);

    layout.scope = right.removeFromTop(216);
    right.removeFromTop(8);

    auto shiftRow = right.removeFromTop(26);
    layout.shiftButtons[0] = shiftRow.removeFromLeft(40);
    shiftRow.removeFromLeft(6);
    layout.shiftButtons[1] = shiftRow.removeFromLeft(40);
    layout.shiftButtons[3] = shiftRow.removeFromRight(40);
    shiftRow.removeFromRight(6);
    layout.shiftButtons[2] = shiftRow.removeFromRight(40);
    layout.curveCaption = shiftRow.reduced(8, 0);
    right.removeFromTop(8);

    const auto cellGap = 6;
    const auto rowGap = 8;
    const auto cellHeight = (right.getHeight() - rowGap) / 2;
    const auto cellWidth = (right.getWidth() - 7 * cellGap) / 8;
    const auto spare = right.getWidth() - (cellWidth * 8 + cellGap * 7);
    for (int row = 0; row < 2; ++row)
    {
        auto x = right.getX() + spare / 2;
        const auto y = right.getY() + row * (cellHeight + rowGap);
        for (int column = 0; column < 8; ++column)
        {
            layout.curveCells[static_cast<size_t>(curveAtCell(column, row))] = { x, y, cellWidth, cellHeight };
            x += cellWidth + cellGap;
        }
    }
    return layout;
}
}
