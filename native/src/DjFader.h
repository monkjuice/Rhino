#pragma once
#include "Theme.h"
#include <functional>

namespace rhino
{
// A fader, vertical or across, dragged by the pixel. Shift drags fine, a
// double-click returns it to its resting value. Values run 0 to 1; what that
// means - a gain, a tempo offset, a crossfader side - is the owner's.
//
// It paints all of itself, on the ground of the panel it stands in, so a
// drag step repaints the fader and nothing behind it.
class DjFader final : public juce::Component,
                      public juce::SettableTooltipClient
{
public:
    explicit DjFader(bool isVertical, juce::Colour accentColour = palette::volume,
                     juce::Colour groundColour = palette::sideSurface);
    void setValue(float v, bool notify);
    void setDefault(float v);
    // Where the cap shows its centre detent: a tempo fader's zero, a
    // crossfader's middle. Negative for none.
    void setDetent(float v);
    // Tick marks along the groove, the CDJ's tempo scale.
    void setTicks(int count);
    bool isDragging() const { return dragging; }
    std::function<void(float)> onChange;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    float positionFor(float v) const;
    static constexpr float capLength = 18.0f;
    bool vertical;
    juce::Colour accent, ground;
    float current = 1.0f, resting = 1.0f, detent = -1.0f, startValue = 1.0f;
    juce::Point<float> startPoint;
    bool dragging = false;
    int ticks = 0;
};
}
