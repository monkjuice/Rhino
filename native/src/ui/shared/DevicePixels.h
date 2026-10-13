#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

namespace rhino
{
// Where a component's origin falls inside a device pixel of the window that
// paints it, `scale` device pixels to a point: how far past the device pixel
// it starts in. The window paints every component from its own origin, so
// that is the component's place in the window times the scale. At 125 or 150
// per cent most components start between two device pixels, and a picture
// painted at the device scale and drawn from a point of the component is
// resampled half a pixel soft. Drawn in device pixels, from this far back of
// a whole one, it is copied as it was painted: the renderer blits a picture
// only when the drawing lands on whole device pixels to within an eighth, so
// the position is worked out in device pixels, where it is exact at every
// scale Windows offers, never scaled down to a point and back. A component
// painted on its own, as the tests paint one, is its own top level and
// stands on the grid.
inline juce::Point<float> devicePhase(const juce::Component& component, float scale)
{
    const auto origin = component.getTopLevelComponent()->getLocalPoint(&component, juce::Point<float>()) * scale;
    return {origin.x - std::floor(origin.x), origin.y - std::floor(origin.y)};
}
}
