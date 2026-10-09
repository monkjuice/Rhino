#pragma once
#include "Theme.h"
#include <cmath>
#include <functional>

// The DJ booth's own controls: a pad that lights, a fader with a cap, and a
// level meter. Painted components rather than stock ones, because a CDJ's
// pads and a DJM's faders say what they are by colour and by throw, and a
// TextButton or a LinearVertical slider in the theme's grey says neither.

namespace rhino
{
// A key that lights in a colour: the hot cues, PLAY and CUE, and the
// booth's switches. A right-click is a second action, which is how a hot
// cue is cleared without a modifier.
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
        const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        const auto on = (lit && (!blinking || blinkOn)) || pressed;
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
    bool lit = false, blinking = false, blinkOn = true, ghosted = false;
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
