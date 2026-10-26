#include "DuckScopeView.h"
#include "../ui/DuckTheme.h"

namespace rhino::duck::ui
{
namespace
{
constexpr float grabReach = 7.0f;
constexpr float resetSize = 22.0f;

float valueOf(const Processor& processor, const char* id)
{
    const auto* value = processor.state.getRawParameterValue(id);
    return value != nullptr ? value->load(std::memory_order_relaxed) : 0.0f;
}
}

ScopeView::ScopeView(Processor& owner) : processor(owner)
{
    endParameter = processor.state.getParameter(id::end);
    setOpaque(false);
}

int ScopeView::currentCurve() const
{
    return clampCurve(juce::roundToInt(valueOf(processor, id::curve)));
}

bool ScopeView::showsEndMarker() const
{
    return juce::roundToInt(valueOf(processor, id::trigger)) != 0 && valueOf(processor, id::loop) < 0.5f;
}

juce::Rectangle<float> ScopeView::plotArea() const
{
    return getLocalBounds().toFloat().reduced(1.0f, 12.0f);
}

juce::Rectangle<float> ScopeView::resetButtonArea() const
{
    const auto area = getLocalBounds().toFloat();
    return { area.getRight() - resetSize - 8.0f, area.getBottom() - resetSize - 8.0f, resetSize, resetSize };
}

float ScopeView::endMarkerX() const
{
    const auto plot = plotArea();
    return plot.getX() + std::clamp(valueOf(processor, id::end), 0.0f, 1.0f) * plot.getWidth();
}

ScopeView::Part ScopeView::partAt(juce::Point<int> at) const
{
    const auto point = at.toFloat();
    if (resetButtonArea().contains(point)) return Part::reset;
    if (!getLocalBounds().contains(at)) return Part::none;
    if (showsEndMarker() && std::abs(point.x - endMarkerX()) <= grabReach) return Part::endMarker;
    const auto plot = plotArea();
    const auto span = mobileSpan(currentCurve(), processor.curveAdjust(currentCurve()));
    if (span.to <= span.from) return Part::none;
    const auto phase = (point.x - plot.getX()) / plot.getWidth();
    const auto reach = grabReach / plot.getWidth();
    for (const auto wrap : { 0.0f, 1.0f })
        if (phase >= span.from - wrap - reach && phase <= span.to - wrap + reach) return Part::slope;
    return Part::none;
}

void ScopeView::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const auto plot = plotArea();
    const auto curve = currentCurve();
    const auto adjust = processor.curveAdjust(curve);
    const auto& engine = processor.engine();

    g.setColour(colour::well);
    g.fillRoundedRectangle(area, 6.0f);
    g.setColour(colour::wellLine);
    for (int quarter = 1; quarter < 4; ++quarter)
        g.fillRect(juce::Rectangle<float>(1.0f, area.getHeight()).withX(plot.getX() + plot.getWidth() * static_cast<float>(quarter) * 0.25f));
    const auto centreY = area.getCentreY();
    g.fillRect(juce::Rectangle<float>(area.getX(), centreY - 0.5f, area.getWidth(), 1.0f));

    // The waveforms, a column per scope column: the sidechain behind, the
    // ducked output in front.
    const auto columnWidth = plot.getWidth() / static_cast<float>(scopeColumns);
    const auto amplitude = area.getHeight() * 0.46f;
    const auto yOf = [centreY, amplitude] (float value) { return centreY - std::clamp(value, -1.05f, 1.05f) * amplitude; };
    if (engine.isArmed())
    {
        for (int column = 0; column < scopeColumns; ++column)
        {
            const auto reading = engine.scopeColumn(column);
            const auto x = plot.getX() + static_cast<float>(column) * columnWidth;
            if (reading.sideHigh > reading.sideLow || reading.sideHigh != 0.0f)
            {
                g.setColour(colour::side.withAlpha(0.45f));
                g.fillRect(x, yOf(reading.sideHigh), columnWidth + 0.5f, std::max(1.0f, yOf(reading.sideLow) - yOf(reading.sideHigh)));
            }
            if (reading.outHigh > reading.outLow || reading.outHigh != 0.0f)
            {
                g.setColour(colour::wave.withAlpha(0.7f));
                g.fillRect(x, yOf(reading.outHigh), columnWidth + 0.5f, std::max(1.0f, yOf(reading.outLow) - yOf(reading.outHigh)));
            }
        }
    }

    // The sloped part's room, lit while it is in hand.
    const auto span = mobileSpan(curve, adjust);
    if (span.to > span.from)
    {
        const auto lit = hover == Part::slope || dragging == Part::slope;
        g.setColour(colour::accent.withAlpha(lit ? 0.18f : 0.09f));
        for (const auto wrap : { 0.0f, 1.0f })
        {
            const auto from = std::clamp(span.from - wrap, 0.0f, 1.0f);
            const auto to = std::clamp(span.to - wrap, 0.0f, 1.0f);
            if (to > from)
                g.fillRect(plot.getX() + from * plot.getWidth(), area.getY() + 1.0f, (to - from) * plot.getWidth(), area.getHeight() - 2.0f);
        }
    }

    // The curve itself, from the one function the audio reads.
    juce::Path line;
    constexpr int steps = 256;
    for (int i = 0; i <= steps; ++i)
    {
        const auto phase = static_cast<float>(i) / static_cast<float>(steps);
        const auto gain = curveGain(curve, std::min(phase, 0.99999f), adjust);
        const juce::Point<float> point(plot.getX() + phase * plot.getWidth(), plot.getY() + (1.0f - gain) * plot.getHeight());
        if (i == 0) line.startNewSubPath(point);
        else line.lineTo(point);
    }
    g.setColour(colour::accent);
    g.strokePath(line, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (showsEndMarker())
    {
        const auto x = endMarkerX();
        const auto lit = hover == Part::endMarker || dragging == Part::endMarker;
        g.setColour(colour::marker.withAlpha(lit ? 1.0f : 0.75f));
        g.fillRect(juce::Rectangle<float>(x - 0.5f, area.getY() + 1.0f, 1.0f, area.getHeight() - 2.0f));
        // The tab stays inside the well at either end of the cycle.
        const auto tabX = std::clamp(x, plot.getX() + 10.0f, plot.getRight() - 10.0f);
        const auto tab = juce::Rectangle<float>(18.0f, 16.0f).withCentre({ tabX, plot.getY() + 10.0f });
        g.fillRoundedRectangle(tab, 3.0f);
        g.setColour(colour::well);
        for (int i = -1; i <= 1; ++i)
            g.fillRect(juce::Rectangle<float>(1.0f, 8.0f).withCentre({ tabX + static_cast<float>(i) * 4.0f, tab.getCentreY() }));
    }

    if (engine.isArmed())
    {
        const auto x = plot.getX() + std::clamp(engine.sweepPhase(), 0.0f, 1.0f) * plot.getWidth();
        g.setColour(colour::text.withAlpha(0.55f));
        g.fillRect(juce::Rectangle<float>(x - 0.5f, area.getY() + 1.0f, 1.0f, area.getHeight() - 2.0f));
    }

    // Reset: an arrow round most of a circle.
    const auto button = resetButtonArea();
    const auto centre = button.getCentre();
    const auto adjusted = adjust.slope != 0.0f || adjust.shift != 0.0f;
    g.setColour(hover == Part::reset ? colour::accent : (adjusted ? colour::chipText : colour::chipMuted));
    juce::Path arrow;
    const auto radius = 6.5f;
    arrow.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, 0.6f, juce::MathConstants<float>::twoPi - 0.9f, true);
    g.strokePath(arrow, juce::PathStrokeType(1.8f));
    const auto tip = centre.getPointOnCircumference(radius, 0.6f);
    juce::Path head;
    head.addTriangle(tip.translated(-4.0f, -2.5f), tip.translated(2.5f, -3.5f), tip.translated(0.5f, 3.0f));
    g.fillPath(head);
}

void ScopeView::setHover(Part part)
{
    if (part == hover) return;
    hover = part;
    switch (hover)
    {
        case Part::slope:
        case Part::endMarker: setMouseCursor(juce::MouseCursor::LeftRightResizeCursor); break;
        case Part::reset:     setMouseCursor(juce::MouseCursor::PointingHandCursor); break;
        case Part::none:      setMouseCursor(juce::MouseCursor::NormalCursor); break;
    }
    repaint();
}

void ScopeView::mouseMove(const juce::MouseEvent& event)
{
    setHover(partAt(event.getPosition()));
}

void ScopeView::mouseExit(const juce::MouseEvent&)
{
    if (dragging == Part::none) setHover(Part::none);
}

void ScopeView::mouseDown(const juce::MouseEvent& event)
{
    dragging = partAt(event.getPosition());
    dragStartX = event.x;
    switch (dragging)
    {
        case Part::slope:
            dragStartOffset = mobileOffset(currentCurve(), processor.curveAdjust(currentCurve()).slope);
            break;
        case Part::endMarker:
            if (endParameter != nullptr) endParameter->beginChangeGesture();
            break;
        case Part::reset:
            processor.resetCurve(currentCurve());
            dragging = Part::none;
            repaint();
            break;
        case Part::none: break;
    }
}

void ScopeView::mouseDrag(const juce::MouseEvent& event)
{
    const auto plot = plotArea();
    if (dragging == Part::slope)
    {
        const auto curve = currentCurve();
        const auto travelled = static_cast<float>(event.x - dragStartX) / plot.getWidth();
        processor.setCurveSlope(curve, slopeForOffset(curve, dragStartOffset + travelled));
        repaint();
    }
    else if (dragging == Part::endMarker && endParameter != nullptr)
    {
        const auto value = std::clamp((static_cast<float>(event.x) - plot.getX()) / plot.getWidth(), 0.0f, 1.0f);
        endParameter->setValueNotifyingHost(endParameter->convertTo0to1(value));
        repaint();
    }
}

void ScopeView::mouseUp(const juce::MouseEvent& event)
{
    if (dragging == Part::endMarker && endParameter != nullptr) endParameter->endChangeGesture();
    dragging = Part::none;
    setHover(partAt(event.getPosition()));
}

juce::String ScopeView::getTooltip()
{
    switch (hover)
    {
        case Part::slope:     return "Slope: drag the sloped part left or right to fit the kick";
        case Part::endMarker: return "End: where a one-shot curve stops and holds";
        case Part::reset:     return "Reset this curve's Slope and Shift";
        case Part::none:      break;
    }
    return {};
}
}
