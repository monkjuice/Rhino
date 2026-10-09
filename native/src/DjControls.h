#pragma once
#include "Theme.h"
#include <cmath>
#include <functional>

// The DJ booth's own controls: a pad that lights, a fader with a cap, a
// level meter and the jog wheel. Painted components rather than stock
// ones, because a CDJ's pads and a DJM's faders say what they are by colour
// and by throw, and a TextButton or a LinearVertical slider in the theme's
// grey says neither.

namespace rhino
{
// A key that lights in a colour: the hot cues, PLAY and CUE, and the
// booth's switches. A right-click is a second action, which is how a hot
// cue is cleared without a modifier. Round, it is the CDJ's big key: a
// dark face in a ring of its colour that fills when lit.
class DjPad final : public juce::Button
{
public:
    DjPad(const juce::String& name, juce::Colour litColour, const juce::String& label = {})
        : juce::Button(name), colour(litColour), text(label.isEmpty() ? name : label)
    {
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
    }
    void setLit(bool shouldBeLit)
    {
        if (lit == shouldBeLit) return;
        lit = shouldBeLit;
        repaint();
    }
    // Blinking says "about to": a start held for the beat.
    void setBlinking(bool shouldBlink)
    {
        if (blinking == shouldBlink) return;
        blinking = shouldBlink;
        repaint();
    }
    void setBlinkPhase(bool on)
    {
        if (!blinking || blinkOn == on) return;
        blinkOn = on;
        repaint();
    }
    void setLabel(const juce::String& label)
    {
        if (text == label) return;
        text = label;
        repaint();
    }
    void setLitColour(juce::Colour c)
    {
        colour = c;
        repaint();
    }
    // A pad that is dark rather than off: a hot cue not yet set still shows
    // its colour faintly, so a hand finds it.
    void setGhosted(bool shouldGhost)
    {
        if (ghosted == shouldGhost) return;
        ghosted = shouldGhost;
        repaint();
    }
    void setRound(bool shouldBeRound)
    {
        round = shouldBeRound;
        repaint();
    }
    bool isLit() const { return lit; }
    std::function<void()> onRightClick;

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu())
        {
            if (onRightClick) onRightClick();
            return;
        }
        juce::Button::mouseDown(event);
    }
    void mouseUp(const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu()) return;
        juce::Button::mouseUp(event);
    }

    void paintButton(juce::Graphics& g, bool highlighted, bool pressed) override
    {
        const auto on = (lit && (!blinking || blinkOn)) || pressed;
        if (round)
        {
            const auto size = static_cast<float>(std::min(getWidth(), getHeight()));
            const auto face = getLocalBounds().toFloat().withSizeKeepingCentre(size, size).reduced(1.5f);
            auto ground = on ? palette::control.overlaidWith(colour.withAlpha(0.35f)) : palette::control;
            if (highlighted && !pressed) ground = ground.brighter(0.08f);
            g.setColour(ground);
            g.fillEllipse(face);
            g.setColour(on ? colour : colour.withMultipliedAlpha(isEnabled() ? 0.55f : 0.25f));
            g.drawEllipse(face.reduced(1.0f), size >= 40.0f ? 3.0f : 2.0f);
            g.setFont(size >= 44.0f ? uiFontBold(11.0f) : uiFontBold(9.0f));
            g.setColour(isEnabled() ? palette::text : palette::disabled);
            drawSnappedText(g, text, face.toNearestInt(), juce::Justification::centred, true);
            return;
        }
        const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        auto ground = on ? colour : (ghosted ? palette::control.overlaidWith(colour.withMultipliedAlpha(0.18f)) : palette::control);
        if (highlighted && !pressed) ground = ground.brighter(0.08f);
        if (!isEnabled()) ground = ground.withMultipliedAlpha(0.5f);
        g.setColour(ground);
        g.fillRoundedRectangle(bounds, 2.0f);
        g.setColour(on ? colour.darker(0.4f) : palette::border);
        g.drawRoundedRectangle(bounds, 2.0f, 1.0f);
        g.setFont(getHeight() >= 22 ? uiFontBold(10.0f) : uiFontBold(9.0f));
        g.setColour(on ? colour.contrasting(0.9f) : (isEnabled() ? palette::text : palette::disabled));
        drawSnappedText(g, text, getLocalBounds(), juce::Justification::centred, true);
    }

private:
    juce::Colour colour;
    juce::String text;
    bool lit = false, blinking = false, blinkOn = true, ghosted = false, round = false;
};

// A fader, vertical or across, dragged by the pixel. Shift drags fine, a
// double-click returns it to its resting value. Values run 0 to 1; what that
// means - a gain, a tempo offset, a crossfader side - is the owner's.
class DjFader final : public juce::Component,
                      public juce::SettableTooltipClient
{
public:
    explicit DjFader(bool isVertical, juce::Colour accentColour = palette::volume)
        : vertical(isVertical), accent(accentColour)
    {
        setWantsKeyboardFocus(false);
        setMouseCursor(vertical ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::LeftRightResizeCursor);
    }
    float value() const { return current; }
    void setValue(float v, bool notify)
    {
        v = juce::jlimit(0.0f, 1.0f, v);
        if (current == v) return;
        current = v;
        repaint();
        if (notify && onChange) onChange(current);
    }
    void setDefault(float v) { resting = juce::jlimit(0.0f, 1.0f, v); }
    // Where the cap shows its centre detent: a tempo fader's zero, a
    // crossfader's middle. Negative for none.
    void setDetent(float v) { detent = v; }
    // Tick marks along the groove, the CDJ's tempo scale.
    void setTicks(int count) { ticks = count; }
    bool isDragging() const { return dragging; }
    std::function<void(float)> onChange;
    std::function<void()> onDragStart, onDragEnd;

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        const auto groove = vertical ? bounds.withSizeKeepingCentre(4.0f, bounds.getHeight() - capLength)
                                     : bounds.withSizeKeepingCentre(bounds.getWidth() - capLength, 4.0f);
        g.setColour(palette::displayInset);
        g.fillRoundedRectangle(groove, 2.0f);
        g.setColour(palette::border);
        g.drawRoundedRectangle(groove, 2.0f, 1.0f);
        if (ticks > 1)
        {
            g.setColour(palette::border.brighter(0.2f));
            for (int i = 0; i < ticks; ++i)
            {
                const auto at = positionFor(static_cast<float>(i) / static_cast<float>(ticks - 1));
                const auto major = i == 0 || i == ticks - 1 || i * 2 == ticks - 1;
                if (vertical)
                    g.fillRect(groove.getRight() + 3.0f, at - 0.5f, major ? 8.0f : 4.0f, 1.0f);
                else
                    g.fillRect(at - 0.5f, groove.getBottom() + 3.0f, 1.0f, major ? 8.0f : 4.0f);
            }
        }
        if (detent >= 0.0f)
        {
            const auto at = positionFor(detent);
            g.setColour(palette::textDim);
            if (vertical) g.fillRect(bounds.getX(), at - 0.5f, bounds.getWidth(), 1.0f);
            else g.fillRect(at - 0.5f, bounds.getY(), 1.0f, bounds.getHeight());
        }
        const auto at = positionFor(current);
        const auto cap = vertical ? juce::Rectangle<float>(bounds.getX() + 2.0f, at - capLength * 0.5f, bounds.getWidth() - 4.0f, capLength)
                                  : juce::Rectangle<float>(at - capLength * 0.5f, bounds.getY() + 2.0f, capLength, bounds.getHeight() - 4.0f);
        g.setColour(juce::Colour(0x66000000));
        g.fillRoundedRectangle(cap.translated(0.0f, 1.0f), 2.0f);
        g.setColour(dragging ? palette::hover.brighter(0.2f) : palette::hover);
        g.fillRoundedRectangle(cap, 2.0f);
        g.setColour(palette::border.brighter(0.3f));
        g.drawRoundedRectangle(cap, 2.0f, 1.0f);
        // The cap's line, in the accent: what the hand looks for.
        g.setColour(accent);
        if (vertical) g.fillRect(cap.getX() + 2.0f, cap.getCentreY() - 1.0f, cap.getWidth() - 4.0f, 2.0f);
        else g.fillRect(cap.getCentreX() - 1.0f, cap.getY() + 2.0f, 2.0f, cap.getHeight() - 4.0f);
    }
    void mouseDown(const juce::MouseEvent& event) override
    {
        dragging = true;
        startValue = current;
        startPoint = event.position;
        if (onDragStart) onDragStart();
        repaint();
    }
    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (!dragging) return;
        const auto travel = static_cast<float>(vertical ? getHeight() : getWidth()) - capLength;
        if (travel <= 0.0f) return;
        const auto delta = vertical ? startPoint.y - event.position.y : event.position.x - startPoint.x;
        const auto fine = event.mods.isShiftDown() ? 0.1f : 1.0f;
        setValue(startValue + fine * delta / travel, true);
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        if (!dragging) return;
        dragging = false;
        if (onDragEnd) onDragEnd();
        repaint();
    }
    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        setValue(resting, true);
    }

private:
    float positionFor(float v) const
    {
        const auto length = static_cast<float>(vertical ? getHeight() : getWidth());
        const auto travel = length - capLength;
        return vertical ? length - capLength * 0.5f - v * travel : capLength * 0.5f + v * travel;
    }
    static constexpr float capLength = 18.0f;
    bool vertical;
    juce::Colour accent;
    float current = 1.0f, resting = 1.0f, detent = -1.0f, startValue = 1.0f;
    juce::Point<float> startPoint;
    bool dragging = false;
    int ticks = 0;
};

// A level meter, lit from the bottom (or the left), falling back at a
// steady rate with the peak held for a moment. Fed a peak per tick by the
// owner's timer; it never reads the engine itself.
class DjMeter final : public juce::Component
{
public:
    explicit DjMeter(bool isVertical = true) : vertical(isVertical) { setInterceptsMouseClicks(false, false); }
    void feed(float peakLinear)
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
    void paint(juce::Graphics& g) override
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

private:
    static constexpr float floorDb = -48.0f, ceilingDb = 3.0f, fall = 0.03f;
    bool vertical;
    float level = 0.0f, held = 0.0f, lastHeld = 0.0f;
    int holdTicks = 0;
};

// The jog wheel: a platter whose marker turns with the track at 33 1/3,
// a ring of ticks around it, and the track's name in the middle. The hand
// on the platter scratches in vinyl mode - the deck follows its speed,
// backwards too - and nudges the tempo in CDJ mode; the hand on the ring
// always nudges. On a deck standing still the platter scrubs.
class DjJogWheel final : public juce::Component,
                         public juce::SettableTooltipClient,
                         private juce::Timer
{
public:
    DjJogWheel()
    {
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
    }
    void setTrackName(const juce::String& name)
    {
        if (trackName == name) return;
        trackName = name;
        repaint();
    }
    // The deck's position, as the marker's angle, and whether it plays.
    void setPosition(double seconds, bool isPlaying)
    {
        const auto angle = static_cast<float>(std::fmod(std::max(0.0, seconds) / secondsPerTurn, 1.0) * juce::MathConstants<double>::twoPi);
        if (std::abs(angle - markerAngle) < 0.01f && playing == isPlaying) return;
        markerAngle = angle;
        playing = isPlaying;
        repaint(platterArea().toNearestInt().expanded(2));
    }
    void setVinylMode(bool vinyl)
    {
        vinylMode = vinyl;
        repaint();
    }
    bool isVinylMode() const { return vinylMode; }
    // The hand's speed in platter units: 1 is the record's own 33 1/3.
    std::function<void(double rate, bool active)> onScratch;
    // The ring's push, in per cent of tempo; 0 lets go.
    std::function<void(float percent)> onNudge;
    // A standing deck dragged: so many seconds along.
    std::function<void(double seconds)> onScrub;
    // Whether the deck plays, which decides between a scratch and a scrub.
    std::function<bool()> deckPlaying;

    void paint(juce::Graphics& g) override
    {
        const auto outer = outerArea();
        const auto platter = platterArea();
        // The ring, with the ticks a CDJ's jog ring carries.
        g.setColour(palette::trackCard);
        g.fillEllipse(outer);
        g.setColour(palette::border);
        g.drawEllipse(outer, 1.0f);
        const auto centre = outer.getCentre();
        const auto outerRadius = outer.getWidth() * 0.5f;
        const auto ringInner = platter.getWidth() * 0.5f + 2.0f;
        g.setColour(palette::border.brighter(0.25f));
        for (int i = 0; i < 48; ++i)
        {
            const auto angle = juce::MathConstants<float>::twoPi * static_cast<float>(i) / 48.0f;
            const auto from = centre.getPointOnCircumference(ringInner + 2.0f, angle);
            const auto to = centre.getPointOnCircumference(outerRadius - 3.0f, angle);
            g.drawLine(from.x, from.y, to.x, to.y, i % 4 == 0 ? 1.5f : 1.0f);
        }
        // The platter and its centre window.
        g.setColour(palette::displayInset);
        g.fillEllipse(platter);
        g.setColour(vinylMode ? palette::djCue.withAlpha(0.6f) : palette::border);
        g.drawEllipse(platter, 1.5f);
        const auto window = platter.reduced(platter.getWidth() * 0.22f);
        g.setColour(palette::sideSurface);
        g.fillEllipse(window);
        g.setColour(palette::border);
        g.drawEllipse(window, 1.0f);
        // The marker, turning with the track.
        const auto markerRadius = platter.getWidth() * 0.5f - 6.0f;
        const auto marker = centre.getPointOnCircumference(markerRadius, markerAngle);
        g.setColour(playing ? palette::djPlay : palette::textDim);
        g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre(marker));
        // The name, two lines at most.
        g.setColour(palette::text);
        g.setFont(window.getWidth() >= 70.0f ? uiFontBold(10.0f) : uiFontBold(8.0f));
        const auto text = window.toNearestInt().reduced(static_cast<int>(window.getWidth() * 0.12f), 0);
        if (trackName.isEmpty())
        {
            g.setColour(palette::textDim);
            drawSnappedText(g, "NO TRACK", text, juce::Justification::centred, true);
        }
        else
            g.drawFittedText(trackName.toUpperCase(), text, juce::Justification::centred, 2, 0.8f);
        if (dragging)
        {
            g.setColour(palette::selection.withAlpha(0.12f));
            g.fillEllipse(onRing ? outer : platter);
        }
    }
    void mouseDown(const juce::MouseEvent& event) override
    {
        const auto platter = platterArea();
        const auto centre = platter.getCentre();
        const auto distance = event.position.getDistanceFrom(centre);
        if (distance > outerArea().getWidth() * 0.5f) return;
        dragging = true;
        onRing = distance > platter.getWidth() * 0.5f;
        lastAngle = angleOf(event.position);
        lastTime = juce::Time::getMillisecondCounterHiRes();
        scratchActive = false;
        repaint();
        startTimerHz(30);
    }
    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (!dragging) return;
        const auto angle = angleOf(event.position);
        auto delta = angle - lastAngle;
        if (delta > juce::MathConstants<float>::pi) delta -= juce::MathConstants<float>::twoPi;
        if (delta < -juce::MathConstants<float>::pi) delta += juce::MathConstants<float>::twoPi;
        lastAngle = angle;
        const auto now = juce::Time::getMillisecondCounterHiRes();
        const auto seconds = std::max(0.001, (now - lastTime) / 1000.0);
        lastTime = now;
        // One turn of the platter is 1.8 s of the record at 33 1/3.
        const auto turns = static_cast<double>(delta) / juce::MathConstants<double>::twoPi;
        const auto rate = turns * secondsPerTurn / seconds;
        const auto isPlaying = deckPlaying ? deckPlaying() : true;
        if (onRing || !vinylMode)
        {
            lastNudge = juce::jlimit(-8.0f, 8.0f, static_cast<float>(rate * 4.0));
            if (onNudge) onNudge(lastNudge);
            return;
        }
        if (!isPlaying)
        {
            if (onScrub) onScrub(turns * secondsPerTurn);
            return;
        }
        scratchActive = true;
        lastRate = juce::jlimit(-8.0, 8.0, rate);
        if (onScratch) onScratch(lastRate, true);
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        if (!dragging) return;
        dragging = false;
        stopTimer();
        if (scratchActive && onScratch) onScratch(0.0, false);
        if (lastNudge != 0.0f && onNudge) onNudge(0.0f);
        scratchActive = false;
        lastNudge = 0.0f;
        repaint();
    }

private:
    // A hand that stops moving but stays on the platter holds the record
    // still; a ring let go of in place lets the tempo go.
    void timerCallback() override
    {
        if (!dragging) return;
        const auto idle = juce::Time::getMillisecondCounterHiRes() - lastTime > 80.0;
        if (!idle) return;
        if (scratchActive && lastRate != 0.0)
        {
            lastRate = 0.0;
            if (onScratch) onScratch(0.0, true);
        }
        if (lastNudge != 0.0f)
        {
            lastNudge = 0.0f;
            if (onNudge) onNudge(0.0f);
        }
    }
    juce::Rectangle<float> outerArea() const
    {
        const auto size = static_cast<float>(std::min(getWidth(), getHeight())) - 2.0f;
        return getLocalBounds().toFloat().withSizeKeepingCentre(size, size);
    }
    juce::Rectangle<float> platterArea() const
    {
        return outerArea().reduced(outerArea().getWidth() * 0.11f);
    }
    float angleOf(juce::Point<float> point) const
    {
        const auto centre = outerArea().getCentre();
        return std::atan2(point.x - centre.x, -(point.y - centre.y));
    }
    static constexpr double secondsPerTurn = 1.8;   // 33 1/3 rpm
    juce::String trackName;
    float markerAngle = 0.0f;
    bool playing = false, vinylMode = true, dragging = false, onRing = false, scratchActive = false;
    float lastAngle = 0.0f, lastNudge = 0.0f;
    double lastTime = 0.0, lastRate = 0.0;
};

// Dresses a stock slider as one of the booth's knobs.
inline void dressDjKnob(juce::Slider& knob, juce::Colour accent, double minimum, double maximum, double resting,
                        const juce::String& tooltip)
{
    knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    knob.setRange(minimum, maximum, 0.0);
    knob.setValue(resting, juce::dontSendNotification);
    knob.setDoubleClickReturnValue(true, resting);
    knob.setColour(juce::Slider::backgroundColourId, palette::control);
    knob.setColour(juce::Slider::trackColourId, accent);
    knob.setColour(juce::Slider::thumbColourId, palette::text);
    knob.setTooltip(tooltip);
    knob.setWantsKeyboardFocus(false);
    knob.setMouseClickGrabsKeyboardFocus(false);
}
}
