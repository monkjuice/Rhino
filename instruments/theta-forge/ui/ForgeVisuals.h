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
    const auto triangle = 1.0f - 4.0f * std::abs(phase - 0.5f);
    const auto saw = phase * 2.0f - 1.0f;
    const auto square = phase < 0.5f ? 1.0f : -1.0f;
    const float frames[] {sine, triangle, saw, square};
    const auto scaled = juce::jlimit(0.0f, 1.0f, position) * 3.0f;
    const auto index = std::min(2, static_cast<int>(scaled));
    return juce::jmap(scaled - static_cast<float>(index), frames[index], frames[index + 1]);
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

inline juce::Rectangle<int> contentBounds(juce::Rectangle<int> bounds)
{
    return bounds.reduced(36).withTrimmedTop(68);
}

inline void drawPanel(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title,
                      const juce::String& detail, juce::Colour accent)
{
    const auto box = area.toFloat();
    g.setColour(panelRaised);
    g.fillRoundedRectangle(box, 4.0f);
    g.setColour(line);
    g.drawRoundedRectangle(box, 4.0f, 1.0f);
    g.setColour(accent);
    g.fillRoundedRectangle(box.withHeight(3.0f), 2.0f);
    g.setColour(text);
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText(title, area.reduced(12, 7).withHeight(16), juce::Justification::centredLeft);
    g.setColour(mutedText);
    g.setFont(juce::FontOptions(9.0f));
    g.drawText(detail, area.reduced(12, 7).withHeight(16), juce::Justification::centredRight);
}

inline juce::Rectangle<int> cell(juce::Rectangle<int> area, int index, int count)
{
    const auto width = area.getWidth() / std::max(1, count);
    return {area.getX() + index * width, area.getY(), width, area.getHeight()};
}

struct SynthLayout
{
    juce::Rectangle<int> oscA, oscB, sourceA, sourceB, mix, filter, ampEnvelope, filterEnvelope, macros;
};

inline SynthLayout synthLayout(juce::Rectangle<int> bounds)
{
    auto content = contentBounds(bounds);
    const auto visualHeight = juce::jlimit(86, 112, bounds.getHeight() / 6);
    const auto sourceHeight = juce::jlimit(84, 106, bounds.getHeight() / 7);
    const auto macroHeight = juce::jlimit(74, 92, bounds.getHeight() / 8);
    auto visual = content.removeFromTop(visualHeight);
    content.removeFromTop(10);
    auto sources = content.removeFromTop(sourceHeight);
    content.removeFromTop(10);
    auto macros = content.removeFromBottom(macroHeight);
    content.removeFromBottom(10);
    const auto filterWidth = content.getWidth() * 3 / 11;
    auto filter = content.removeFromLeft(filterWidth);
    content.removeFromLeft(10);
    auto amp = content.removeFromLeft((content.getWidth() - 10) / 2);
    content.removeFromLeft(10);
    const auto sourceAWidth = sources.getWidth() * 3 / 8;
    auto sourceA = sources.removeFromLeft(sourceAWidth);
    sources.removeFromLeft(8);
    const auto sourceBWidth = sources.getWidth() * 3 / 5;
    auto sourceB = sources.removeFromLeft(sourceBWidth);
    sources.removeFromLeft(8);
    return {visual.removeFromLeft(visual.getWidth() / 2 - 5), visual,
            sourceA, sourceB, sources, filter, amp, content, macros};
}

template <typename NormalisedValue>
void paint(juce::Graphics& g, juce::Rectangle<int> componentBounds, int page, NormalisedValue value)
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

    if (page == 0)
    {
        const auto layout = synthLayout(componentBounds);
        drawPanel(g, layout.oscA, "OSC A", "BASIC MORPH", electricBlue);
        drawPanel(g, layout.oscB, "OSC B", "BASIC MORPH", signalViolet);
        for (const auto& area : {layout.oscA, layout.oscB})
        {
            g.setColour(line.withAlpha(0.75f));
            g.drawHorizontalLine(area.getCentreY() + 8, area.getX() + 12, area.getRight() - 12);
        }
        drawWaveform(g, layout.oscA.toFloat().reduced(14.0f, 29.0f), value(0), electricBlue);
        drawWaveform(g, layout.oscB.toFloat().reduced(14.0f, 29.0f), value(1), signalViolet);
        drawPanel(g, layout.sourceA, "OSC A", "POSITION / STACK", electricBlue);
        drawPanel(g, layout.sourceB, "OSC B", "POSITION / MIX / TUNE", signalViolet);
        drawPanel(g, layout.mix, "SOURCES", "SUB / NOISE", electricBlue);
        drawPanel(g, layout.filter, "FILTER", "LOW-PASS VOICE FILTER", signalViolet);
        drawPanel(g, layout.ampEnvelope, "AMP ENVELOPE", "VOLUME SHAPE", electricBlue);
        drawPanel(g, layout.filterEnvelope, "FILTER ENVELOPE", "CUTOFF SHAPE", signalViolet);
        drawPanel(g, layout.macros, "PERFORMANCE MACROS",
                  value(35) + value(36) + value(37) + value(38) > 0.0f ? "MACRO ACTIVE" : "DIRECT", signalViolet);
        return;
    }

    auto content = contentBounds(componentBounds);
    const auto macroHeight = juce::jlimit(74, 92, componentBounds.getHeight() / 8);
    auto macros = content.removeFromBottom(macroHeight);
    content.removeFromBottom(10);
    const auto topHeight = content.getHeight() / 2 - 5;
    auto motion = content.removeFromTop(topHeight);
    content.removeFromTop(10);
    const auto lfo = motion.removeFromLeft(motion.getWidth() * 2 / 3 - 5);
    motion.removeFromLeft(10);
    drawPanel(g, lfo, "LFO 1", "RATE / FILTER / POSITION / PITCH", signalViolet);
    drawPanel(g, motion, "TONE", "DRIVE / OUTPUT", electricBlue);
    drawPanel(g, content, "FX + VOICE", "CHORUS / DELAY / VOICING", signalViolet);
    drawPanel(g, macros, "PERFORMANCE MACROS", "SHAPE / MOTION / WEIGHT / SPACE", signalViolet);
}

inline bool isParameterVisible(int index, int page)
{
    return page == 0 ? (index < 19 || index >= 35) : index >= 19;
}

inline juce::Rectangle<int> controlCell(juce::Rectangle<int> bounds, int index, int page)
{
    if (page == 0)
    {
        const auto layout = synthLayout(bounds);
        if (index == 0) return cell(layout.sourceA, 0, 3);
        if (index == 6) return cell(layout.sourceA, 1, 3);
        if (index == 7) return cell(layout.sourceA, 2, 3);
        if (index >= 1 && index <= 3) return cell(layout.sourceB, index - 1, 3);
        if (index >= 4 && index <= 5) return cell(layout.mix, index - 4, 2);
        if (index == 8) return cell(layout.filter, 0, 3);
        if (index == 9) return cell(layout.filter, 1, 3);
        if (index == 14) return cell(layout.filter, 2, 3);
        if (index >= 10 && index <= 13) return cell(layout.ampEnvelope, index - 10, 4);
        if (index >= 15 && index <= 18) return cell(layout.filterEnvelope, index - 15, 4);
        return cell(layout.macros, index - 35, 4);
    }

    auto content = contentBounds(bounds);
    const auto macroHeight = juce::jlimit(74, 92, bounds.getHeight() / 8);
    auto macros = content.removeFromBottom(macroHeight);
    content.removeFromBottom(10);
    const auto topHeight = content.getHeight() / 2 - 5;
    auto motion = content.removeFromTop(topHeight);
    content.removeFromTop(10);
    const auto lfo = motion.removeFromLeft(motion.getWidth() * 2 / 3 - 5);
    motion.removeFromLeft(10);
    if (index >= 19 && index <= 20) return cell(lfo, index - 19, 4);
    if (index == 23) return cell(lfo, 2, 4);
    if (index == 24) return cell(lfo, 3, 4);
    if (index == 21) return cell(motion, 0, 2);
    if (index == 22) return cell(motion, 1, 2);
    if (index >= 25 && index <= 30) return cell(content, index - 25, 10);
    if (index >= 31 && index <= 34) return cell(content, index - 21, 10);
    return cell(macros, index - 35, 4);
}
}
