#include "LevelMeter.h"
#include <algorithm>
#include <cmath>
#include <iterator>

// The track cards' level meter: see LevelMeter.h.

namespace rhino
{
namespace
{
struct Breakpoint
{
    float decibels, fraction;
};
// Ardour's log_meter, as the points its straight lines join.
constexpr Breakpoint scale[] {{-70.0f, 0.0f},           {-60.0f, 2.5f / 115.0f},  {-50.0f, 7.5f / 115.0f},
                              {-40.0f, 15.0f / 115.0f}, {-30.0f, 30.0f / 115.0f}, {-20.0f, 50.0f / 115.0f},
                              {6.0f, 1.0f}};
}

float meterFraction(float decibels)
{
    // Asked this way round so that a NaN reads as the floor, not the top.
    if (!(decibels > scale[0].decibels))
        return 0.0f;
    for (size_t i = 1; i < std::size(scale); ++i)
    {
        if (decibels > scale[i].decibels)
            continue;
        const auto& low = scale[i - 1];
        const auto& high = scale[i];
        return low.fraction + (decibels - low.decibels) / (high.decibels - low.decibels) * (high.fraction - low.fraction);
    }
    return 1.0f;
}

void MeterBallistics::advance(float peakDb, double seconds)
{
    const auto elapsed = std::max(0.0, seconds);
    const auto incoming = std::isfinite(peakDb) ? std::max(floorDb, peakDb) : floorDb;
    const auto fall = static_cast<float>(fallDbPerSecond * elapsed);
    levelDb = std::max(incoming, std::max(floorDb, levelDb - fall));
    if (incoming >= heldDb)
    {
        heldDb = incoming;
        heldFor = 0.0;
        return;
    }
    heldFor += elapsed;
    if (heldFor > holdSeconds)
        heldDb = std::max(levelDb, heldDb - fall);
}

LevelMeter::LevelMeter()
{
    setOpaque(true);
    // The card under it takes the clicks: a meter is read, not pressed.
    setInterceptsMouseClicks(false, false);
}

int LevelMeter::pixelsFor(float decibels) const
{
    return juce::roundToInt(meterFraction(decibels) * static_cast<float>(getHeight()));
}

void LevelMeter::feed(float leftDb, float rightDb, double seconds)
{
    channels[0].advance(leftDb, seconds);
    channels[1].advance(rightDb, seconds);
    const std::array<int, 4> next {pixelsFor(channels[0].levelDb), pixelsFor(channels[0].heldDb),
                                   pixelsFor(channels[1].levelDb), pixelsFor(channels[1].heldDb)};
    if (next == drawn)
        return;
    drawn = next;
    repaint();
}

void LevelMeter::resized()
{
    // A new height puts every level at a new pixel; JUCE repaints a resized
    // component anyway, and the next feed must not think it already has.
    drawn.fill(-1);
}

void LevelMeter::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    g.setColour(palette::displayInset);
    g.fillRect(bounds);
    const auto height = bounds.getHeight();
    if (height <= 0)
        return;
    // One gradient for both bars, pinned to the meter rather than to the lit
    // part of it, so a colour always stands for the same level: green to
    // -12 dB, yellow by -3 and red from full scale up.
    juce::ColourGradient gradient(palette::meterSafe, 0.0f, static_cast<float>(bounds.getBottom()),
                                  palette::meterClip, 0.0f, static_cast<float>(bounds.getY()), false);
    gradient.addColour(meterFraction(-12.0f), palette::meterSafe);
    gradient.addColour(meterFraction(-3.0f), palette::meterHot);
    gradient.addColour(meterFraction(0.0f), palette::meterClip);
    for (size_t channel = 0; channel < channels.size(); ++channel)
    {
        const auto x = bounds.getX() + static_cast<int>(channel) * (barWidth + barGap);
        if (const auto lit = pixelsFor(channels[channel].levelDb); lit > 0)
        {
            g.setGradientFill(gradient);
            g.fillRect(x, bounds.getBottom() - lit, barWidth, lit);
        }
        if (const auto held = pixelsFor(channels[channel].heldDb); held > 0)
        {
            g.setColour(gradient.getColourAtPosition(static_cast<double>(held) / height).brighter(0.4f));
            g.fillRect(x, bounds.getBottom() - held, barWidth, 1);
        }
    }
}
}
