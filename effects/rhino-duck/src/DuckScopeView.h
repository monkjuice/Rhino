#pragma once

#include "DuckProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace rhino::duck::ui
{
// The scope: one cycle of the curve over what the engine recorded - the kick
// on the sidechain, the signal before the duck and the signal after it - with
// the sloped part to drag (Hold), a handle at its end to drag (Release), the
// End marker to drag in one-shot mode, the sweep, and a button that resets
// the curve.
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

    enum class Part { none, slope, holdHandle, releaseHandle, endMarker, reset };
    // What lies under a point, for the tests as well as the pointer.
    Part partAt(juce::Point<int>) const;
    juce::Rectangle<float> plotArea() const;
    juce::Rectangle<float> resetButtonArea() const;
    // Where the sloped part's two handles are drawn: its start and its end.
    juce::Point<float> handlePoint(Part) const;

private:
    int currentCurve() const;
    bool showsEndMarker() const;
    float endMarkerX() const;
    void setHover(Part);

    Processor& processor;
    Part hover = Part::none;
    Part dragging = Part::none;
    float dragStartValue = 0.0f;
    int dragStartX = 0;
    juce::RangedAudioParameter* endParameter = nullptr;
};
}
