#include "DuckScopeView.h"
#include "../ui/DuckArt.h"

namespace rhino::duck::ui
{
namespace
{
constexpr float grabReach = 7.0f;
constexpr float handleReach = 9.0f;
constexpr float resetSize = 22.0f;

float valueOf(const Processor& processor, const char* id)
{
    const auto* value = processor.state.getRawParameterValue(id);
    return value != nullptr ? value->load(std::memory_order_relaxed) : 0.0f;
}

// A phase on the display: wrapped into the cycle, except that the very end
// of a span stays at the right-hand edge rather than jumping to the left.
float displayPhase(float phase)
{
    const auto wrapped = phase - std::floor(phase);
    return wrapped < 1.0e-6f && phase > 0.5f ? 1.0f : wrapped;
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

// The curve's full height, under the legend's strip at the top.
juce::Rectangle<float> ScopeView::plotArea() const
{
    return getLocalBounds().toFloat().withTrimmedTop(22.0f).withTrimmedBottom(14.0f).reduced(1.0f, 0.0f);
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

juce::Point<float> ScopeView::handlePoint(Part part) const
{
    const auto curve = currentCurve();
    const auto adjust = processor.curveAdjust(curve);
    const auto span = mobileSpan(curve, adjust);
    const auto phase = displayPhase(part == Part::releaseHandle ? span.to : span.from);
    const auto gain = curveGain(curve, std::min(phase, 0.99999f), adjust);
    const auto plot = plotArea();
    return { plot.getX() + phase * plot.getWidth(), plot.getY() + (1.0f - gain) * plot.getHeight() };
}

ScopeView::Part ScopeView::partAt(juce::Point<int> at) const
{
    const auto point = at.toFloat();
    if (resetButtonArea().contains(point)) return Part::reset;
    if (!getLocalBounds().contains(at)) return Part::none;
    if (showsEndMarker() && std::abs(point.x - endMarkerX()) <= grabReach) return Part::endMarker;
    const auto span = mobileSpan(currentCurve(), processor.curveAdjust(currentCurve()));
    if (span.to <= span.from) return Part::none;
    // The end before the start: on a short sloped part the two overlap, and
    // the end is the one only a handle reaches.
    for (const auto handle : { Part::releaseHandle, Part::holdHandle })
        if (point.getDistanceFrom(handlePoint(handle)) <= handleReach) return handle;
    const auto plot = plotArea();
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

    drawRecess(g, area, 6.0f);

    // The grid: a faint line every sixteenth of the cycle, a firmer one every
    // quarter, and the centre the waveforms swing about.
    for (int sixteenth = 1; sixteenth < 16; ++sixteenth)
    {
        g.setColour(colour::wellLine.withAlpha(sixteenth % 4 == 0 ? 1.0f : 0.4f));
        g.fillRect(juce::Rectangle<float>(1.0f, area.getHeight() - 2.0f)
                       .withPosition(plot.getX() + plot.getWidth() * static_cast<float>(sixteenth) / 16.0f, area.getY() + 1.0f));
    }
    const auto centreY = area.getCentreY();
    g.setColour(colour::wellLine);
    g.fillRect(juce::Rectangle<float>(area.getX() + 1.0f, centreY - 0.5f, area.getWidth() - 2.0f, 1.0f));

    // The sloped part's room, lit while it is in hand.
    const auto span = mobileSpan(curve, adjust);
    if (span.to > span.from)
    {
        const auto lit = hover == Part::slope || hover == Part::holdHandle || hover == Part::releaseHandle || dragging != Part::none;
        g.setColour(colour::accent.withAlpha(lit ? 0.16f : 0.08f));
        for (const auto wrap : { 0.0f, 1.0f })
        {
            const auto from = std::clamp(span.from - wrap, 0.0f, 1.0f);
            const auto to = std::clamp(span.to - wrap, 0.0f, 1.0f);
            if (to > from)
                g.fillRect(plot.getX() + from * plot.getWidth(), area.getY() + 1.0f, (to - from) * plot.getWidth(), area.getHeight() - 2.0f);
        }
    }

    // The waveforms, a column per scope column: the kick behind, the signal
    // before the duck, and the ducked signal inside it, so the gap between
    // the two greys is what the duck took away.
    const auto columnWidth = plot.getWidth() / static_cast<float>(scopeColumns);
    const auto amplitude = area.getHeight() * 0.46f;
    const auto yOf = [centreY, amplitude] (float value) { return centreY - std::clamp(value, -1.05f, 1.05f) * amplitude; };
    const auto band = [&g, &yOf, columnWidth] (float x, float low, float high, juce::Colour ink)
    {
        if (!(high > low || high != 0.0f)) return;
        g.setColour(ink);
        g.fillRect(x, yOf(high), columnWidth + 0.5f, std::max(1.0f, yOf(low) - yOf(high)));
    };
    if (engine.isArmed())
    {
        for (int column = 0; column < scopeColumns; ++column)
        {
            const auto reading = engine.scopeColumn(column);
            const auto x = plot.getX() + static_cast<float>(column) * columnWidth;
            band(x, reading.sideLow, reading.sideHigh, colour::side.withAlpha(0.9f));
            band(x, reading.dryLow, reading.dryHigh, colour::dry.withAlpha(0.32f));
            band(x, reading.outLow, reading.outHigh, colour::wave.withAlpha(0.78f));
        }
    }

    // The curve itself, from the one function the audio reads, over a faint
    // fill that fades towards the floor.
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
    auto fill = line;
    fill.lineTo(plot.getRight(), plot.getBottom());
    fill.lineTo(plot.getX(), plot.getBottom());
    fill.closeSubPath();
    g.setGradientFill(juce::ColourGradient(colour::accent.withAlpha(0.13f), plot.getX(), plot.getY(),
                                           colour::accent.withAlpha(0.0f), plot.getX(), plot.getBottom(), false));
    g.fillPath(fill);
    g.setColour(colour::accent);
    g.strokePath(line, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // The sloped part's handles: Hold at its start, Release at its end.
    if (span.to > span.from)
        for (const auto handle : { Part::holdHandle, Part::releaseHandle })
        {
            const auto lit = hover == handle || dragging == handle;
            const auto at = handlePoint(handle);
            const auto size = lit ? 13.0f : 10.0f;
            g.setColour(colour::well);
            g.fillEllipse(juce::Rectangle<float>(size + 3.0f, size + 3.0f).withCentre(at));
            g.setColour(lit ? colour::accentHot : colour::accent);
            g.fillEllipse(juce::Rectangle<float>(size, size).withCentre(at));
            g.setColour(colour::marker);
            g.drawEllipse(juce::Rectangle<float>(size, size).withCentre(at), 1.5f);
        }

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

    // The playhead: where the cycle is now.
    if (engine.isArmed())
    {
        const auto x = plot.getX() + std::clamp(engine.sweepPhase(), 0.0f, 1.0f) * plot.getWidth();
        g.setColour(colour::text.withAlpha(0.55f));
        g.fillRect(juce::Rectangle<float>(x - 0.5f, area.getY() + 1.0f, 1.0f, area.getHeight() - 2.0f));
    }

    // What each colour is, in the top right corner.
    auto legend = juce::Rectangle<float>(area.getRight() - 178.0f, area.getY() + 5.0f, 170.0f, 12.0f);
    g.setFont(font(Face::label, 12.0f));
    for (const auto& [name, ink] : { std::pair { "KICK", colour::side }, std::pair { "DRY", colour::dry }, std::pair { "OUT", colour::wave } })
    {
        auto entry = legend.removeFromLeft(56.0f);
        g.setColour(ink);
        g.fillRoundedRectangle(entry.removeFromLeft(8.0f).withSizeKeepingCentre(8.0f, 8.0f), 2.0f);
        entry.removeFromLeft(5.0f);
        g.setColour(colour::textDim);
        g.drawText(name, entry, juce::Justification::centredLeft, false);
    }

    // Reset: an arrow round most of a circle.
    const auto button = resetButtonArea();
    const auto centre = button.getCentre();
    g.setColour(hover == Part::reset ? colour::accent : (adjust != CurveAdjust {} ? colour::chipText : colour::chipMuted));
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
        case Part::holdHandle:
        case Part::releaseHandle:
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
    const auto part = slopedPart(currentCurve(), processor.curveAdjust(currentCurve()));
    switch (dragging)
    {
        case Part::slope:
        case Part::holdHandle:    dragStartValue = part.start; break;
        case Part::releaseHandle: dragStartValue = part.length; break;
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
    const auto travelled = static_cast<float>(event.x - dragStartX) / plot.getWidth();
    switch (dragging)
    {
        case Part::slope:
        case Part::holdHandle:    processor.setCurveHold(currentCurve(), dragStartValue + travelled); break;
        case Part::releaseHandle: processor.setCurveRelease(currentCurve(), dragStartValue + travelled); break;
        case Part::endMarker:
            if (endParameter != nullptr)
                endParameter->setValueNotifyingHost(endParameter->convertTo0to1(std::clamp((static_cast<float>(event.x) - plot.getX()) / plot.getWidth(), 0.0f, 1.0f)));
            break;
        case Part::reset:
        case Part::none: return;
    }
    repaint();
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
        case Part::slope:
        case Part::holdHandle:    return "Hold: drag the sloped part left or right to fit the kick";
        case Part::releaseHandle: return "Release: drag the end of the sloped part to lengthen or shorten it";
        case Part::endMarker:     return "End: where a one-shot curve stops and holds";
        case Part::reset:         return "Reset this curve's Attack, Hold, Release, Depth and Shift";
        case Part::none:          break;
    }
    return {};
}
}
