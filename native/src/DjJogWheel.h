#pragma once
#include "Theme.h"
#include <functional>

namespace rhino
{
// The jog wheel: a platter whose marker turns with the track at 33 1/3,
// a ring of ticks around it, and the track's name in the middle. The hand
// on the platter scratches in vinyl mode - the deck follows its speed,
// backwards too - and nudges the tempo in CDJ mode; the hand on the ring
// always nudges. On a deck standing still the platter scrubs.
//
// The marker moves on every display refresh while the deck plays, so the
// face - the ring and its ticks, the platter, the window and the name - is
// painted once into an image at the scale it is shown at and blitted, and a
// turn of the marker repaints only the marker, where it was and where it is.
// The image is painted on the window's own device grid, so the blit copies
// it rather than resampling it. The wheel paints all of its square, on the
// console's ground.
class DjJogWheel final : public juce::Component,
                         public juce::SettableTooltipClient,
                         private juce::Timer
{
public:
    DjJogWheel();
    void setTrackName(const juce::String& name);
    // The deck's position, as the marker's angle, and whether it plays.
    void setPosition(double seconds, bool isPlaying);
    void setVinylMode(bool vinyl);
    bool isVinylMode() const { return vinylMode; }
    // The hand's speed in platter units: 1 is the record's own 33 1/3.
    std::function<void(double rate, bool active)> onScratch;
    // The ring's push, in per cent of tempo; 0 lets go.
    std::function<void(float percent)> onNudge;
    // A standing deck dragged: so many seconds from where the deck stood
    // when the drag began scrubbing, which `starting` marks, so the owner
    // reads that place then rather than at every step.
    std::function<void(double seconds, bool starting)> onScrub;
    // Whether the deck plays, which decides between a scratch and a scrub.
    std::function<bool()> deckPlaying;

    void paint(juce::Graphics&) override;
    void resized() override;
    void enablementChanged() override;
    void lookAndFeelChanged() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    friend int runArrangementTest();
    // A hand that stops moving but stays on the platter holds the record
    // still; a ring let go of in place lets the tempo go.
    void timerCallback() override;
    juce::Rectangle<float> outerArea() const;
    juce::Rectangle<float> platterArea() const;
    float angleOf(juce::Point<float> point) const;
    // Where the marker stands at an angle, and what a repaint of it covers.
    juce::Rectangle<float> markerBox(float angle) const;
    juce::Rectangle<int> markerDamage(float angle) const;
    // Everything but the marker and a hand's tint, painted at `scale` and
    // `phase` - where the wheel's origin falls inside a device pixel of its
    // window (DevicePixels.h) - into an image of the type `g` draws best.
    void renderFace(juce::Graphics& g, float scale, juce::Point<float> phase);
    void dropFace();

    static constexpr double secondsPerTurn = 1.8;   // 33 1/3 rpm
    juce::String trackName;
    juce::Image face;
    float faceScale = 0.0f;
    juce::Point<float> facePhase;
    float markerAngle = 0.0f;
    bool playing = false, vinylMode = true, dragging = false, onRing = false, scratchActive = false, scrubbing = false;
    float lastAngle = 0.0f, lastNudge = 0.0f;
    double lastTime = 0.0, lastRate = 0.0, scrubbed = 0.0;
};
}
