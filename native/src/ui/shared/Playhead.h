#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace rhino
{
inline const juce::Colour playheadColour {0xff57a7ff};

// Each footprint includes physical-pixel rounding at fractional desktop scales.
inline juce::Rectangle<int> playheadDamage(float previous, float next, juce::Rectangle<int> area)
{
    const auto strip = [area](float x)
    {
        return x < 0 ? juce::Rectangle<int>{}
                     : juce::Rectangle<float>{x - 2.0f, static_cast<float>(area.getY()), 6.0f,
                                               static_cast<float>(area.getHeight())}
                           .getSmallestIntegerContainer().getIntersection(area);
    };
    return strip(previous).getUnion(strip(next));
}

void movePlayhead(juce::Component& owner, float& current, float next, juce::Rectangle<int> area);

// Hands what a vblank callback repainted to the window before the window
// paints this frame. In the pinned JUCE (37c894f) the Windows Direct2D peer
// runs vblank listeners before it paints the damage it has collected, and
// repaint() only invalidates, so damage asked for from a vblank listener was
// painted a frame late: a narrow repaint - a playhead's strip, a jog wheel's
// marker - erased the old picture and clipped out the new one. Called once at
// the end of such a callback, after every repaint it makes. Does nothing on
// other platforms and renderers. Recheck it whenever JUCE is upgraded.
void deliverVBlankDamage(juce::Component& owner);
}
