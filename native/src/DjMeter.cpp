#include "DjMeter.h"
#include <cmath>

// The booth's level meter: see DjMeter.h.

namespace rhino
{
DjMeter::DjMeter(bool isVertical) : vertical(isVertical)
{
    setOpaque(true);
    setInterceptsMouseClicks(false, false);
}

void DjMeter::feed(float peakLinear)
{
    const auto db = peakLinear > 0.0f ? std::max(floorDb, 20.0f * std::log10(peakLinear)) : floorDb;
    const auto incoming = juce::jlimit(0.0f, 1.0f, (db - floorDb) / (ceilingDb - floorDb));
    auto next = std::max(incoming, level - fall);
    if (incoming >= held || holdTicks <= 0)
    {
        held = incoming;
        holdTicks = 24;
    }
    else
    {
        --holdTicks;
    }
    if (std::abs(next - level) > 0.002f || held != lastHeld)
    {
        level = next;
        lastHeld = held;
        repaint();
    }
}

void DjMeter::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(palette::displayInset);
    g.fillRect(bounds);
    const auto lit = vertical ? bounds.withTop(bounds.getBottom() - bounds.getHeight() * level)
                              : bounds.withWidth(bounds.getWidth() * level);
    // Cyan up to -6 dB, amber to -1, red at the top, as a mixer's.
    const auto split = [&bounds, this](float db)
    {
        const auto fraction = (db - floorDb) / (ceilingDb - floorDb);
        return vertical ? bounds.getBottom() - bounds.getHeight() * fraction : bounds.getX() + bounds.getWidth() * fraction;
    };
    const auto amberAt = split(-6.0f), redAt = split(-1.0f);
    juce::Graphics::ScopedSaveState scope(g);
    g.reduceClipRegion(lit.toNearestInt());
    g.setColour(palette::volume);
    g.fillRect(bounds);
    g.setColour(palette::djCue);
    g.fillRect(vertical ? bounds.withBottom(amberAt) : bounds.withLeft(amberAt));
    g.setColour(palette::recordAccent);
    g.fillRect(vertical ? bounds.withBottom(redAt) : bounds.withLeft(redAt));
    g.setColour(palette::text);
    const auto peak = split(floorDb + held * (ceilingDb - floorDb));
    if (vertical) g.fillRect(bounds.getX(), peak - 1.0f, bounds.getWidth(), 1.0f);
    else g.fillRect(peak - 1.0f, bounds.getY(), 1.0f, bounds.getHeight());
}
}
