#pragma once

#include "ForgeLayout.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

// Drawing primitives and the knob look. Layout lives next door in
// ForgeLayout.h; nothing here decides where anything goes.
namespace theta::forge::ui
{
inline const auto electricBlue = juce::Colour(0xff45a8ff);
inline const auto signalViolet = juce::Colour(0xff9a6cff);
inline const auto panel = juce::Colour(0xff111522);
inline const auto panelRaised = juce::Colour(0xff191d2d);
inline const auto line = juce::Colour(0xff34394e);
inline const auto text = juce::Colour(0xffe8eaff);
inline const auto mutedText = juce::Colour(0xff8f95ad);

inline juce::Colour accentFor(const Module& module)
{
    return module.violet ? signalViolet : electricBlue;
}

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    // Steppers are bar-style sliders, so JUCE routes them here. They are drawn
    // as a plain numeric field rather than a filled bar: for a tuning value,
    // the number is the information and a fill proportion is noise.
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float, float, float, juce::Slider::SliderStyle, juce::Slider& slider) override
    {
        const auto area = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(1.0f);
        const auto accent = slider.findColour(juce::Slider::rotarySliderFillColourId);
        const auto enabled = slider.isEnabled();
        const auto active = slider.getValue() != slider.getDoubleClickReturnValue();

        g.setColour(juce::Colour(0xff0b0e18).withAlpha(enabled ? 1.0f : 0.5f));
        g.fillRoundedRectangle(area, 3.0f);
        g.setColour((active ? accent : line).withAlpha(enabled ? 1.0f : 0.35f));
        g.drawRoundedRectangle(area, 3.0f, 1.0f);

        g.setColour((active ? accent : text).withAlpha(enabled ? 1.0f : 0.35f));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(slider.getTextFromValue(slider.getValue()), area, juce::Justification::centred);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float position, float startAngle, float endAngle, juce::Slider& slider) override
    {
        auto area = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                            static_cast<float>(width), static_cast<float>(height)).reduced(4.0f);
        const auto diameter = juce::jmin(area.getWidth(), area.getHeight());
        auto knob = juce::Rectangle<float>(diameter, diameter).withCentre(area.getCentre());
        const auto centre = knob.getCentre();
        const auto radius = diameter * 0.5f;
        const auto angle = juce::jmap(position, startAngle, endAngle);
        const auto accent = slider.findColour(juce::Slider::rotarySliderFillColourId);
        const auto enabled = slider.isEnabled();
        const auto polar = [centre] (float distance, float polarAngle)
        {
            return centre + juce::Point<float>(std::sin(polarAngle) * distance,
                                                -std::cos(polarAngle) * distance);
        };

        juce::Path ticks;
        for (int i = 0; i <= 18; ++i)
        {
            const auto tickAngle = juce::jmap(static_cast<float>(i) / 18.0f, startAngle, endAngle);
            const auto inner = polar(radius * 0.82f, tickAngle);
            const auto outer = polar(radius * (i % 3 == 0 ? 0.98f : 0.93f), tickAngle);
            ticks.startNewSubPath(inner); ticks.lineTo(outer);
        }
        g.setColour(line.withAlpha(enabled ? 1.0f : 0.4f));
        g.strokePath(ticks, juce::PathStrokeType(1.0f));

        auto body = knob.reduced(radius * 0.19f);
        juce::ColourGradient metal(juce::Colour(0xff34394b), body.getX(), body.getY(),
                                   juce::Colour(0xff111522), body.getRight(), body.getBottom(), false);
        g.setGradientFill(metal);
        g.fillEllipse(body);
        g.setColour(juce::Colour(0xff050711));
        g.drawEllipse(body, 2.0f);

        juce::Path active;
        active.addCentredArc(centre.x, centre.y, radius * 0.71f, radius * 0.71f, 0.0f,
                             startAngle, angle, true);
        g.setColour(accent.withAlpha(enabled ? 0.18f : 0.06f));
        g.strokePath(active, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
        g.setColour(accent.withAlpha(enabled ? 1.0f : 0.3f));
        g.strokePath(active, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

        juce::Path pointer;
        pointer.startNewSubPath(centre);
        pointer.lineTo(polar(radius * 0.52f, angle));
        g.setColour(text.withAlpha(enabled ? 1.0f : 0.35f));
        g.strokePath(pointer, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        g.setColour(accent.withAlpha(enabled ? 1.0f : 0.3f));
        g.fillEllipse(juce::Rectangle<float>(5.0f, 5.0f).withCentre(centre));
    }
};

// The dot in a module header that switches the module on. Serum marks an
// enabled module the same way, and it reads at a glance across eight panels in
// a way a labelled checkbox does not.
class EnableLed final : public juce::Button
{
public:
    EnableLed() : juce::Button("enable") { setClickingTogglesState(true); }

    juce::Colour accent = electricBlue;

    void paintButton(juce::Graphics& g, bool highlighted, bool) override
    {
        const auto area = getLocalBounds().toFloat().reduced(getWidth() * 0.3f);
        const auto on = getToggleState();
        if (on)
        {
            g.setColour(accent.withAlpha(0.22f));
            g.fillEllipse(area.expanded(3.5f));
        }
        g.setColour(on ? accent : juce::Colour(0xff232838));
        g.fillEllipse(area);
        g.setColour(on ? juce::Colours::white.withAlpha(0.5f)
                       : mutedText.withAlpha(highlighted ? 0.85f : 0.45f));
        g.drawEllipse(area, 1.0f);
    }
};

inline float waveform(float phase, float position)
{
    const auto sine = std::sin(phase * juce::MathConstants<float>::twoPi);
    const auto triangle = 1.0f - 4.0f * std::abs(phase - 0.5f);
    const auto saw = phase * 2.0f - 1.0f;
    const auto square = phase < 0.5f ? 1.0f : -1.0f;
    const float frames[] {sine, triangle, saw, square};
    const auto scaled = juce::jlimit(0.0f, 1.0f, position) * 3.0f;
    const auto index = std::min(2, static_cast<int>(scaled));
    return juce::jmap(scaled - static_cast<float>(index), frames[index], frames[index + 1]);
}

inline void strokeGlow(juce::Graphics& g, const juce::Path& path, juce::Colour colour, float alpha)
{
    g.setColour(colour.withAlpha(0.16f * alpha));
    g.strokePath(path, juce::PathStrokeType(7.0f));
    g.setColour(colour.withAlpha(alpha));
    g.strokePath(path, juce::PathStrokeType(1.8f));
}

inline void drawDisplayWell(juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto box = area.toFloat();
    g.setColour(juce::Colour(0xff0b0e18));
    g.fillRoundedRectangle(box, 3.0f);
    g.setColour(line.withAlpha(0.6f));
    g.drawRoundedRectangle(box, 3.0f, 1.0f);
    g.setColour(line.withAlpha(0.45f));
    g.drawHorizontalLine(area.getCentreY(), box.getX() + 4.0f, box.getRight() - 4.0f);
}

inline void drawWaveform(juce::Graphics& g, juce::Rectangle<int> area, float position,
                         juce::Colour colour, float alpha)
{
    const auto box = area.toFloat().reduced(6.0f, 8.0f);
    juce::Path path;
    constexpr int points = 180;
    for (int i = 0; i <= points; ++i)
    {
        const auto phase = static_cast<float>(i) / static_cast<float>(points);
        const auto x = box.getX() + phase * box.getWidth();
        const auto y = box.getCentreY() - waveform(phase, position) * box.getHeight() * 0.44f;
        if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
    }
    strokeGlow(g, path, colour, alpha);
}

// A static read of the current ADSR. The live stage indicator arrives with the
// ENV 1 milestone; the shape itself is already worth seeing while dialling.
inline void drawEnvelope(juce::Graphics& g, juce::Rectangle<int> area, float attack, float decay,
                         float sustain, float release, juce::Colour colour, float alpha)
{
    const auto box = area.toFloat().reduced(8.0f, 9.0f);
    const auto span = attack + decay + release + 0.001f;
    const auto sustainWidth = box.getWidth() * 0.22f;
    const auto scale = (box.getWidth() - sustainWidth) / span;
    const auto floorY = box.getBottom();
    const auto peakY = box.getY();
    const auto sustainY = juce::jmap(juce::jlimit(0.0f, 1.0f, sustain), floorY, peakY);

    juce::Path path;
    path.startNewSubPath(box.getX(), floorY);
    const auto attackX = box.getX() + attack * scale;
    path.lineTo(attackX, peakY);
    const auto decayX = attackX + decay * scale;
    path.quadraticTo(attackX + (decayX - attackX) * 0.4f, sustainY, decayX, sustainY);
    const auto sustainX = decayX + sustainWidth;
    path.lineTo(sustainX, sustainY);
    path.quadraticTo(sustainX + release * scale * 0.4f, floorY, sustainX + release * scale, floorY);
    strokeGlow(g, path, colour, alpha);

    g.setColour(colour.withAlpha(0.25f * alpha));
    g.drawVerticalLine(juce::roundToInt(sustainX), sustainY, floorY);
}

// A static read of the LFO shape. Shape selection and the running phase dot
// arrive with the LFO 1 milestone.
inline void drawLfo(juce::Graphics& g, juce::Rectangle<int> area, float cycles,
                    juce::Colour colour, float alpha)
{
    const auto box = area.toFloat().reduced(6.0f, 8.0f);
    juce::Path path;
    constexpr int points = 220;
    for (int i = 0; i <= points; ++i)
    {
        const auto phase = static_cast<float>(i) / static_cast<float>(points);
        const auto x = box.getX() + phase * box.getWidth();
        const auto y = box.getCentreY()
            - std::sin(phase * cycles * juce::MathConstants<float>::twoPi) * box.getHeight() * 0.42f;
        if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
    }
    strokeGlow(g, path, colour, alpha);
}

// The pedal shell: a raised body, an accent cap, and a header strip reserved
// for the title. Knob labels are laid out below that strip, which is why they
// no longer collide with it.
inline void drawModuleShell(juce::Graphics& g, juce::Rectangle<int> area, const Module& module, bool on)
{
    const auto box = area.toFloat();
    const auto accent = accentFor(module);
    const auto alpha = on ? 1.0f : 0.45f;

    g.setColour(panelRaised.withAlpha(on ? 1.0f : 0.55f));
    g.fillRoundedRectangle(box, 5.0f);
    g.setColour(line.withAlpha(alpha));
    g.drawRoundedRectangle(box, 5.0f, 1.0f);
    g.setColour(accent.withAlpha(alpha));
    g.fillRoundedRectangle(box.withHeight(3.0f).reduced(1.0f, 0.0f), 2.0f);

    auto header = area.withHeight(headerHeight).reduced(10, 0);
    // The enable LED sits at the far left of the header; leave room for it.
    if (module.enableId != nullptr) header.removeFromLeft(headerHeight);
    g.setColour(text.withAlpha(alpha));
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText(module.title, header, juce::Justification::centredLeft);
    if (*module.detail != 0)
    {
        g.setColour(mutedText.withAlpha(alpha));
        g.setFont(juce::FontOptions(9.0f));
        g.drawText(module.detail, header, juce::Justification::centredRight);
    }
}

inline void drawBackdrop(juce::Graphics& g, juce::Rectangle<int> componentBounds)
{
    const auto bounds = componentBounds.toFloat();
    juce::ColourGradient background(juce::Colour(0xff090b13), 0.0f, 0.0f,
                                    juce::Colour(0xff17182a), bounds.getRight(), bounds.getBottom(), false);
    g.setGradientFill(background);
    g.fillRect(bounds);
    const auto frame = bounds.reduced(14.0f);
    g.setColour(line);
    g.drawRoundedRectangle(frame, 3.0f, 1.0f);
    g.setColour(electricBlue);
    g.fillRect(frame.getX(), frame.getY(), 4.0f, frame.getHeight());
    g.setColour(signalViolet.withAlpha(0.7f));
    g.fillRect(frame.getRight() - 2.0f, frame.getY(), 2.0f, frame.getHeight());

    g.setColour(text);
    g.setFont(juce::FontOptions(30.0f, juce::Font::bold));
    g.drawText("FORGE", 30, 20, 220, 36, juce::Justification::centredLeft);
    g.setColour(signalViolet);
    g.setFont(juce::FontOptions(10.0f));
    g.drawText("SYNTHETIC SIGNAL FORGE // UNIT 01", 32, 54, 280, 14, juce::Justification::centredLeft);
}
}
