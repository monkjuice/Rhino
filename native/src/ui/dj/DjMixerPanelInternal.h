#pragma once
#include "DjMixerPanel.h"
#include "dj/DjMixer.h"
#include <algorithm>

// What DjMixerPanel's own units share: its sizes, how a knob stands under
// its caption, and the EQ knob's travel. Nothing else includes it.

namespace rhino::mixerpanel
{
inline constexpr int knobSize = 28, knobLabelHeight = 10, keyHeight = 18, titleHeight = 14;

// A knob under its caption: the caption's ten pixels, the knob, a gap.
inline void knobRow(juce::Rectangle<int>& column, juce::Slider& knob)
{
    column.removeFromTop(knobLabelHeight);
    knob.setBounds(column.removeFromTop(knobSize).withSizeKeepingCentre(knobSize, knobSize));
    column.removeFromTop(2);
}

// Two knobs side by side under their captions.
inline void knobPair(juce::Rectangle<int>& column, juce::Slider& first, juce::Slider& second)
{
    column.removeFromTop(knobLabelHeight);
    auto row = column.removeFromTop(knobSize);
    first.setBounds(row.removeFromLeft(row.getWidth() / 2).withSizeKeepingCentre(knobSize, knobSize));
    second.setBounds(row.withSizeKeepingCentre(knobSize, knobSize));
    column.removeFromTop(2);
}

// Where a control's caption stands: the ten pixels over it.
inline juce::Rectangle<int> captionOver(const juce::Component& control)
{
    return control.getBounds().withHeight(knobLabelHeight).translated(0, -knobLabelHeight);
}

// The EQ knob: off at the bottom, unity in the middle, +6 dB at the top,
// the bottom half a steep fall to a kill.
inline float eqDbFor(double knob)
{
    if (knob <= 0.01) return Session::djKillDb;
    if (knob < 0.5) return static_cast<float>(-30.0 * (0.5 - knob) / 0.49);
    return static_cast<float>(DjChannelStrip::maximumEqDb * (knob - 0.5) / 0.5);
}

inline double knobForEqDb(float decibels)
{
    if (decibels <= Session::djKillDb) return 0.0;
    if (decibels < 0.0f) return 0.5 - 0.49 * std::min(1.0f, -decibels / 30.0f);
    return 0.5 + 0.5 * std::min(1.0f, decibels / DjChannelStrip::maximumEqDb);
}
}
