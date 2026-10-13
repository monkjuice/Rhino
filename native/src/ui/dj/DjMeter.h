#pragma once
#include "ui/shared/Theme.h"

namespace rhino
{
// A level meter, lit from the bottom (or the left), falling back at a
// steady rate with the peak held for a moment. Fed a peak per tick by the
// owner's timer; it never reads the engine itself. It paints all of
// itself, so a tick that moves it repaints the meter and nothing behind it.
class DjMeter final : public juce::Component
{
public:
    explicit DjMeter(bool isVertical = true);
    void feed(float peakLinear);
    void paint(juce::Graphics&) override;

private:
    static constexpr float floorDb = -48.0f, ceilingDb = 3.0f, fall = 0.03f;
    bool vertical;
    float level = 0.0f, held = 0.0f, lastHeld = 0.0f;
    int holdTicks = 0;
};
}
