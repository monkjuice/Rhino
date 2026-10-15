#pragma once
#include "ui/shared/Theme.h"
#include <array>

namespace rhino
{
// How far up a meter a level reaches, from 0 at the floor to 1 at the top.
// It is the IEC 60268-18 scale Ardour's meters draw (log_meter in
// libs/ardour/ardour/logmeter.h): -70 dB is the floor, the 26 dB below +6
// take the top 57% of the height, so the range a mix lives in is spread out
// and the quiet end is pressed together, and 0 dBFS sits at 87%, leaving room
// above it for an overload to show.
float meterFraction(float decibels);

// What a meter shows, apart from how it is drawn so it can be tested. A peak
// shows at once; the bar then falls at a steady rate in decibels, and the
// highest peak is held for a moment before it falls too, never below the bar.
struct MeterBallistics
{
    static constexpr float floorDb = -70.0f, fallDbPerSecond = 24.0f;
    static constexpr double holdSeconds = 1.5;
    float levelDb = floorDb, heldDb = floorDb;
    double heldFor = 0.0;
    // The loudest sample since the last call, and how long ago that was.
    void advance(float peakDb, double seconds);
};

// A vertical stereo meter: the left channel's bar beside the right's, lit from
// the bottom through green, yellow and red, with the held peak a line above.
// A mono signal lights both. It is fed by its owner and never reads the
// engine, and it is opaque and repaints only when a bar or a held peak moves
// by a whole pixel, so a frame that moves it repaints the meter alone and a
// silent one repaints nothing.
class LevelMeter final : public juce::Component
{
public:
    LevelMeter();
    void feed(float leftDb, float rightDb, double seconds);
    void paint(juce::Graphics&) override;
    void resized() override;

    static constexpr int barWidth = 3, barGap = 1, width = barWidth * 2 + barGap;

private:
    int pixelsFor(float decibels) const;
    std::array<MeterBallistics, 2> channels;
    // The bar and held-peak heights the last repaint was asked for, left then
    // right, compared against before asking for another.
    std::array<int, 4> drawn {-1, -1, -1, -1};
};
}
