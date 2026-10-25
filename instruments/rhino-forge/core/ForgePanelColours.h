#pragma once

#include <algorithm>

// How many colours an oscillator's plate can be painted, and which one a new
// oscillator starts on.
//
// These two numbers are the patch's rather than the panel's. A preset and a
// project store the choice as an index, so the Processor needs the count to
// keep a stored index legal and the default to answer for a state that has
// none - and it needs neither to know what the colours look like. The colours
// themselves, and their names, are the panel's (ui/ForgeStyle.h), which
// checks its list against this count.
namespace rhino::forge
{
inline constexpr int panelColourCount = 4;
// Green, in the order the panel lists them.
inline constexpr int defaultPanelColourIndex = 2;

inline constexpr int clampPanelColour(int stored) noexcept
{
    return std::clamp(stored, 0, panelColourCount - 1);
}
}
