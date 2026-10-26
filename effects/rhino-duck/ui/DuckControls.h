#pragma once

#include "DuckTheme.h"
#include "../core/DuckCurves.h"

#include <juce_gui_basics/juce_gui_basics.h>

// The panel's own controls: a chip, the Mix knob, the cutoff slider and a
// curve button. Each draws itself, so the panel needs no look-and-feel.
namespace rhino::duck::ui
{
// A rounded button that is lit in the accent colour while on. With `bars`
// it draws three lines instead of its text: the menu.
class Chip final : public juce::Button
{
public:
    explicit Chip(const juce::String& text) : juce::Button(text) { setButtonText(text); }

    bool bars = false;

    void paintButton(juce::Graphics& g, bool over, bool down) override
    {
        const auto area = getLocalBounds().toFloat().reduced(0.5f);
        const auto lit = getToggleState();
        auto fill = lit ? colour::accent : (over ? colour::chipHover : colour::chip);
        if (down) fill = fill.brighter(0.12f);
        g.setColour(fill);
        g.fillRoundedRectangle(area, area.getHeight() * 0.5f);
        g.setColour(lit ? colour::accentDark : colour::chipText);
        if (bars)
        {
            const auto centre = area.getCentre();
            for (int i = -1; i <= 1; ++i)
                g.fillRect(juce::Rectangle<float>(14.0f, 2.0f).withCentre({ centre.x, centre.y + static_cast<float>(i) * 5.0f }));
            return;
        }
        g.setFont(font(Face::header, 13.0f));
        g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred, false);
    }
};

// The one big knob. A ring that fills in the accent colour as it turns.
class MixKnob final : public juce::Slider
{
public:
    MixKnob()
    {
        setSliderStyle(RotaryHorizontalVerticalDrag);
        setTextBoxStyle(NoTextBox, false, 0, 0);
        setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        setDoubleClickReturnValue(true, 1.0);
        setMouseDragSensitivity(220);
    }

    void paint(juce::Graphics& g) override
    {
        const auto square = getLocalBounds().toFloat().withSizeKeepingCentre(
            static_cast<float>(std::min(getWidth(), getHeight())), static_cast<float>(std::min(getWidth(), getHeight())));
        const auto centre = square.getCentre();
        const auto radius = square.getWidth() * 0.5f;
        const auto ringRadius = radius - 6.0f;
        const auto rotary = getRotaryParameters();
        const auto proportion = static_cast<float>(valueToProportionOfLength(getValue()));
        const auto angle = rotary.startAngleRadians + proportion * (rotary.endAngleRadians - rotary.startAngleRadians);

        juce::Path track;
        track.addCentredArc(centre.x, centre.y, ringRadius, ringRadius, 0.0f, rotary.startAngleRadians, rotary.endAngleRadians, true);
        g.setColour(colour::knobRing);
        g.strokePath(track, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (proportion > 0.001f)
        {
            juce::Path value;
            value.addCentredArc(centre.x, centre.y, ringRadius, ringRadius, 0.0f, rotary.startAngleRadians, angle, true);
            g.setColour(colour::accent);
            g.strokePath(value, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        const auto bodyRadius = ringRadius - 14.0f;
        g.setColour(colour::knobBody.darker(0.6f).withAlpha(0.5f));
        g.fillEllipse(juce::Rectangle<float>(bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre(centre.translated(0.0f, 3.0f)));
        g.setGradientFill(juce::ColourGradient(colour::knobFace, centre.x, centre.y - bodyRadius,
                                               colour::knobBody, centre.x, centre.y + bodyRadius, false));
        g.fillEllipse(juce::Rectangle<float>(bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre(centre));
        g.setColour(colour::knobRing);
        g.drawEllipse(juce::Rectangle<float>(bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre(centre), 1.0f);
        const auto hubRadius = bodyRadius * 0.34f;
        g.setColour(colour::knobBody.darker(0.3f));
        g.fillEllipse(juce::Rectangle<float>(hubRadius * 2.0f, hubRadius * 2.0f).withCentre(centre));

        const auto pointerRadius = bodyRadius * 0.74f;
        const auto pointer = centre.getPointOnCircumference(pointerRadius, angle);
        g.setColour(colour::accent);
        g.fillEllipse(juce::Rectangle<float>(10.0f, 10.0f).withCentre(pointer));
    }
};

// The band cutoff: a bar that fills to its thumb.
class CutoffSlider final : public juce::Slider
{
public:
    CutoffSlider()
    {
        setSliderStyle(LinearHorizontal);
        setTextBoxStyle(NoTextBox, false, 0, 0);
    }

    // Drawn in grey while the band split is off, and still draggable.
    bool muted = false;

    void paint(juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat();
        const auto track = area.withSizeKeepingCentre(area.getWidth() - 10.0f, 8.0f);
        const auto proportion = static_cast<float>(valueToProportionOfLength(getValue()));
        const auto thumbX = track.getX() + proportion * track.getWidth();
        g.setColour(colour::chip);
        g.fillRoundedRectangle(track, 4.0f);
        g.setColour(muted ? colour::chipMuted : colour::accent);
        g.fillRoundedRectangle(track.withRight(thumbX), 4.0f);
        g.setColour(colour::marker);
        g.fillEllipse(juce::Rectangle<float>(16.0f, 16.0f).withCentre({ thumbX, track.getCentreY() }));
        g.setColour(colour::well);
        g.drawEllipse(juce::Rectangle<float>(16.0f, 16.0f).withCentre({ thumbX, track.getCentreY() }), 1.5f);
    }
};

// One of the sixteen curve buttons, its picture drawn from curveGain with the
// curve's own Slope and Shift, so the thumbnail is the curve as it will play.
class CurveCell final : public juce::Button
{
public:
    explicit CurveCell(int curve) : juce::Button("curve " + juce::String(curve + 1)), index(curve) {}

    const int index;
    CurveAdjust adjust;

    void paintButton(juce::Graphics& g, bool over, bool down) override
    {
        const auto area = getLocalBounds().toFloat().reduced(0.5f);
        const auto lit = getToggleState();
        auto fill = lit ? colour::accent : (over ? colour::chipHover : colour::chip);
        if (down) fill = fill.brighter(0.12f);
        g.setColour(fill);
        g.fillRoundedRectangle(area, 6.0f);

        const auto plot = area.reduced(9.0f, 8.0f);
        juce::Path line;
        constexpr int steps = 48;
        for (int i = 0; i <= steps; ++i)
        {
            const auto phase = static_cast<float>(i) / static_cast<float>(steps);
            const auto gain = curveGain(index, std::min(phase, 0.9999f), adjust);
            const juce::Point<float> point(plot.getX() + phase * plot.getWidth(), plot.getY() + (1.0f - gain) * plot.getHeight());
            if (i == 0) line.startNewSubPath(point);
            else line.lineTo(point);
        }
        g.setColour(lit ? colour::accentDark : colour::wave.withAlpha(0.8f));
        g.strokePath(line, juce::PathStrokeType(1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
};
}
