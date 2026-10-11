#pragma once
#include "DjDeckPanel.h"
#include <cmath>

// What DjDeckPanel's own units share - the lengths its keys offer and how
// they are named. Nothing else includes it.

namespace rhino::deckpanel
{
inline constexpr double beatLoopSizes[] {0.125, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0};
inline constexpr int jumpSizes[] {1, 2, 4, 8, 16, 32};
inline constexpr int tempoRanges[] {6, 10, 16, 100};

inline juce::String beatsName(double beats)
{
    if (beats >= 1.0) return juce::String(static_cast<int>(beats));
    return "1/" + juce::String(static_cast<int>(std::round(1.0 / beats)));
}

inline juce::String rangeName(int range)
{
    return range >= 100 ? juce::String("WIDE") : juce::String::charToString(static_cast<juce::juce_wchar>(0xb1)) + juce::String(range) + "%";
}
}
