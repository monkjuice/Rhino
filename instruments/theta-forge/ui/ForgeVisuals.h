#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

namespace theta::forge::ui
{
inline constexpr int parameterCount = 39;
inline const auto electricBlue = juce::Colour(0xff45a8ff);
inline const auto signalViolet = juce::Colour(0xff9a6cff);
inline const auto panel = juce::Colour(0xff111522);
inline const auto panelRaised = juce::Colour(0xff191d2d);
inline const auto line = juce::Colour(0xff34394e);
inline const auto text = juce::Colour(0xffe8eaff);
inline const auto mutedText = juce::Colour(0xff8f95ad);

inline juce::Colour accentForParameter(int index)
{
    return (index >= 35 || (index >= 14 && index < 25) || index == 27 || index == 30)
        ? signalViolet : electricBlue;
}

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float position, float startAngle, float endAngle, juce::Slider& slider) override
    {
        auto area = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                            static_cast<float>(width), static_cast<float>(height)).reduced(6.0f);
        const auto diameter = juce::jmin(area.getWidth(), area.getHeight());
        auto knob = juce::Rectangle<float>(diameter, diameter).withCentre(area.getCentre());
        const auto centre = knob.getCentre();
        const auto radius = diameter * 0.5f;
        const auto angle = juce::jmap(position, startAngle, endAngle);
        const auto accent = slider.findColour(juce::Slider::rotarySliderFillColourId);
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
        g.setColour(line);
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
        g.setColour(accent.withAlpha(0.18f));
        g.strokePath(active, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
        g.setColour(accent);
        g.strokePath(active, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

        juce::Path pointer;
        pointer.startNewSubPath(centre);
        pointer.lineTo(polar(radius * 0.52f, angle));
        g.setColour(text);
        g.strokePath(pointer, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        g.setColour(accent);
        g.fillEllipse(juce::Rectangle<float>(5.0f, 5.0f).withCentre(centre));
    }
};

inline float waveform(float phase, float position)
{
    const auto sine = std::sin(phase * juce::MathConstants<float>::twoPi);
    const auto saw = phase * 2.0f - 1.0f;
    const auto square = phase < 0.5f ? 1.0f : -1.0f;
    const auto first = juce::jmap(juce::jlimit(0.0f, 1.0f, position * 2.0f), sine, saw);
    return juce::jmap(juce::jlimit(0.0f, 1.0f, position * 2.0f - 1.0f), first, square);
}

inline void drawWaveform(juce::Graphics& g, juce::Rectangle<float> area, float position, juce::Colour colour)
{
    juce::Path path;
    constexpr int points = 160;
    for (int i = 0; i <= points; ++i)
    {
        const auto phase = static_cast<float>(i) / static_cast<float>(points);
        const auto x = area.getX() + phase * area.getWidth();
        const auto y = area.getCentreY() - waveform(phase, position) * area.getHeight() * 0.38f;
        if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
    }
    g.setColour(colour.withAlpha(0.16f));
    g.strokePath(path, juce::PathStrokeType(7.0f));
    g.setColour(colour);
    g.strokePath(path, juce::PathStrokeType(1.8f));
}

template <typename NormalisedValue>
void paint(juce::Graphics& g, juce::Rectangle<int> componentBounds, NormalisedValue value)
{
    const auto bounds = componentBounds.toFloat();
    juce::ColourGradient background(juce::Colour(0xff090b13), 0.0f, 0.0f,
                                    juce::Colour(0xff17182a), bounds.getRight(), bounds.getBottom(), false);
    g.setGradientFill(background);
    g.fillRect(bounds);
    const auto frame = bounds.reduced(16.0f);
    g.setColour(line);
    g.drawRoundedRectangle(frame, 3.0f, 1.0f);
    g.setColour(electricBlue);
    g.fillRect(frame.getX(), frame.getY(), 4.0f, frame.getHeight());
    g.setColour(signalViolet.withAlpha(0.7f));
    g.fillRect(frame.getRight() - 2.0f, frame.getY(), 2.0f, frame.getHeight());

    g.setColour(text);
    g.setFont(juce::FontOptions(36.0f, juce::Font::bold));
    g.drawText("FORGE", 36, 26, 220, 42, juce::Justification::centredLeft);
    g.setColour(signalViolet);
    g.setFont(juce::FontOptions(11.0f));
    g.drawText("SYNTHETIC SIGNAL FORGE // UNIT 01", 39, 68, 280, 18, juce::Justification::centredLeft);

    const auto waveArea = juce::Rectangle<float>(36.0f, 104.0f, componentBounds.getWidth() - 72.0f, 105.0f);
    g.setColour(panel);
    g.fillRoundedRectangle(waveArea, 3.0f);
    g.setColour(line);
    for (int gridLine = 1; gridLine < 4; ++gridLine)
        g.drawHorizontalLine(static_cast<int>(waveArea.getY() + waveArea.getHeight() * gridLine / 4.0f),
                             waveArea.getX(), waveArea.getRight());
    drawWaveform(g, waveArea.reduced(12.0f, 16.0f), value(0), electricBlue);
    drawWaveform(g, waveArea.reduced(12.0f, 28.0f), value(1), signalViolet);

    constexpr auto lowerY = 226.0f;
    g.setColour(panelRaised);
    g.fillRoundedRectangle(36.0f, lowerY, componentBounds.getWidth() - 72.0f,
                           componentBounds.getHeight() - lowerY - 32.0f, 3.0f);
    g.setColour(mutedText);
    g.setFont(juce::FontOptions(10.0f));
    g.drawText("CONTROL MATRIX // " + juce::String(value(35) + value(36) + value(37) + value(38) > 0.0f ? "MACRO ACTIVE" : "DIRECT"),
               52, 238, 280, 16, juce::Justification::centredLeft);
}

inline bool isParameterVisible(int index, int page)
{
    return page == 0 ? (index < 19 || index >= 31)
                     : ((index >= 19 && index < 31) || index >= 35);
}

inline juce::Rectangle<int> controlCell(juce::Rectangle<int> bounds, int index, int page)
{
    const auto left = 44;
    const auto available = bounds.getWidth() - 88;
    const auto controlTop = 262;
    const auto controlHeight = juce::jmax(300, bounds.getHeight() - controlTop - 40);
    if (page == 1)
    {
        if (index >= 35)
        {
            const auto cellWidth = available / 4;
            return {left + (index - 35) * cellWidth, controlTop + controlHeight * 2 / 3, cellWidth, controlHeight / 3};
        }
        const auto local = index - 19;
        const auto cellWidth = available / 6;
        const auto rowHeight = controlHeight / 3;
        return {left + (local % 6) * cellWidth, controlTop + (local / 6) * rowHeight, cellWidth, rowHeight};
    }
    const auto rowHeight = controlHeight / 4;
    if (index < 8)
    {
        const auto cellWidth = available / 8;
        return {left + index * cellWidth, controlTop, cellWidth, rowHeight};
    }
    if (index < 14)
    {
        const auto local = index - 8;
        const auto cellWidth = available / 6;
        return {left + local * cellWidth, controlTop + rowHeight, cellWidth, rowHeight};
    }
    if (index >= 35)
    {
        const auto cellWidth = available / 4;
        return {left + (index - 35) * cellWidth, controlTop + rowHeight * 3, cellWidth, rowHeight};
    }
    const auto local = index < 19 ? index - 14 : index - 26;
    const auto cellWidth = available / 9;
    return {left + local * cellWidth, controlTop + rowHeight * 2, cellWidth, rowHeight};
}
}
