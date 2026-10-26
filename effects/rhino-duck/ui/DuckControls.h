#pragma once

#include "DuckTheme.h"
#include "../core/DuckCurves.h"

#include <juce_gui_basics/juce_gui_basics.h>

// The panel's own controls: a chip, the knobs, the cutoff slider, the switch,
// the preset box and a curve button. Each draws itself, so the panel needs no
// look-and-feel. Chips, cells and the switch are flat; only the knobs are
// given a body.
namespace rhino::duck::ui
{
// What a chip shows instead of its text: the menu's bars, or an arrow from
// the handoff's icon set (prev, next, rewind, fast forward).
enum class Glyph { text, bars, back, forward, rewind, fastForward };

// The glyph's strokes in a 64-unit square, as the icons draw them.
inline juce::Path glyphPath(Glyph glyph)
{
    juce::Path path;
    const auto chevron = [&path] (float tipX, float backX)
    {
        path.startNewSubPath(backX, 18.0f);
        path.lineTo(tipX, 32.0f);
        path.lineTo(backX, 46.0f);
    };
    switch (glyph)
    {
        case Glyph::back:        chevron(18.0f, 34.0f); break;
        case Glyph::forward:     chevron(46.0f, 30.0f); break;
        case Glyph::rewind:      chevron(14.0f, 29.0f); chevron(35.0f, 50.0f); break;
        case Glyph::fastForward: chevron(29.0f, 14.0f); chevron(50.0f, 35.0f); break;
        case Glyph::bars:
            for (const auto y : { 20.0f, 32.0f, 44.0f })
            {
                path.startNewSubPath(16.0f, y);
                path.lineTo(48.0f, y);
            }
            break;
        case Glyph::text: break;
    }
    return path;
}

inline void drawGlyph(juce::Graphics& g, Glyph glyph, juce::Rectangle<float> area)
{
    const auto side = std::min(area.getWidth(), area.getHeight());
    const auto box = area.withSizeKeepingCentre(side, side);
    auto path = glyphPath(glyph);
    path.applyTransform(juce::AffineTransform::scale(side / 64.0f).translated(box.getX(), box.getY()));
    g.strokePath(path, juce::PathStrokeType(std::max(1.6f, side * 0.07f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// A rounded button that is lit in the accent colour while on. Disabled, it
// keeps showing whether it is on, faintly.
class Chip final : public juce::Button
{
public:
    explicit Chip(const juce::String& text, Glyph shown = Glyph::text) : juce::Button(text), glyph(shown) { setButtonText(text); }

    Glyph glyph;

    void paintButton(juce::Graphics& g, bool over, bool down) override
    {
        const auto area = getLocalBounds().toFloat().reduced(0.5f);
        const auto lit = getToggleState();
        const auto live = isEnabled();
        auto fill = lit ? colour::accent : (over && live ? colour::chipHover : colour::chip);
        if (down) fill = fill.brighter(0.12f);
        if (!live) fill = fill.withMultipliedAlpha(lit ? 0.35f : 0.4f);
        g.setColour(fill);
        g.fillRoundedRectangle(area, area.getHeight() * 0.5f);
        // Disabled, the outline keeps it reading as a button.
        if (!live)
        {
            g.setColour(colour::chipMuted.withAlpha(0.3f));
            g.drawRoundedRectangle(area.reduced(0.5f), area.getHeight() * 0.5f, 1.0f);
        }
        auto ink = lit ? colour::accentDark : colour::chipText;
        if (!live) ink = lit ? colour::accentDark.withAlpha(0.7f) : colour::textMuted;
        g.setColour(ink);
        if (glyph != Glyph::text)
        {
            drawGlyph(g, glyph, area.reduced(area.getHeight() * 0.22f));
            return;
        }
        g.setFont(font(Face::header, 13.0f));
        g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred, false);
    }
};

// A knob: a ring that fills in the accent colour as it turns, round a dark
// machined body with an accent pointer. Mix and the strip's four share it.
class Knob final : public juce::Slider
{
public:
    Knob()
    {
        setSliderStyle(RotaryHorizontalVerticalDrag);
        setTextBoxStyle(NoTextBox, false, 0, 0);
        setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        setMouseDragSensitivity(220);
    }

    void paint(juce::Graphics& g) override
    {
        const auto side = static_cast<float>(std::min(getWidth(), getHeight()));
        const auto square = getLocalBounds().toFloat().withSizeKeepingCentre(side, side);
        const auto centre = square.getCentre();
        const auto radius = side * 0.5f;
        const auto ringWidth = std::max(3.0f, radius * 0.1f);
        const auto ringRadius = radius - ringWidth * 0.5f - 0.5f;
        const auto rotary = getRotaryParameters();
        const auto proportion = static_cast<float>(valueToProportionOfLength(getValue()));
        const auto angle = rotary.startAngleRadians + proportion * (rotary.endAngleRadians - rotary.startAngleRadians);
        const auto live = isEnabled();

        juce::Path track;
        track.addCentredArc(centre.x, centre.y, ringRadius, ringRadius, 0.0f, rotary.startAngleRadians, rotary.endAngleRadians, true);
        g.setColour(colour::knobRing);
        g.strokePath(track, juce::PathStrokeType(ringWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (proportion > 0.001f)
        {
            juce::Path value;
            value.addCentredArc(centre.x, centre.y, ringRadius, ringRadius, 0.0f, rotary.startAngleRadians, angle, true);
            g.setColour(live ? colour::accent : colour::chipMuted.withAlpha(0.6f));
            g.strokePath(value, juce::PathStrokeType(ringWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        const auto bodyRadius = ringRadius - ringWidth * 0.5f - std::max(3.0f, radius * 0.12f);
        const auto body = juce::Rectangle<float>(bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre(centre);
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.fillEllipse(body.translated(0.0f, std::max(1.5f, radius * 0.05f)).expanded(1.0f));
        g.setGradientFill(juce::ColourGradient(colour::knobFace, body.getX(), body.getY(),
                                               colour::knobBody, body.getRight(), body.getBottom(), false));
        g.fillEllipse(body);
        // The machined face: a lit rim at the top, a darker cap inside it.
        g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.18f), centre.x, body.getY(),
                                               juce::Colours::transparentWhite, centre.x, centre.y, false));
        g.drawEllipse(body.reduced(0.75f), 1.2f);
        const auto cap = body.reduced(bodyRadius * 0.16f);
        g.setGradientFill(juce::ColourGradient(colour::knobBody.brighter(0.18f), cap.getX(), cap.getY(),
                                               colour::knobBody.darker(0.35f), cap.getRight(), cap.getBottom(), false));
        g.fillEllipse(cap);

        const auto inner = centre.getPointOnCircumference(bodyRadius * 0.38f, angle);
        const auto outer = centre.getPointOnCircumference(bodyRadius * 0.82f, angle);
        g.setColour(live ? colour::accent : colour::chipMuted);
        g.drawLine(juce::Line<float>(inner, outer), std::max(2.0f, radius * 0.075f));
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
        const auto track = area.withSizeKeepingCentre(area.getWidth() - 12.0f, 6.0f);
        const auto proportion = static_cast<float>(valueToProportionOfLength(getValue()));
        const auto thumbX = track.getX() + proportion * track.getWidth();
        g.setColour(colour::well);
        g.fillRoundedRectangle(track, 3.0f);
        g.setColour(muted ? colour::chipMuted.withAlpha(0.5f) : colour::accent);
        g.fillRoundedRectangle(track.withRight(thumbX), 3.0f);
        const auto thumb = juce::Rectangle<float>(14.0f, 14.0f).withCentre({ thumbX, track.getCentreY() });
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.fillEllipse(thumb.translated(0.0f, 1.0f));
        g.setColour(muted ? colour::chipMuted : colour::marker);
        g.fillEllipse(thumb);
        g.setColour(muted ? colour::chip : colour::accent);
        g.fillEllipse(thumb.reduced(4.0f));
    }
};

// A pill switch and its caption, both of them the click target.
class Switch final : public juce::Button
{
public:
    explicit Switch(const juce::String& caption) : juce::Button(caption) { setButtonText(caption); }

    void paintButton(juce::Graphics& g, bool over, bool) override
    {
        auto area = getLocalBounds().toFloat();
        const auto on = getToggleState();
        const auto pill = area.removeFromLeft(area.getHeight() * 1.9f).reduced(1.0f, 2.0f);
        g.setColour(on ? colour::accent : (over ? colour::chipHover : colour::chip));
        g.fillRoundedRectangle(pill, pill.getHeight() * 0.5f);
        const auto thumbSize = pill.getHeight() - 4.0f;
        const auto thumbX = on ? pill.getRight() - 2.0f - thumbSize : pill.getX() + 2.0f;
        g.setColour(juce::Colours::black.withAlpha(0.4f));
        g.fillEllipse(thumbX, pill.getY() + 3.0f, thumbSize, thumbSize);
        g.setColour(on ? colour::accentDark.brighter(0.9f) : colour::marker);
        g.fillEllipse(thumbX, pill.getY() + 2.0f, thumbSize, thumbSize);
        area.removeFromLeft(10.0f);
        g.setColour(on ? colour::text : colour::textDim);
        g.setFont(font(Face::label, 15.0f));
        g.drawText(getButtonText(), area, juce::Justification::centredLeft, false);
    }
};

// The preset bar's box: the chosen curve, and a chevron that says a list
// opens from it.
class PresetBox final : public juce::Button
{
public:
    PresetBox() : juce::Button("preset") {}

    void paintButton(juce::Graphics& g, bool over, bool down) override
    {
        const auto area = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(down ? colour::chip : colour::well);
        g.fillRoundedRectangle(area, 5.0f);
        g.setColour(over ? colour::chipMuted : colour::divider);
        g.drawRoundedRectangle(area, 5.0f, 1.0f);
        auto inside = getLocalBounds().reduced(12, 0);
        auto chevron = inside.removeFromRight(16).toFloat();
        g.setColour(colour::chipText);
        juce::Path arrow;
        const auto c = chevron.getCentre();
        arrow.startNewSubPath(c.x - 5.0f, c.y - 2.5f);
        arrow.lineTo(c.x, c.y + 2.5f);
        arrow.lineTo(c.x + 5.0f, c.y - 2.5f);
        g.strokePath(arrow, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(colour::text);
        g.setFont(font(Face::label, 16.0f));
        g.drawText(getButtonText(), inside, juce::Justification::centredLeft, true);
    }
};

// One of the sixteen curve buttons, its picture drawn from curveGain with the
// curve's own adjustments, so the thumbnail is the curve as it will play.
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

        const auto plot = area.reduced(10.0f, 7.0f);
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
