#pragma once

#include "DuckProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace rhino::duck::ui
{
// The scope: one cycle of the curve over the ducked output and the sidechain,
// as the engine recorded them, with the sloped part to drag, the End marker
// to drag in one-shot mode, the sweep, and a button that resets the curve.
class ScopeView final : public juce::Component,
                        public juce::TooltipClient
{
public:
    explicit ScopeView(Processor&);

    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    juce::String getTooltip() override;

    enum class Part { none, slope, endMarker, reset };
    // What lies under a point, for the tests as well as the pointer.
    Part partAt(juce::Point<int>) const;
    juce::Rectangle<float> plotArea() const;
    juce::Rectangle<float> resetButtonArea() const;

private:
    int currentCurve() const;
    bool showsEndMarker() const;
    float endMarkerX() const;
    void setHover(Part);

    Processor& processor;
    Part hover = Part::none;
    Part dragging = Part::none;
    float dragStartOffset = 0.0f;
    int dragStartX = 0;
    juce::RangedAudioParameter* endParameter = nullptr;
};
}
