#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace tracktion { inline namespace engine { class TransportControl; } }

namespace rhino
{
inline const juce::Colour playheadColour {0xff57a7ff};
double playheadTime(const tracktion::engine::TransportControl&);

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
}
