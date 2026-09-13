#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

namespace theta::forge::ui
{
inline constexpr int parameterCount = 35;

inline juce::Colour accentForParameter(int index)
{
    if (index < 8) return juce::Colour(0xffff7a59);
    if (index < 10) return juce::Colour(0xff4de0c1);
    if (index < 14) return juce::Colour(0xffffc66d);
    if (index < 19) return juce::Colour(0xffc996ff);
    if (index < 25) return juce::Colour(0xff65bfff);
    if (index < 31) return juce::Colour(0xfff08bc2);
    return juce::Colour(0xff8fd879);
}

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
    juce::ColourGradient background(juce::Colour(0xff10151e), 0.0f, 0.0f,
                                    juce::Colour(0xff202235), bounds.getRight(), bounds.getBottom(), false);
    g.setGradientFill(background);
    g.fillRect(bounds);
    const auto frame = bounds.reduced(16.0f);
    g.setColour(juce::Colour(0xff33424d));
    g.drawRoundedRectangle(frame, 9.0f, 1.0f);
    g.setColour(juce::Colour(0xffff7a59));
    g.fillRoundedRectangle(frame.getX(), frame.getY(), 6.0f, frame.getHeight(), 3.0f);

    g.setColour(juce::Colour(0xfff7f4e9));
    g.setFont(juce::FontOptions(36.0f, juce::Font::bold));
    g.drawText("FORGE", 36, 26, 220, 42, juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xffffc66d));
    g.setFont(juce::FontOptions(11.0f));
    g.drawText("SHAPE SOUND. LEAVE A MARK.", 39, 68, 260, 18, juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xff94a9b6));
    g.drawText("TWO OSCILLATOR INSTRUMENT", componentBounds.getWidth() - 300, 43, 260, 18,
               juce::Justification::centredRight);

    const auto waveArea = juce::Rectangle<float>(36.0f, 104.0f, componentBounds.getWidth() - 72.0f, 105.0f);
    g.setColour(juce::Colour(0xff151f2a));
    g.fillRoundedRectangle(waveArea, 8.0f);
    g.setColour(juce::Colour(0xff2c3a45));
    for (int line = 1; line < 4; ++line)
        g.drawHorizontalLine(static_cast<int>(waveArea.getY() + waveArea.getHeight() * line / 4.0f),
                             waveArea.getX(), waveArea.getRight());
    drawWaveform(g, waveArea.reduced(12.0f, 16.0f), value(0), juce::Colour(0xffff7a59));
    drawWaveform(g, waveArea.reduced(12.0f, 28.0f), value(1), juce::Colour(0xff4de0c1));

    constexpr auto lowerY = 226.0f;
    g.setColour(juce::Colour(0xff18212c));
    g.fillRoundedRectangle(36.0f, lowerY, componentBounds.getWidth() - 72.0f,
                           componentBounds.getHeight() - lowerY - 32.0f, 8.0f);
    g.setColour(juce::Colour(0xff94a9b6));
    g.setFont(juce::FontOptions(10.0f));
    g.drawText("OSCILLATOR FORGE", 52, 238, 220, 16, juce::Justification::centredLeft);
    g.drawText("OSCILLATORS", 52, 238, 180, 16, juce::Justification::centredLeft);
}

inline bool isParameterVisible(int index, int page)
{
    return page == 0 ? (index < 19 || index >= 31) : (index >= 19 && index < 31);
}

inline juce::Rectangle<int> controlCell(juce::Rectangle<int> bounds, int index, int page)
{
    const auto left = 44;
    const auto available = bounds.getWidth() - 88;
    const auto controlTop = 262;
    const auto controlHeight = juce::jmax(300, bounds.getHeight() - controlTop - 40);
    if (page == 1)
    {
        const auto local = index - 19;
        const auto cellWidth = available / 6;
        const auto rowHeight = controlHeight / 2;
        return {left + (local % 6) * cellWidth, controlTop + (local / 6) * rowHeight, cellWidth, rowHeight};
    }
    const auto rowHeight = controlHeight / 3;
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
    const auto local = index < 19 ? index - 14 : index - 26;
    const auto cellWidth = available / 9;
    return {left + local * cellWidth, controlTop + rowHeight * 2, cellWidth, rowHeight};
}
}
