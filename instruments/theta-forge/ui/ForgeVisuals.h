#pragma once

#include "ForgeLayout.h"
#include <functional>
#include <BinaryData.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

// Drawing primitives and the knob look. Layout lives next door in
// ForgeLayout.h; nothing here decides where anything goes.
namespace theta::forge::ui
{
inline const auto electricBlue = juce::Colour(0xff45a8ff);
inline const auto signalViolet = juce::Colour(0xff9a6cff);
inline const auto panel = juce::Colour(0xff111522);
inline const auto panelRaised = juce::Colour(0xff191d2d);
inline const auto line = juce::Colour(0xff34394e);
inline const auto text = juce::Colour(0xffe8eaff);
inline const auto mutedText = juce::Colour(0xff8f95ad);

inline constexpr int handleWidth = 46;

// --- Knob geometry ----------------------------------------------------------
//
// Shared by the look that draws a knob and by the component that has to know
// what the mouse is over, so the ring you can see and the ring you can grab
// cannot drift apart.

// The 4px inset is the optical one every knob's circle sits inside.
inline juce::Rectangle<float> knobCircle(juce::Rectangle<int> rotaryArea)
{
    const auto area = rotaryArea.toFloat().reduced(4.0f);
    const auto diameter = juce::jmin(area.getWidth(), area.getHeight());
    return juce::Rectangle<float>(diameter, diameter).withCentre(area.getCentre());
}

// Where the modulation ring is drawn, as a share of the knob's radius. The
// knob's body stops at 0.81, so the ring sits in clear air outside it.
inline constexpr float modRingRadius = 0.88f;

// The band the ring can be grabbed in: everything outside the body out to a
// little past the rim. Keeping it clear of the body is what lets the knob keep
// its own gesture — inside the circle still means "turn this".
inline bool onModRing(juce::Rectangle<int> rotaryArea, juce::Point<int> position)
{
    const auto knob = knobCircle(rotaryArea);
    const auto radius = knob.getWidth() * 0.5f;
    if (radius <= 0.0f) return false;
    const auto distance = position.toFloat().getDistanceFrom(knob.getCentre());
    return distance >= radius * 0.82f && distance <= radius * 1.06f;
}

inline juce::Colour accentFor(const Module& module)
{
    return module.violet ? signalViolet : electricBlue;
}

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    // Steppers and bars are both bar-style sliders, so JUCE routes them here.
    // A vertical one is a plain numeric field: for a tuning value the number is
    // the information and a fill proportion would be noise. A horizontal one is
    // a matrix amount, where the fill is the whole point — it runs out from
    // wherever zero falls in the range, so the sign of a depth reads across the
    // table without the number being looked at.
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float, float, float, juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        const auto area = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(1.0f);
        const auto accent = slider.findColour(juce::Slider::rotarySliderFillColourId);
        const auto enabled = slider.isEnabled();
        const auto active = slider.getValue() != slider.getDoubleClickReturnValue();
        const auto horizontal = style == juce::Slider::LinearBar;

        g.setColour(juce::Colour(0xff0b0e18).withAlpha(enabled ? 1.0f : 0.5f));
        g.fillRoundedRectangle(area, 3.0f);

        if (horizontal)
        {
            const auto range = slider.getRange();
            const auto at = [&] (double plain)
            {
                const auto span = range.getLength();
                const auto proportion = span > 0.0 ? (plain - range.getStart()) / span : 0.0;
                return area.getX() + static_cast<float>(juce::jlimit(0.0, 1.0, proportion)) * area.getWidth();
            };
            const auto origin = at(juce::jlimit(range.getStart(), range.getEnd(), 0.0));
            const auto now = at(slider.getValue());

            g.setColour(line.withAlpha(enabled ? 0.7f : 0.25f));
            g.fillRect(origin - 0.5f, area.getY() + 2.0f, 1.0f, area.getHeight() - 4.0f);
            if (std::abs(now - origin) >= 1.0f)
            {
                g.setColour(accent.withAlpha(enabled ? 0.5f : 0.15f));
                g.fillRoundedRectangle({juce::jmin(origin, now), area.getY() + 1.5f,
                                        std::abs(now - origin), area.getHeight() - 3.0f}, 2.0f);
            }
        }

        g.setColour((active ? accent : line).withAlpha(enabled ? 1.0f : 0.35f));
        g.drawRoundedRectangle(area, 3.0f, 1.0f);

        // Over a fill, the value is read against the accent rather than in it.
        g.setColour((horizontal || !active ? text : accent).withAlpha(enabled ? 1.0f : 0.35f));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(slider.getTextFromValue(slider.getValue()), area, juce::Justification::centred);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float position, float startAngle, float endAngle, juce::Slider& slider) override
    {
        const auto knob = knobCircle({x, y, width, height});
        const auto centre = knob.getCentre();
        const auto radius = knob.getWidth() * 0.5f;
        const auto angle = juce::jmap(position, startAngle, endAngle);
        const auto accent = slider.findColour(juce::Slider::rotarySliderFillColourId);
        const auto enabled = slider.isEnabled();
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
        g.setColour(line.withAlpha(enabled ? 1.0f : 0.4f));
        g.strokePath(ticks, juce::PathStrokeType(1.0f));

        auto body = knob.reduced(radius * 0.19f);
        juce::ColourGradient metal(juce::Colour(0xff34394b), body.getX(), body.getY(),
                                   juce::Colour(0xff111522), body.getRight(), body.getBottom(), false);
        g.setGradientFill(metal);
        g.fillEllipse(body);
        g.setColour(juce::Colour(0xff050711));
        g.drawEllipse(body, 2.0f);

        // One lit LED where the pointer aims, rather than a strip filled from the
        // start of the travel. The eye then reads the position itself, which is
        // what the value is, and a centred parameter like PAN no longer looks
        // like it is holding a large amount of something.
        const auto led = polar(radius * 0.71f, angle);
        g.setColour(accent.withAlpha(enabled ? 0.22f : 0.06f));
        g.fillEllipse(juce::Rectangle<float>(9.0f, 9.0f).withCentre(led));
        g.setColour(accent.withAlpha(enabled ? 1.0f : 0.3f));
        g.fillEllipse(juce::Rectangle<float>(4.0f, 4.0f).withCentre(led));

        // A modulated knob carries a ring outside the LED, so the two never read
        // as one. Two things are drawn on it: how far this knob's slots could
        // move it, faint, because a reach is potential rather than a value; and,
        // while a note is sounding, where the modulation actually has it this
        // instant, bright, with a marker at the end. That second arc fills and
        // empties as the source plays, which is what makes an envelope or an LFO
        // visible on the knob it is driving rather than only in the module it
        // comes from.
        //
        // Live is asked of the panel rather than inferred from the offset: an
        // LFO passes through zero twice a cycle, and a marker that blinked out
        // each time it did would read as a fault rather than as a crossing.
        const auto& properties = slider.getProperties();
        const auto depth = static_cast<float>(properties.getWithDefault("modDepth", 0.0));
        const auto offset = static_cast<float>(properties.getWithDefault("modOffset", 0.0));
        const auto live = static_cast<bool>(properties.getWithDefault("modLive", false));
        if ((depth != 0.0f || offset != 0.0f) && enabled)
        {
            const auto ring = radius * modRingRadius;
            const auto angleAt = [&] (float amount)
            {
                return juce::jmap(juce::jlimit(0.0f, 1.0f, position + amount), startAngle, endAngle);
            };
            const auto arc = [&] (float to, juce::Colour colour, float thickness)
            {
                if (std::abs(to - angle) < 0.002f) return;
                juce::Path path;
                path.addCentredArc(centre.x, centre.y, ring, ring, 0.0f,
                                   juce::jmin(angle, to), juce::jmax(angle, to), true);
                g.setColour(colour);
                g.strokePath(path, juce::PathStrokeType(thickness, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
            };

            // The reach lifts under the cursor, which is the only thing telling
            // you the ring is a control and not just a reading.
            const auto hovered = static_cast<bool>(properties.getWithDefault("modHover", false));
            arc(angleAt(depth), signalViolet.withAlpha(hovered ? 0.75f : 0.32f), hovered ? 4.0f : 3.0f);
            if (live)
            {
                const auto now = angleAt(offset);
                arc(now, signalViolet.withAlpha(0.95f), 3.0f);
                const auto marker = polar(ring, now);
                g.setColour(signalViolet.withAlpha(0.3f));
                g.fillEllipse(juce::Rectangle<float>(11.0f, 11.0f).withCentre(marker));
                g.setColour(signalViolet.interpolatedWith(juce::Colours::white, 0.75f));
                g.fillEllipse(juce::Rectangle<float>(4.5f, 4.5f).withCentre(marker));
            }
        }

        juce::Path pointer;
        pointer.startNewSubPath(centre);
        pointer.lineTo(polar(radius * 0.52f, angle));
        g.setColour(text.withAlpha(enabled ? 1.0f : 0.35f));
        g.strokePath(pointer, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        g.setColour(accent.withAlpha(enabled ? 1.0f : 0.3f));
        g.fillEllipse(juce::Rectangle<float>(5.0f, 5.0f).withCentre(centre));
    }
};

// A knob whose modulation ring can be taken hold of. A press that lands on the
// ring sets the depth of the slot pointed here; a press anywhere inside the
// body is an ordinary knob. Splitting them by where the press lands is what
// lets the ring become a control without the knob losing the gesture a hand
// already knows.
class ModKnob final : public juce::Slider
{
public:
    // Set by the editor each refresh: the depth the ring is showing, and
    // whether there is a single slot for a drag to mean something to.
    float ringDepth = 0.0f;
    bool ringDraggable = false;
    // Given the depth the drag has reached. The editor decides which slot that
    // belongs to; this component knows only the gesture.
    std::function<void(float)> onRingDrag;

    // Pixels of travel for a whole range, ordinarily and while a modifier asks
    // for a slower hand. Shift or Ctrl, because different hosts have trained
    // people on different ones and there is no cost to taking both.
    static constexpr int normalDragPixels = 250;
    static constexpr int fineDragPixels = 1400;

    static bool fineDrag(const juce::MouseEvent& event)
    {
        return event.mods.isShiftDown() || event.mods.isCommandDown();
    }

    // When set, a gesture lands on one of this many evenly spaced steps across
    // the range, so a knob that picks from a list of things can actually pick
    // one instead of stopping a hair short of it. The fine modifier turns it
    // off, which is how you reach the places in between on purpose.
    //
    // Only a gesture is affected. JUCE asks snapValue about a value the hand
    // arrived at, never about one set by the host, an attachment or the matrix,
    // so a modulated position still sweeps the table smoothly.
    int gestureSteps = 0;

    double snapValue(double attempted, juce::Slider::DragMode) override
    {
        if (!stepping()) return attempted;
        const auto step = gestureStep();
        return getMinimum() + std::round((attempted - getMinimum()) / step) * step;
    }

    // One notch, one step. Left to itself the wheel moves a knob by less than a
    // step is wide, so snapping rounded every notch straight back to where it
    // started and the wheel did nothing at all.
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        if (!stepping() || wheel.deltaY == 0.0f)
        {
            juce::Slider::mouseWheelMove(event, wheel);
            return;
        }
        const auto step = gestureStep();
        const auto direction = wheel.deltaY > 0.0f ? 1.0 : -1.0;
        const auto now = std::round((getValue() - getMinimum()) / step);
        setValue(getMinimum() + (now + direction) * step, juce::sendNotificationSync);
    }

    bool overRing(juce::Point<int> position)
    {
        return ringDraggable && onModRing(ringArea(), position);
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (!event.mods.isPopupMenu() && overRing(event.getPosition()))
        {
            draggingRing = true;
            depthAtDragStart = ringDepth;
            return;
        }
        // Set per gesture rather than once, so the modifier is read at the
        // moment the hand goes down on the control.
        setMouseDragSensitivity(fineDrag(event) ? fineDragPixels : normalDragPixels);
        juce::Slider::mouseDown(event);
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (!draggingRing) { juce::Slider::mouseDrag(event); return; }
        // Up is more, down is less, and the whole bipolar range is 200 pixels
        // of travel, so a depth can be crossed from one sign to the other
        // without letting go. A modifier stretches that the same way it
        // stretches a knob's own travel.
        const auto pixels = fineDrag(event) ? 560.0f : 100.0f;
        const auto moved = -static_cast<float>(event.getDistanceFromDragStartY()) / pixels;
        if (onRingDrag) onRingDrag(juce::jlimit(-1.0f, 1.0f, depthAtDragStart + moved));
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        if (draggingRing) { draggingRing = false; return; }
        juce::Slider::mouseUp(event);
    }

    // Double-clicking the ring takes the routing back to no depth, the same way
    // double-clicking a knob takes it back to its default.
    void mouseDoubleClick(const juce::MouseEvent& event) override
    {
        if (overRing(event.getPosition()))
        {
            if (onRingDrag) onRingDrag(0.0f);
            return;
        }
        juce::Slider::mouseDoubleClick(event);
    }

    void mouseMove(const juce::MouseEvent& event) override
    {
        setRingHover(overRing(event.getPosition()));
        juce::Slider::mouseMove(event);
    }

    void mouseExit(const juce::MouseEvent& event) override
    {
        setRingHover(false);
        juce::Slider::mouseExit(event);
    }

private:
    bool draggingRing = false;
    float depthAtDragStart = 0.0f;

    // Stepping is off while the fine modifier is held, which is how the places
    // between two steps are reached deliberately rather than by accident.
    bool stepping() const
    {
        const auto mods = juce::ModifierKeys::getCurrentModifiers();
        return gestureSteps >= 2 && getMaximum() > getMinimum()
            && !mods.isShiftDown() && !mods.isCommandDown();
    }

    double gestureStep() const
    {
        return (getMaximum() - getMinimum()) / static_cast<double>(gestureSteps - 1);
    }

    // The rotary itself, which is not the whole component: a knob keeps its
    // readout below, and the look is handed only the part it draws the circle
    // in.
    juce::Rectangle<int> ringArea()
    {
        if (auto* look = dynamic_cast<juce::Slider::LookAndFeelMethods*>(&getLookAndFeel()))
            return look->getSliderLayout(*this).sliderBounds;
        return getLocalBounds();
    }

    void setRingHover(bool hovered)
    {
        if (static_cast<bool>(getProperties().getWithDefault("modHover", false)) == hovered) return;
        getProperties().set("modHover", hovered);
        setMouseCursor(hovered ? juce::MouseCursor::UpDownResizeCursor
                               : juce::MouseCursor::NormalCursor);
        repaint();
    }
};

// The dot in a module header that switches the module on. Serum marks an
// enabled module the same way, and it reads at a glance across eight panels in
// a way a labelled checkbox does not.
class EnableLed final : public juce::Button
{
public:
    EnableLed() : juce::Button("enable") { setClickingTogglesState(true); }

    juce::Colour accent = electricBlue;

    void paintButton(juce::Graphics& g, bool highlighted, bool) override
    {
        const auto area = getLocalBounds().toFloat().reduced(getWidth() * 0.3f);
        const auto on = getToggleState();
        if (on)
        {
            g.setColour(accent.withAlpha(0.22f));
            g.fillEllipse(area.expanded(3.5f));
        }
        g.setColour(on ? accent : juce::Colour(0xff232838));
        g.fillEllipse(area);
        g.setColour(on ? juce::Colours::white.withAlpha(0.5f)
                       : mutedText.withAlpha(highlighted ? 0.85f : 0.45f));
        g.drawEllipse(area, 1.0f);
    }
};

// The tag beside a source that you drag onto a knob. It is deliberately not
// the knob or the macro dial itself: dragging those has to keep meaning "turn
// this", so the grab handle is a separate thing sitting next to the control.
class SourceHandle final : public juce::Component,
                          public juce::SettableTooltipClient
{
public:
    SourceHandle(int sourceIndex, juce::String label)
        : source(sourceIndex), caption(std::move(label))
    {
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        setRepaintsOnMouseActivity(true);
    }

    int source = 0;
    juce::String caption;
    juce::Colour accent = signalViolet;
    // Set by the editor while this handle is being dragged.
    bool dragging = false;

    void paint(juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat().reduced(0.6f);
        const auto lit = dragging || isMouseOver();
        g.setColour(lit ? accent.withAlpha(0.3f) : juce::Colour(0xff0d1120));
        g.fillRoundedRectangle(area, 3.0f);
        g.setColour(accent.withAlpha(lit ? 1.0f : 0.65f));
        g.drawRoundedRectangle(area, 3.0f, 1.0f);
        g.setColour(lit ? juce::Colours::white : accent);
        g.setFont(juce::FontOptions(juce::jmin(10.0f, area.getHeight() * 0.72f), juce::Font::bold));
        g.drawText(caption, area, juce::Justification::centred);
    }
};

// A physical rocker, for the switches that are genuinely two-state. A knob
// whose only readout is ON or OFF asks the hand to do the wrong gesture.
//
// Drawn rather than blitted, so it scales with the window and picks up the
// panel's accent the way everything else does.
class RockerSwitch final : public juce::Button
{
public:
    explicit RockerSwitch(const juce::String& name) : juce::Button(name) { setClickingTogglesState(true); }

    juce::Colour accent = electricBlue;

    void paintButton(juce::Graphics& g, bool highlighted, bool held) override
    {
        const auto on = getToggleState();
        const auto enabled = isEnabled();
        const auto alpha = enabled ? 1.0f : 0.35f;
        const auto bezel = getLocalBounds().toFloat();
        const auto radius = bezel.getWidth() * 0.2f;

        // A dark bezel with a thin rim that picks up both accents, the way the
        // panel's own frame does. The rim is a stroke, not a fill: filled, it
        // reads as a pale outline rather than an edge catching the light.
        g.setColour(juce::Colour(0xff0a0c14));
        g.fillRoundedRectangle(bezel, radius);
        juce::ColourGradient rim(signalViolet.withAlpha(alpha * (highlighted ? 1.0f : 0.8f)),
                                 bezel.getX(), bezel.getY(),
                                 accent.withAlpha(alpha * (highlighted ? 1.0f : 0.8f)),
                                 bezel.getRight(), bezel.getBottom(), false);
        g.setGradientFill(rim);
        g.drawRoundedRectangle(bezel.reduced(0.7f), radius, 1.4f);

        const auto well = bezel.reduced(bezel.getWidth() * 0.13f);
        g.setColour(juce::Colour(0xff05070e));
        g.fillRoundedRectangle(well, radius * 0.7f);

        const auto body = well.reduced(well.getWidth() * 0.08f);

        // The rocker actually rocks: the raised face is at the bottom when on
        // and at the top when off, and the indicator travels with it. Keeping
        // the raised face on one side and only changing the light would leave
        // the switch looking stuck.
        const auto travel = held ? 0.04f : 0.0f;
        const auto pivot = body.getY() + body.getHeight() * (on ? 0.38f + travel : 0.62f - travel);
        const juce::Rectangle<float> top(body.getX(), body.getY(), body.getWidth(), pivot - body.getY());
        const juce::Rectangle<float> bottom(body.getX(), pivot, body.getWidth(), body.getBottom() - pivot);
        const auto raised = on ? bottom : top;
        // The far side is angled away from the eye, so it is both darker and
        // narrower than the face pointing at you.
        const auto sunken = (on ? top : bottom).reduced(body.getWidth() * 0.07f, 0.0f);

        juce::ColourGradient shade(juce::Colour(0xff161a26), sunken.getX(), sunken.getY(),
                                   juce::Colour(0xff070a12), sunken.getX(), sunken.getBottom(), false);
        g.setGradientFill(shade);
        g.fillRoundedRectangle(sunken, radius * 0.4f);

        // Light falls from above, so the raised face is brightest at whichever
        // edge is tipped towards it.
        juce::ColourGradient metal(juce::Colour(0xffb4bccf).withAlpha(alpha), raised.getX(), raised.getY(),
                                   juce::Colour(0xff4e5568).withAlpha(alpha), raised.getX(), raised.getBottom(), false);
        if (!on) metal = juce::ColourGradient(juce::Colour(0xff98a0b4).withAlpha(alpha), raised.getX(), raised.getY(),
                                              juce::Colour(0xff6a7186).withAlpha(alpha), raised.getX(), raised.getBottom(), false);
        g.setGradientFill(metal);
        g.fillRoundedRectangle(raised, radius * 0.5f);

        // The lit lip where the raised face breaks the plane of the recess.
        g.setColour(juce::Colours::white.withAlpha(0.3f * alpha));
        const auto lipY = on ? raised.getY() + 0.5f : raised.getBottom() - 0.5f;
        g.drawLine(raised.getX() + 1.0f, lipY, raised.getRight() - 1.0f, lipY, 1.0f);

        // The indicator rides the raised face: lit when on, a dark slot when off.
        const auto barWidth = juce::jmax(2.0f, body.getWidth() * 0.16f);
        const auto bar = juce::Rectangle<float>(barWidth, raised.getHeight() * 0.5f)
            .withCentre(raised.getCentre());
        if (on && enabled)
        {
            for (auto spread = 5.0f; spread >= 1.0f; spread -= 2.0f)
            {
                g.setColour(accent.withAlpha(0.16f));
                g.fillRoundedRectangle(bar.expanded(spread), barWidth);
            }
            g.setColour(accent);
        }
        else
        {
            g.setColour(juce::Colour(0xff2b3044).withAlpha(alpha));
        }
        g.fillRoundedRectangle(bar, barWidth * 0.5f);
    }
};

// A small labelled on/off button. Used for the filter's per-source routing,
// where the label is the whole control and a knob would be absurd.
class ToggleChip final : public juce::Button
{
public:
    explicit ToggleChip(const juce::String& label) : juce::Button(label) { setClickingTogglesState(true); }

    juce::Colour accent = electricBlue;

    void paintButton(juce::Graphics& g, bool highlighted, bool) override
    {
        const auto area = getLocalBounds().toFloat().reduced(1.0f);
        const auto on = getToggleState();
        const auto enabled = isEnabled();
        g.setColour(on ? accent.withAlpha(enabled ? 0.24f : 0.08f) : juce::Colour(0xff0b0e18));
        g.fillRoundedRectangle(area, 3.0f);
        g.setColour((on ? accent : line).withAlpha(enabled ? (highlighted ? 1.0f : 0.85f) : 0.3f));
        g.drawRoundedRectangle(area, 3.0f, 1.0f);
        g.setColour((on ? accent : mutedText).withAlpha(enabled ? 1.0f : 0.35f));
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText(getName(), area, juce::Justification::centred);
    }
};

// A tab in the title bar. Only the top row of the panel follows it, so it is
// drawn as a chip that lights along its bottom edge rather than as a folder tab
// joined to the whole window: the edge points down at the one row that changes.
class PageTab final : public juce::Button
{
public:
    explicit PageTab(const juce::String& label) : juce::Button(label) {}

    juce::Colour accent = electricBlue;

    void paintButton(juce::Graphics& g, bool highlighted, bool) override
    {
        const auto area = getLocalBounds().toFloat().reduced(0.5f);
        const auto on = getToggleState();

        g.setColour(on ? panelRaised : juce::Colour(0xff0d1120));
        g.fillRoundedRectangle(area, 4.0f);
        g.setColour((on ? accent : line).withAlpha(on ? 1.0f : (highlighted ? 0.9f : 0.6f)));
        g.drawRoundedRectangle(area, 4.0f, 1.0f);
        if (on)
        {
            g.setColour(accent.withAlpha(0.25f));
            g.fillRoundedRectangle(area.reduced(1.0f), 3.5f);
            g.setColour(accent);
            g.fillRoundedRectangle(area.withTop(area.getBottom() - 2.5f).reduced(3.0f, 0.0f), 1.25f);
        }
        // Qualified: Button has a private `text` member of its own that would
        // otherwise win the lookup here.
        g.setColour(on ? theta::forge::ui::text : mutedText.withAlpha(highlighted ? 1.0f : 0.8f));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(getName(), getLocalBounds(), juce::Justification::centred);
    }
};

// --- Tables -----------------------------------------------------------------
//
// The matrix is eight slots read as rows. What makes that a table rather than a
// field of controls is the furniture around the controls: titles once at the
// top, a number against every row, and a band under alternate rows so the eye
// tracks across one slot without sliding into the next.

inline void drawColumnTitle(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title)
{
    if (title.isEmpty()) return;
    g.setColour(mutedText.withAlpha(0.8f));
    g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    g.drawText(title, area, juce::Justification::centred);
}

inline void drawColumnTitleRule(juce::Graphics& g, juce::Rectangle<int> titles)
{
    g.setColour(line.withAlpha(0.7f));
    g.drawHorizontalLine(titles.getBottom() - 1, static_cast<float>(titles.getX()),
                         static_cast<float>(titles.getRight()));
}

// `number` is the slot as a player counts them, from one. A live slot's number
// is lit, so the rows actually doing something are countable at a glance.
inline void drawTableRow(juce::Graphics& g, juce::Rectangle<int> row, juce::Rectangle<int> gutter,
                         int number, bool live, juce::Colour accent)
{
    if (number % 2 == 0)
    {
        g.setColour(juce::Colour(0xff0d1120).withAlpha(0.55f));
        g.fillRoundedRectangle(row.toFloat()
                                   .withLeft(static_cast<float>(gutter.getX()))
                                   .reduced(0.0f, 2.0f), 3.0f);
    }
    g.setColour(live ? accent : mutedText.withAlpha(0.45f));
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    g.drawText(juce::String(number), gutter, juce::Justification::centred);
}

// --- Drawing a wave ----------------------------------------------------------
//
// One shape, used by the small tube on the oscillator and by the big canvas in
// the table editor, so the two cannot drift apart.

// The trace of a wave across a box: phase runs left to right, +1 sits at the
// top. The curve is sampled rather than handed in, because the small tube reads
// a morph between two frames and the canvas reads one frame exactly.
inline juce::Path wavePath(juce::Rectangle<float> box, const std::function<float(float)>& at, int points)
{
    juce::Path path;
    for (int i = 0; i <= points; ++i)
    {
        const auto phase = static_cast<float>(i) / static_cast<float>(points);
        const auto x = box.getX() + phase * box.getWidth();
        const auto y = box.getCentreY() - juce::jlimit(-1.0f, 1.0f, at(phase)) * box.getHeight() * 0.5f;
        if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
    }
    return path;
}

// The area a wave encloses against its zero line, which is how Serum and Vital
// both draw one and what gives a wave its weight on screen instead of leaving
// it a hairline.
//
// It is one path, not two. The trace already ends on the right-hand edge, so
// running it back along the zero line and closing it makes a figure that
// crosses itself wherever the wave crosses zero: the humps above the line and
// the humps below it wind in opposite directions. Under the non-zero rule both
// are filled and the space outside them is not, which is exactly the region
// between the curve and the line, however many times it changes sides. Filling
// each half separately would need the crossings found first, and this needs
// none of them.
inline void fillWaveArea(juce::Graphics& g, juce::Rectangle<float> box, const juce::Path& trace,
                         juce::Colour colour, float alpha)
{
    auto area = trace;
    area.lineTo(box.getRight(), box.getCentreY());
    area.lineTo(box.getX(), box.getCentreY());
    area.closeSubPath();

    // Brightest against the curve and falling away toward the line, so a tall
    // excursion reads as further from zero rather than merely as more ink.
    //
    // Painted twice, once from each edge toward the line, because one gradient
    // cannot be bright at the top and at the bottom of the same box. The two
    // overlap only where the fill is faintest.
    juce::ColourGradient above(colour.withAlpha(0.34f * alpha), box.getCentreX(), box.getY(),
                               colour.withAlpha(0.06f * alpha), box.getCentreX(), box.getCentreY(), false);
    g.setGradientFill(above);
    g.fillPath(area);
    juce::ColourGradient below(colour.withAlpha(0.34f * alpha), box.getCentreX(), box.getBottom(),
                               colour.withAlpha(0.06f * alpha), box.getCentreX(), box.getCentreY(), false);
    g.setGradientFill(below);
    g.fillPath(area);
}

// Rounded joins rather than the default mitre: an envelope whose decay is a
// millisecond turns two of its corners into near-spikes, and a mitre there
// shoots a splinter of the fat halo stroke well clear of the shape.
inline void strokeGlow(juce::Graphics& g, const juce::Path& path, juce::Colour colour, float alpha)
{
    const auto joined = [] (float width)
    {
        return juce::PathStrokeType(width, juce::PathStrokeType::curved,
                                    juce::PathStrokeType::rounded);
    };
    g.setColour(colour.withAlpha(0.16f * alpha));
    g.strokePath(path, joined(7.0f));
    g.setColour(colour.withAlpha(alpha));
    g.strokePath(path, joined(1.8f));
}

// --- The picture tube -------------------------------------------------------
//
// An oscillator draws its wave on a small CRT: a black surround, a bowed glass
// face lit from the middle, scanlines, and a bezel that glows where the tube's
// edge catches its own light.

inline constexpr float crtBezel = 6.0f;

// The glass face, inset from the well so the bezel glow has somewhere to sit.
inline juce::Rectangle<float> crtFace(juce::Rectangle<int> well)
{
    return well.toFloat().reduced(crtBezel);
}

// The silhouette of a picture tube: corners pulled well in, edges bowed gently
// outward. It is a path rather than a rounded rectangle because the bow is what
// makes it read as glass instead of a box.
inline juce::Path crtPath(juce::Rectangle<float> face)
{
    const auto corner = juce::jmin(face.getHeight() * 0.34f, face.getWidth() * 0.06f, 18.0f);
    // A quadratic's control point pulls the curve half way toward it, so the
    // glass bulges by half of this.
    const auto bow = 4.0f;
    const auto l = face.getX(), t = face.getY(), r = face.getRight(), b = face.getBottom();
    const auto cx = face.getCentreX(), cy = face.getCentreY();

    juce::Path tube;
    tube.startNewSubPath(l + corner, t);
    tube.quadraticTo(cx, t - bow, r - corner, t);
    tube.quadraticTo(r, t, r, t + corner);
    tube.quadraticTo(r + bow, cy, r, b - corner);
    tube.quadraticTo(r, b, r - corner, b);
    tube.quadraticTo(cx, b + bow, l + corner, b);
    tube.quadraticTo(l, b, l, b - corner);
    tube.quadraticTo(l - bow, cy, l, t + corner);
    tube.quadraticTo(l, t, l + corner, t);
    tube.closeSubPath();
    return tube;
}

inline void drawCrtScreen(juce::Graphics& g, juce::Rectangle<int> well,
                          juce::Colour phosphor, float alpha)
{
    const auto face = crtFace(well);
    if (face.getWidth() <= 1.0f || face.getHeight() <= 1.0f) return;
    const auto tube = crtPath(face);
    const auto cx = face.getCentreX(), cy = face.getCentreY();

    // Nothing is painted around the tube. The corners the bowed glass leaves
    // behind would otherwise be a darker patch sitting on the panel, so the
    // tube is left to float on whatever is already there and they disappear.

    {
        juce::Graphics::ScopedSaveState clip(g);
        g.reduceClipRegion(tube);

        g.setColour(juce::Colour(0xff01030c));
        g.fillPath(tube);

        // The tube's own light, brightest at the middle of the face and falling
        // away toward the corners the way a phosphor screen does.
        const auto radius = juce::jmax(face.getWidth(), face.getHeight()) * 0.60f;
        juce::ColourGradient bloom(phosphor.withAlpha(0.34f * alpha), cx, cy,
                                   phosphor.withAlpha(0.0f), cx + radius, cy, true);
        bloom.addColour(0.45, phosphor.withAlpha(0.14f * alpha));
        g.setGradientFill(bloom);
        g.fillPath(tube);

        // The face is much wider than it is tall, so the bloom alone leaves the
        // top and bottom lit. Darken them back down for a vignette on all sides.
        const auto shade = juce::Colour(0xdd000208);
        juce::ColourGradient top(shade, cx, face.getY(), juce::Colours::transparentBlack, cx, cy, false);
        g.setGradientFill(top);
        g.fillPath(tube);
        juce::ColourGradient bottom(shade, cx, face.getBottom(), juce::Colours::transparentBlack, cx, cy, false);
        g.setGradientFill(bottom);
        g.fillPath(tube);

        // Scanlines. Three pixels apart is close enough to read as a raster
        // without turning into a moire against the trace.
        g.setColour(juce::Colour(0xff000000).withAlpha(0.22f));
        for (auto y = face.getY(); y < face.getBottom(); y += 3.0f)
            g.fillRect(face.getX(), y, face.getWidth(), 1.0f);

        // The zero axis, as faint as a graticule etched on the glass.
        g.setColour(phosphor.withAlpha(0.16f * alpha));
        g.fillRect(face.getX() + 4.0f, cy, face.getWidth() - 8.0f, 1.0f);
    }

    // The bezel: widest and faintest first, so the edge blooms outward.
    g.setColour(phosphor.withAlpha(0.10f * alpha));
    g.strokePath(tube, juce::PathStrokeType(7.0f));
    g.setColour(phosphor.withAlpha(0.22f * alpha));
    g.strokePath(tube, juce::PathStrokeType(3.5f));
    g.setColour(phosphor.withAlpha(0.80f * alpha));
    g.strokePath(tube, juce::PathStrokeType(1.4f));
}

// A phosphor trace: a wide halo, a soft body, and a core hot enough to have
// burnt toward white, which is what makes a CRT line look lit rather than drawn.
inline void strokePhosphor(juce::Graphics& g, const juce::Path& path,
                           juce::Colour phosphor, float alpha)
{
    g.setColour(phosphor.withAlpha(0.16f * alpha));
    g.strokePath(path, juce::PathStrokeType(9.0f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
    g.setColour(phosphor.withAlpha(0.55f * alpha));
    g.strokePath(path, juce::PathStrokeType(3.4f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
    g.setColour(phosphor.interpolatedWith(juce::Colours::white, 0.7f).withAlpha(alpha));
    g.strokePath(path, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
}

inline constexpr float displayCorner = 3.0f;

// The well's own outline, for a trace to be clipped against. A curve is drawn
// the full width of its display and cut off here, so it reaches both edges and
// runs behind the frame rather than stopping short of it with a margin of dead
// space at each end.
inline juce::Path displayClip(juce::Rectangle<int> area)
{
    juce::Path clip;
    clip.addRoundedRectangle(area.toFloat(), displayCorner);
    return clip;
}

inline void drawDisplayWell(juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto box = area.toFloat();
    g.setColour(juce::Colour(0xff0b0e18));
    g.fillRoundedRectangle(box, displayCorner);
    g.setColour(line.withAlpha(0.6f));
    g.drawRoundedRectangle(box, displayCorner, 1.0f);
    g.setColour(line.withAlpha(0.45f));
    g.drawHorizontalLine(area.getCentreY(), box.getX(), box.getRight());
}

// The oscillator's own table on its tube. The table is handed in rather than
// looked up, because each oscillator now has one of its own and the panel has
// to draw the one that oscillator is actually reading.
inline void drawWaveform(juce::Graphics& g, juce::Rectangle<int> area, const theta::forge::WavetableEdit& table,
                         float position, juce::Colour colour, float alpha)
{
    const auto face = crtFace(area);
    // Full width of the glass: the trace runs off both edges and is cut by the
    // tube, the way a scope's is, rather than stopping short inside it.
    const auto box = face.reduced(0.0f, 5.0f).withSizeKeepingCentre(face.getWidth(),
                                                                    face.getHeight() * 0.88f);
    const auto path = wavePath(box, [&table, position] (float phase)
                               { return table.sample(position, phase); }, 220);
    // Clipped to the glass, so the halo stops at the bezel rather than spilling
    // over the tube's edge.
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(crtPath(face));
    fillWaveArea(g, box, path, colour, alpha);
    strokePhosphor(g, path, colour, alpha);
}

// --- The envelope -----------------------------------------------------------

// Envelope stages, matching Core's ordering.
enum class Stage { idle, attack, decay, sustain, release };

// How long each stage actually lasts, which is not always what its knob says.
// Core's decay ends the instant it begins when sustain is full — there is
// nothing to fall to — and its release ends at once when sustain is nothing.
// The picture has to agree, or a patch holding a flat note draws a decay and a
// release it will never play.
struct EnvelopeTimes
{
    float attack = 0.0f, decay = 0.0f, release = 0.0f;
    float total() const { return attack + decay + release; }
};

inline EnvelopeTimes envelopeTimes(float attack, float decay, float sustain, float release)
{
    const auto held = juce::jlimit(0.0f, 1.0f, sustain);
    return {juce::jmax(0.0f, attack),
            held < 1.0f ? juce::jmax(0.0f, decay) : 0.0f,
            held > 0.0f ? juce::jmax(0.0f, release) : 0.0f};
}

// The time the display's width spans, and the spacing of the marks across it.
struct EnvelopeAxis
{
    float seconds = 1.0f;
    float mark = 0.25f;
};

// The windows the display can be set to, shortest first, each with the mark
// spacing that divides it into something readable.
//
// The window is fixed and chosen by the player, never fitted to the envelope.
// Fitting is what this display used to do, and it is the bug: a shape stretched
// to the full width looks identical at 50 ms and at 4 s, so the one thing an
// envelope display exists to show — how long the stages last — was the one thing
// it could not show. A fixed window makes the axis a ruler, so turning a knob
// moves the shape against it instead of rescaling it underneath; and an envelope
// longer than the window runs off the right-hand edge and is cut by the frame,
// which is itself the reading that it is longer than three seconds.
inline constexpr EnvelopeAxis envelopeZooms[] = {
    {0.05f, 0.01f}, {0.1f, 0.025f}, {0.25f, 0.05f}, {0.5f, 0.1f}, {1.0f, 0.25f},
    {1.5f, 0.5f},   {3.0f, 1.0f},   {6.0f, 1.0f},   {10.0f, 2.0f}, {16.0f, 4.0f}};
inline constexpr int envelopeZoomCount = sizeof(envelopeZooms) / sizeof(envelopeZooms[0]);

// Three seconds, which is what Serum opens on. Long enough to hold the
// envelopes most patches actually use, and short enough that a percussive one
// still reads as the sliver it is.
inline constexpr int envelopeDefaultZoom = 6;

inline EnvelopeAxis envelopeAxis(int zoom)
{
    return envelopeZooms[juce::jlimit(0, envelopeZoomCount - 1, zoom)];
}

// Where each corner of an envelope lands inside a box. Pure geometry, so what
// the picture claims about time can be checked without a Graphics.
//
// There is no sustain plateau. Sustain is a level held for as long as the key
// is, not a duration, so giving it a slice of the width tore a gap in the time
// axis and left every stage after it sitting somewhere it does not belong.
// Decay ends on the sustain level and release starts from it, as Serum draws it.
struct EnvelopeShape
{
    juce::Rectangle<float> box;
    EnvelopeAxis axis;
    float attackX = 0.0f;  // the peak
    float decayX = 0.0f;   // the sustain level, and where release begins
    float endX = 0.0f;     // silence
    float floorY = 0.0f, peakY = 0.0f, sustainY = 0.0f;

    float xFor(float seconds) const
    {
        return box.getX() + seconds / axis.seconds * box.getWidth();
    }
};

inline EnvelopeShape envelopeShape(juce::Rectangle<float> box, float attack, float decay,
                                   float sustain, float release, int zoom = envelopeDefaultZoom)
{
    const auto times = envelopeTimes(attack, decay, sustain, release);
    EnvelopeShape shape;
    shape.box = box;
    shape.axis = envelopeAxis(zoom);
    shape.floorY = box.getBottom();
    shape.peakY = box.getY();
    shape.sustainY = juce::jmap(juce::jlimit(0.0f, 1.0f, sustain), shape.floorY, shape.peakY);
    shape.attackX = shape.xFor(times.attack);
    shape.decayX = shape.xFor(times.attack + times.decay);
    shape.endX = shape.xFor(times.total());
    return shape;
}

// A time written the way the knobs write theirs, so "250 ms" on the axis and
// "250 ms" under a knob are the same number in the same words.
inline juce::String envelopeTimeText(float seconds)
{
    if (seconds < 1.0f) return juce::String(juce::roundToInt(seconds * 1000.0f)) + " ms";
    return juce::String(seconds, 2).trimCharactersAtEnd("0").trimCharactersAtEnd(".") + " s";
}

// --- The zoom control -------------------------------------------------------
//
// Geometry first, so the box that is drawn and the box that is clicked cannot
// drift apart — the same rule the knob's modulation ring follows.
//
// A narrow strip of its own down the right-hand side, with the plot shortened
// to make room. Sitting the control *on* the display was tried first and does
// not work: there is no corner a curve cannot reach, and a long attack with a
// short tail peaks under exactly the corner it wants.

inline constexpr int envelopeZoomStripWidth = 22;
inline constexpr int envelopeZoomStripGap = 5;
inline constexpr int envelopeZoomButton = 18;

inline juce::Rectangle<int> envelopeZoomStrip(juce::Rectangle<int> display)
{
    return display.removeFromRight(envelopeZoomStripWidth);
}

// What is left of the display once the strip has taken its share: the box the
// axis is measured across and the curve is drawn in.
inline juce::Rectangle<int> envelopePlotBounds(juce::Rectangle<int> display)
{
    return display.withTrimmedRight(envelopeZoomStripWidth + envelopeZoomStripGap);
}

// Plus magnifies, as it does on a map or in a page view: it shortens the window
// so the shape grows. The span between the buttons is a readout of where that
// has got to, not a number the buttons add to — which is why plus makes it go
// down. The wheel over the plot runs the same way round, up for in.
inline juce::Rectangle<int> envelopeZoomIn(juce::Rectangle<int> display)
{
    return envelopeZoomStrip(display).removeFromTop(envelopeZoomButton);
}

inline juce::Rectangle<int> envelopeZoomOut(juce::Rectangle<int> display)
{
    return envelopeZoomStrip(display).removeFromBottom(envelopeZoomButton);
}

inline void drawEnvelopeZoom(juce::Graphics& g, juce::Rectangle<int> display, int zoom, float alpha)
{
    const auto strip = envelopeZoomStrip(display);
    g.setColour(juce::Colour(0xff0b0e18));
    g.fillRoundedRectangle(strip.toFloat(), displayCorner);
    g.setColour(line.withAlpha(0.6f));
    g.drawRoundedRectangle(strip.toFloat(), displayCorner, 1.0f);

    const auto button = [&] (juce::Rectangle<int> box, const char* glyph, bool enabled)
    {
        g.setColour(text.withAlpha((enabled ? 0.95f : 0.22f) * alpha));
        g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        g.drawText(glyph, box, juce::Justification::centred);
    };
    button(envelopeZoomIn(display), "+", zoom > 0);
    button(envelopeZoomOut(display), "-", zoom < envelopeZoomCount - 1);

    // The span reads up the strip, because 22px of width holds "250MS" only on
    // its side. Rotated about the middle of the gap the two buttons leave, with
    // the box turned on its side to match so the text still centres in it.
    const auto middle = strip.withTrimmedTop(envelopeZoomButton)
                            .withTrimmedBottom(envelopeZoomButton)
                            .toFloat();
    juce::Graphics::ScopedSaveState rotated(g);
    g.addTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi,
                                                   middle.getCentreX(), middle.getCentreY()));
    g.setColour(mutedText.withAlpha(0.9f * alpha));
    g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    g.drawText(envelopeTimeText(envelopeAxis(zoom).seconds).toUpperCase().removeCharacters(" "),
               juce::Rectangle<float>(middle.getHeight(), middle.getWidth())
                   .withCentre(middle.getCentre()),
               juce::Justification::centred);
}

// The second marks behind an envelope, each labelled with the time it stands
// for. They are what turns the shape into a measurement: without them a fixed
// window is one more arbitrary stretch.
inline void drawEnvelopeGrid(juce::Graphics& g, juce::Rectangle<float> box, EnvelopeAxis axis)
{
    const auto marks = juce::roundToInt(axis.seconds / axis.mark);
    g.setFont(juce::FontOptions(9.0f));
    for (int i = 1; i < marks; ++i)
    {
        const auto at = axis.mark * static_cast<float>(i);
        const auto x = box.getX() + at / axis.seconds * box.getWidth();
        g.setColour(line.withAlpha(0.5f));
        g.drawVerticalLine(juce::roundToInt(x), box.getY(), box.getBottom());
        g.setColour(mutedText.withAlpha(0.55f));
        g.drawText(envelopeTimeText(at),
                   juce::Rectangle<float>(x + 3.0f, box.getBottom() - 12.0f, 44.0f, 11.0f).toNearestInt(),
                   juce::Justification::centredLeft);
    }
}

// The current ADSR against a time axis, with a playhead showing where a
// sounding note has reached. The playhead's height is the envelope's real
// value, so a note released during its attack visibly falls from the level it
// actually got to rather than from the sustain line.
inline void drawEnvelope(juce::Graphics& g, juce::Rectangle<int> area, float attack, float decay,
                         float sustain, float release, juce::Colour colour, float alpha,
                         Stage stage = Stage::idle, float level = 0.0f,
                         int zoom = envelopeDefaultZoom)
{
    // Drawn before the plot takes the clip below, because the strip is outside
    // it: the zoom control is beside the well, not on it.
    drawEnvelopeZoom(g, area, zoom, alpha);

    // Clipped to the well and drawn its full width, so the curve starts against
    // the left edge and anything past the end of the window is cut off by the
    // frame rather than squeezed back inside it.
    const auto plot = envelopePlotBounds(area);
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(displayClip(plot));

    const auto box = plot.toFloat().reduced(0.0f, 9.0f);
    const auto shape = envelopeShape(box, attack, decay, sustain, release, zoom);
    drawEnvelopeGrid(g, box, shape.axis);

    // Straight segments, because Core's stages are straight: each one adds or
    // subtracts a fixed amount per sample. A drawn curve would be a shape the
    // engine never plays.
    juce::Path path;
    path.startNewSubPath(box.getX(), shape.floorY);
    path.lineTo(shape.attackX, shape.peakY);
    path.lineTo(shape.decayX, shape.sustainY);
    path.lineTo(shape.endX, shape.floorY);

    // Filled down to silence rather than to the middle of the well: an envelope
    // runs from nothing to full, so its baseline is the floor, not the centre.
    auto under = path;
    under.lineTo(box.getX(), shape.floorY);
    under.closeSubPath();
    g.setGradientFill({colour.withAlpha(0.30f * alpha), box.getCentreX(), shape.peakY,
                       colour.withAlpha(0.04f * alpha), box.getCentreX(), shape.floorY, false});
    g.fillPath(under);

    strokeGlow(g, path, colour, alpha);

    // A handle on every corner, so the three stages stay tellable apart where
    // one of them is short enough to leave almost no segment to see.
    for (const auto& node : {juce::Point<float>(shape.attackX, shape.peakY),
                             juce::Point<float>(shape.decayX, shape.sustainY),
                             juce::Point<float>(shape.endX, shape.floorY)})
    {
        const auto dot = juce::Rectangle<float>(6.5f, 6.5f).withCentre(node);
        g.setColour(panel.withAlpha(alpha));
        g.fillEllipse(dot);
        g.setColour(colour.withAlpha(alpha));
        g.drawEllipse(dot, 1.6f);
    }

    if (stage == Stage::idle) return;

    // Where along the drawn shape the note has reached. Each stage maps its own
    // progress onto its own segment; sustain is the corner decay ends on, so a
    // held note parks there and release carries on from it without a jump.
    const auto held = juce::jlimit(0.0f, 1.0f, sustain);
    auto x = box.getX();
    switch (stage)
    {
        case Stage::attack:
            x = juce::jmap(juce::jlimit(0.0f, 1.0f, level), box.getX(), shape.attackX);
            break;
        case Stage::decay:
            x = juce::jmap(juce::jlimit(0.0f, 1.0f, held < 1.0f ? (1.0f - level) / (1.0f - held) : 1.0f),
                           shape.attackX, shape.decayX);
            break;
        case Stage::sustain:
            x = shape.decayX;
            break;
        case Stage::release:
            x = juce::jmap(juce::jlimit(0.0f, 1.0f, held > 0.0f ? 1.0f - level / held : 1.0f),
                           shape.decayX, shape.endX);
            break;
        case Stage::idle:
            break;
    }
    const auto y = juce::jmap(juce::jlimit(0.0f, 1.0f, level), shape.floorY, shape.peakY);

    g.setColour(colour.withAlpha(0.35f * alpha));
    g.drawVerticalLine(juce::roundToInt(x), y, shape.floorY);
    g.setColour(juce::Colours::white.withAlpha(0.9f * alpha));
    g.fillEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre({x, y}));
    g.setColour(colour);
    g.fillEllipse(juce::Rectangle<float>(4.0f, 4.0f).withCentre({x, y}));
}

inline const char* stageName(Stage stage)
{
    switch (stage)
    {
        case Stage::attack: return "ATTACK";
        case Stage::decay: return "DECAY";
        case Stage::sustain: return "SUSTAIN";
        case Stage::release: return "RELEASE";
        case Stage::idle: break;
    }
    return "AMP";
}

// LFO 1's shape, drawn as exactly one cycle with an indicator riding it. One
// cycle rather than several, and rather than a count that grows with the rate,
// so the width of the display is the length of the cycle: the indicator then
// sweeps the whole thing and its position is the phase, read directly. Drawing
// two cycles left the indicator stuck in the left-hand half, because a phase
// only ever covers one of them. The rate is written in the module header, so the
// picture does not have to carry it too.
//
// The indicator is the engine's own running phase, not an animation timed in the
// editor, so it cannot drift away from what is being heard.
inline void drawLfo(juce::Graphics& g, juce::Rectangle<int> area, theta::forge::LfoShape shape,
                    float phase, float held, juce::Colour colour, float alpha)
{
    // Clipped to the well and drawn its full width, so one cycle spans edge to
    // edge and the curve meets both sides instead of floating inside a margin.
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(displayClip(area));

    const auto box = area.toFloat().reduced(0.0f, 8.0f);
    const auto plot = [&] (float at) { return box.getCentreY() - at * box.getHeight() * 0.42f; };

    juce::Path path;
    if (shape == theta::forge::LfoShape::sampleHold)
    {
        // The step being held, flat across the whole display, because that is
        // genuinely what this shape is putting out right now: one value, held.
        // The jump is seen rather than drawn — the line lifts to a new height
        // each time the cycle turns over, which is what sample and hold looks
        // like when you watch it. Drawing a row of invented steps instead would
        // put the indicator on a curve the voice is not reading.
        const auto y = plot(juce::jlimit(-1.0f, 1.0f, held));
        path.startNewSubPath(box.getX(), y);
        path.lineTo(box.getRight(), y);
    }
    else
    {
        constexpr int points = 240;
        for (int i = 0; i <= points; ++i)
        {
            // The full closed interval, so a saw reaches the top of its ramp and
            // a square its second half before the cycle ends.
            const auto along = static_cast<float>(i) / static_cast<float>(points);
            const auto at = theta::forge::lfoWave(shape, along, held);
            const auto x = box.getX() + along * box.getWidth();
            if (i == 0) path.startNewSubPath(x, plot(at)); else path.lineTo(x, plot(at));
        }
    }
    strokeGlow(g, path, colour, alpha);

    // One cycle wide, so the phase is the position along it directly and the
    // indicator always sits on the curve it is drawn over.
    const auto at = juce::jlimit(0.0f, 1.0f, phase);
    const auto x = box.getX() + at * box.getWidth();
    const auto y = plot(juce::jlimit(-1.0f, 1.0f, theta::forge::lfoWave(shape, phase, held)));
    g.setColour(colour.withAlpha(0.3f * alpha));
    g.drawVerticalLine(juce::roundToInt(x), box.getY(), box.getBottom());
    g.setColour(juce::Colours::white.withAlpha(0.9f * alpha));
    g.fillEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre({x, y}));
    g.setColour(colour);
    g.fillEllipse(juce::Rectangle<float>(4.0f, 4.0f).withCentre({x, y}));
}

// The pedal shell: a raised body, an accent cap, and a header strip reserved
// for the title. Knob labels are laid out below that strip, which is why they
// no longer collide with it.
inline void drawModuleShell(juce::Graphics& g, juce::Rectangle<int> area, const Module& module, bool on,
                            const juce::String& detailOverride = {})
{
    const auto box = area.toFloat();
    const auto accent = accentFor(module);
    const auto alpha = on ? 1.0f : 0.45f;

    g.setColour(panelRaised.withAlpha(on ? 1.0f : 0.55f));
    g.fillRoundedRectangle(box, 5.0f);
    g.setColour(line.withAlpha(alpha));
    g.drawRoundedRectangle(box, 5.0f, 1.0f);
    g.setColour(accent.withAlpha(alpha));
    g.fillRoundedRectangle(box.withHeight(3.0f).reduced(1.0f, 0.0f), 2.0f);

    auto header = area.withHeight(headerHeight).reduced(10, 0);
    // The enable LED sits at the far left of the header; leave room for it.
    if (module.enableId != nullptr) header.removeFromLeft(headerHeight);
    // A source module's drag handle carries the module's name, so the title is
    // not drawn again beside it.
    if (module.handleSource == 0)
    {
        g.setColour(text.withAlpha(alpha));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(module.title, header, juce::Justification::centredLeft);
    }
    const auto detail = detailOverride.isNotEmpty() ? detailOverride : juce::String(module.detail);
    if (detail.isNotEmpty())
    {
        g.setColour(mutedText.withAlpha(alpha));
        g.setFont(juce::FontOptions(9.0f));
        g.drawText(detail, header, juce::Justification::centredRight);
    }
}

// The FORGE wordmark, drawn at its own aspect ratio against the left edge of
// the area given. ImageCache keeps the decoded PNG, so a repaint is a blit.
inline void drawWordmark(juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto logo = juce::ImageCache::getFromMemory(BinaryData::forge_logo_png,
                                                      BinaryData::forge_logo_pngSize);
    if (logo.isNull())
    {
        // Binary data missing is a build fault, not a runtime state, but the
        // panel should still name itself rather than show a gap.
        g.setColour(text);
        g.setFont(juce::FontOptions(30.0f, juce::Font::bold));
        g.drawText("FORGE", area.toNearestInt(), juce::Justification::centredLeft);
        return;
    }

    const auto width = area.getHeight() * (float) logo.getWidth() / (float) logo.getHeight();
    juce::Graphics::ScopedSaveState state(g);
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.drawImage(logo, area.withWidth(juce::jmin(width, area.getWidth())),
                juce::RectanglePlacement::stretchToFit);
}

inline void drawBackdrop(juce::Graphics& g, juce::Rectangle<int> componentBounds)
{
    const auto bounds = componentBounds.toFloat();
    juce::ColourGradient background(juce::Colour(0xff090b13), 0.0f, 0.0f,
                                    juce::Colour(0xff17182a), bounds.getRight(), bounds.getBottom(), false);
    g.setGradientFill(background);
    g.fillRect(bounds);
    // A hairline just inside the window edge. The lit strips that used to run
    // down both sides are gone: they cost the modules most of the margin, and
    // the panel already has plenty of light in it.
    g.setColour(line);
    g.drawRoundedRectangle(bounds.reduced(windowMargin * 0.5f), 3.0f, 1.0f);

    const auto margin = static_cast<float>(windowMargin);
    drawWordmark(g, {margin, 20.0f, 220.0f, 36.0f});
    g.setColour(signalViolet);
    g.setFont(juce::FontOptions(10.0f));
    g.drawText("SYNTHETIC SIGNAL FORGE // UNIT 01", windowMargin + 2, 54, 280, 14,
               juce::Justification::centredLeft);
}
}
