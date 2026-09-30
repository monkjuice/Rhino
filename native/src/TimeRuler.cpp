#include "TimeRuler.h"
#include "ArrangementGrid.h"
#include "Theme.h"

namespace rhino
{
juce::String formatClock(double seconds, bool withMilliseconds)
{
    if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
    const auto totalMs = static_cast<juce::int64>(seconds * 1000.0 + 0.5);
    const auto milliseconds = static_cast<int>(totalMs % 1000);
    const auto totalSeconds = totalMs / 1000;
    auto text = juce::String(static_cast<int>(totalSeconds / 60)).paddedLeft('0', 2)
              + ":" + juce::String(static_cast<int>(totalSeconds % 60)).paddedLeft('0', 2);
    if (withMilliseconds) text += ":" + juce::String(milliseconds).paddedLeft('0', 3);
    return text;
}

TimeRuler::TimeRuler(Session& s) : session(s)
{
    setOpaque(true);
    setInterceptsMouseClicks(false, false);
}

void TimeRuler::setView(double start, double span)
{
    if (viewStart == start && viewSpan == span) return;
    viewStart = start;
    viewSpan = span;
    repaint();
}

void TimeRuler::paint(juce::Graphics& g)
{
    g.fillAll(palette::sideSurface);
    if (getWidth() <= 1 || viewSpan <= 0.0) return;

    const auto barLength = std::max(0.25, session.beatsPerBar());
    // Bar one starts at beat zero, which is what makes the first label 00:00.
    const auto timeOfBar = [this, barLength](double bar)
    {
        return session.edit->tempoSequence
            .toTime(tracktion::core::BeatPosition::fromBeats((bar - 1.0) * barLength)).inSeconds();
    };
    const auto xFor = [this](double seconds)
    {
        return static_cast<float>((seconds - viewStart) / viewSpan * getWidth());
    };
    const auto beatAt = [this](double seconds)
    {
        return session.edit->tempoSequence
            .toBeats(tracktion::core::TimePosition::fromSeconds(seconds)).inBeats();
    };

    const auto firstBar = std::max(1.0, std::floor(beatAt(viewStart) / barLength) + 1.0);
    const auto lastBar = std::floor(beatAt(viewStart + viewSpan) / barLength) + 1.0;
    if (lastBar < firstBar) return;
    // A tempo ramp makes bars unequal in pixels, so the step is picked from the
    // width of the first bar on screen and the labels simply thin out or crowd
    // a little where the tempo moves - exactly as the bar numbers above do.
    const auto pixelsPerBar = std::max(0.01f, xFor(timeOfBar(firstBar + 1.0)) - xFor(timeOfBar(firstBar)));
    const auto step = barLabelStep(pixelsPerBar, timeLabelMinimumPixels);

    g.setFont(uiFont(10.0f));
    int painted = 0;
    for (auto bar = firstLabelledBar(firstBar, step); bar <= lastBar + step && painted < 512; bar += step)
    {
        const auto time = timeOfBar(bar);
        const auto x = xFor(time);
        if (x < -1.0f) continue;
        if (x > static_cast<float>(getWidth())) break;
        ++painted;
        // A tick on the line the bar number above is drawn from, and the time
        // set four pixels to the right of it: the same offset the bar number
        // takes, so the two readings share a left edge down the whole column.
        g.setColour(palette::border.brighter(0.2f));
        g.drawVerticalLine(static_cast<int>(x), 0.0f, 4.0f);
        g.setColour(palette::textDim);
        drawSnappedText(g, formatClock(time, true),
                        {static_cast<int>(x) + 4, 0, 76, getHeight()});
    }
}
}
