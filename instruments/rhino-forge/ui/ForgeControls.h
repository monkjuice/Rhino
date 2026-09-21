#pragma once

#include "ForgeDisplays.h"

// The components a module is built out of: the knob and its modulation ring,
// the enable LED, the drag handle, the rocker, the chip, the cards in a
// header, the value bubble, the title-bar buttons and the picker of waves.
namespace rhino::forge::ui
{
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
    //
    // These reach the knobs. They do not reach a stepper: JUCE applies drag
    // sensitivity to rotary sliders, and a stepper is a bar — see below.
    static constexpr int normalDragPixels = 250;
    static constexpr int fineDragPixels = 1400;

    // What a stepper costs per entry, and the longest a whole field may take.
    //
    // A stepper is a bar slider one line tall, and a bar maps the pointer
    // straight onto its own height — so the entire list was crossed by dragging
    // the height of the field. Measured on an eighteen-entry shape field, a
    // five-pixel twitch moved four entries; a tuning field's 201 cents moved
    // fifty-seven. Drag sensitivity does not help, because JUCE spends it on
    // rotary sliders and this is not one, which is why the bar owns its own
    // drag below rather than asking for a bigger number.
    //
    // The cap is what keeps a long numeric field reachable: twenty pixels an
    // entry is the right weight for a list of shapes and would be four thousand
    // pixels for a field of cents, so a field longer than the cap shares the cap
    // out instead. The fine modifier stretches whichever of the two applies.
    static constexpr int steppedDragPixelsPerEntry = 20;
    static constexpr int maxSteppedDragTravel = 600;
    static constexpr int fineSteppedDragMultiplier = 5;

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

    // "The ring" is whichever shape this control wears its modulation in: the
    // band outside a knob's rim, or the strip along the foot of a numeric
    // field. Everything past this point — the drag, the double-click, the
    // cursor — is the same gesture either way.
    bool overRing(juce::Point<int> position)
    {
        if (!ringDraggable) return false;
        const auto area = ringArea();
        return isBar() ? onModBar(area.toFloat().reduced(1.0f), position)
                       : onModRing(area, position);
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
        if (steppedField())
        {
            valueAtDragStart = getValue();
            // A press on a field that steps through a list must not move it.
            // Left to itself a bar slider jumps to wherever the pointer landed,
            // so clicking one to read it would change it.
            setSliderSnapsToMousePosition(false);
        }
        juce::Slider::mouseDown(event);
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (draggingRing)
        {
            dragRing(event);
            return;
        }
        if (steppedField())
        {
            const auto perEntry = steppedDragPixels(fineDrag(event));
            const auto moved = -static_cast<double>(event.getDistanceFromDragStartY()) / perEntry;
            setValue(valueAtDragStart + moved * getInterval(), juce::sendNotificationSync);
            return;
        }
        juce::Slider::mouseDrag(event);
    }

    void dragRing(const juce::MouseEvent& event)
    {
        // Up is more, down is less, and the whole bipolar range is 200 pixels
        // of travel, so a depth can be crossed from one sign to the other
        // without letting go. A modifier stretches that the same way it
        // stretches a knob's own travel.
        const auto pixels = fineDrag(event) ? 560.0f : 100.0f;
        const auto moved = -static_cast<float>(event.getDistanceFromDragStartY()) / pixels;
        if (onRingDrag) onRingDrag(juce::jlimit(-1.0f, 1.0f, depthAtDragStart + moved));
    }

    // Whether this control is one of the fields that steps through a list. A
    // stepper is the only thing drawn as a vertical bar; the matrix's amount is
    // a horizontal one and a fader is a genuine slider with a thumb, and both
    // of those are read as a position and so are dragged to one.
    bool steppedField() const
    {
        return getSliderStyle() == juce::Slider::LinearBarVertical && getInterval() > 0.0
            && getMaximum() > getMinimum();
    }

    double steppedDragPixels(bool fine) const
    {
        const auto entries = juce::jmax(1.0, (getMaximum() - getMinimum()) / getInterval());
        const auto perEntry = juce::jmin(static_cast<double>(steppedDragPixelsPerEntry),
                                         maxSteppedDragTravel / entries);
        return juce::jmax(1.0, fine ? perEntry * fineSteppedDragMultiplier : perEntry);
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
    // Where a stepper stood when the hand went down, because its drag is
    // counted from there rather than from the pointer's position in the field.
    double valueAtDragStart = 0.0;

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

    // Set only on the modules that have a colour to choose. Right-clicking the
    // LED is how an oscillator's panel colour is set: the LED is already the
    // module's identity on the panel — the one thing wearing its colour at all
    // times — so it is where you would go looking to change it.
    std::function<void()> onColourMenu;

    // juce::Button triggers on any mouse button, so the menu gesture has to be
    // taken out of both halves of the click or the right-click would toggle the
    // module off on its way to opening the menu. The flag rather than a second
    // test of the modifiers, because what is down at the release is not
    // reliably what was down at the press.
    void mouseDown(const juce::MouseEvent& event) override
    {
        menuGesture = event.mods.isPopupMenu() && onColourMenu != nullptr;
        if (menuGesture) { onColourMenu(); return; }
        juce::Button::mouseDown(event);
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        if (menuGesture) { menuGesture = false; return; }
        juce::Button::mouseUp(event);
    }

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

private:
    bool menuGesture = false;
};

// The tag beside a source that you drag onto a knob. It is deliberately not
// the knob or the macro dial itself: dragging those has to keep meaning "turn
// this", so the grab handle is a separate thing sitting next to the control.
//
// Not final: a macro's handle is a plate of two storeys rather than a tag, and
// it is a SourceHandle rather than a type of its own because the drag is the
// editor's and the editor finds what is being dragged by this type. See
// ForgeMacroCell.h.
class SourceHandle : public juce::Component,
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
    // A handle in a module header is drawn as a card hanging from the module's
    // top edge, carrying the lit strip across itself: it is the module's name,
    // so it should look like part of the module. A macro's handle is not — it
    // stands beside a knob, where there is no edge to hang from.
    bool card = false;

    void paint(juce::Graphics& g) override
    {
        const auto lit = dragging || isMouseOver();
        const auto fill = lit ? accent.withAlpha(0.3f) : juce::Colour(0xff0d1120);
        auto text = getLocalBounds().toFloat();

        if (card)
        {
            const auto area = getLocalBounds().toFloat().reduced(0.6f, 0.0f);
            const auto outline = cardOutline(area);
            g.setColour(fill);
            g.fillPath(outline);
            g.setColour(accent.withAlpha(lit ? 1.0f : 0.65f));
            g.strokePath(outline, juce::PathStrokeType(1.0f));
            g.setColour(accent.withAlpha(lit ? 1.0f : 0.85f));
            g.fillRect(area.withHeight(moduleEdgeHeight));
            text = area.withTrimmedTop(moduleEdgeHeight);
        }
        else
        {
            const auto area = getLocalBounds().toFloat().reduced(0.6f);
            g.setColour(fill);
            g.fillRoundedRectangle(area, 3.0f);
            g.setColour(accent.withAlpha(lit ? 1.0f : 0.65f));
            g.drawRoundedRectangle(area, 3.0f, 1.0f);
            text = area;
        }

        g.setColour(lit ? juce::Colours::white : accent);
        g.setFont(panelFont(Face::emphasis, juce::jmin(13.0f, text.getHeight() * 0.72f)));
        g.drawText(caption, text, juce::Justification::centred);
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
        g.setFont(panelFont(Face::emphasis, controlLabelSize));
        g.drawText(getName(), area, juce::Justification::centred);
    }
};

class BankCard final : public juce::Button
{
public:
    explicit BankCard(const juce::String& label) : juce::Button(label) { setClickingTogglesState(true); }

    juce::Colour accent = signalViolet;

    void paintButton(juce::Graphics& g, bool highlighted, bool) override
    {
        const auto area = getLocalBounds().toFloat().reduced(0.6f, 0.0f);
        const auto on = getToggleState();
        const auto outline = cardOutline(area);
        g.setColour(on ? accent.withAlpha(0.26f) : juce::Colour(0xff0b0e18));
        g.fillPath(outline);
        g.setColour((on ? accent : line).withAlpha(highlighted ? 1.0f : 0.8f));
        g.strokePath(outline, juce::PathStrokeType(1.0f));
        // The module's own lit edge, continued across the card. A bank that is
        // not showing keeps a dimmed length of it, so the row of cards still
        // reads as one strip.
        g.setColour(accent.withAlpha(on ? 1.0f : 0.3f));
        g.fillRect(area.withHeight(moduleEdgeHeight));
        g.setColour(on ? juce::Colours::white : mutedText);
        g.setFont(panelFont(Face::emphasis, 12.0f));
        g.drawText(getName(), area.withTrimmedTop(moduleEdgeHeight), juce::Justification::centred);
    }
};

// What a knob says while it is being turned. Nothing on the panel prints a
// value at rest — a readout under every knob costs the panel a line of height
// each and is read perhaps once a session — so the value comes to the hand
// instead: the control's name, the value under it, beside the knob for as long
// as it is moving and a moment after.
class ValueBubble final : public juce::Component
{
public:
    ValueBubble()
    {
        setInterceptsMouseClicks(false, false);
        setVisible(false);
    }

    juce::Colour accent = electricBlue;
    juce::String caption, reading;

    static constexpr int captionHeight = 13;
    static constexpr int readingHeight = 21;
    static constexpr int padding = 9;

    // Wide enough for the longer of the two lines, at the size each is drawn,
    // with a few pixels over the measurement: a box sized to exactly the width
    // a string measures still comes out ellipsised once it is drawn into it.
    static constexpr int slack = 10;

    int widthFor() const
    {
        const auto captionFont = panelFont(Face::label, 10.0f);
        const auto readingFont = panelFont(Face::reading, 16.0f);
        return juce::jmax(64, juce::roundToInt(juce::jmax(juce::GlyphArrangement::getStringWidth(captionFont, caption),
                                                          juce::GlyphArrangement::getStringWidth(readingFont, reading)))
                                  + padding * 2 + slack);
    }

    static constexpr int heightFor() { return captionHeight + readingHeight + padding; }

    void paint(juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(juce::Colour(0xf205070e));
        g.fillRoundedRectangle(area, 5.0f);
        g.setColour(accent.withAlpha(0.85f));
        g.drawRoundedRectangle(area, 5.0f, 1.0f);
        g.setColour(accent.withAlpha(0.9f));
        g.fillRoundedRectangle(area.withHeight(moduleEdgeHeight).reduced(2.0f, 0.0f), 1.5f);

        auto body = getLocalBounds().reduced(padding, 0).withTrimmedTop(padding / 2);
        g.setColour(mutedText);
        g.setFont(panelFont(Face::label, 10.0f));
        g.drawText(caption, body.removeFromTop(captionHeight), juce::Justification::centred);
        g.setColour(text);
        g.setFont(panelFont(Face::reading, 16.0f));
        g.drawText(reading, body.removeFromTop(readingHeight), juce::Justification::centred);
    }
};

// Preset buttons sit flush in the black inset of the maker assembly.
class PresetButton final : public juce::TextButton
{
public:
    using juce::TextButton::TextButton;

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto box = getLocalBounds().toFloat().reduced(0.7f);
        drawWell(g, box, juce::Colour(down ? 0xff151c26 : 0xff070b10), 1.0f, 2);
        g.setColour((highlighted ? electricBlue : juce::Colour(0xff657389)).withAlpha(0.85f));
        g.strokePath(chamferedPath(box, 2), juce::PathStrokeType(0.8f));
        g.setColour(rhino::forge::ui::text.withAlpha(0.85f));
        g.setFont(panelFont(Face::emphasis, 14));
        g.drawText(getButtonText(), box, juce::Justification::centred);
    }
};

// A tab's lit bottom edge points toward the signal row that follows it.
class PageTab final : public juce::Button
{
public:
    explicit PageTab(const juce::String& label) : juce::Button(label) {}

    juce::Colour accent = electricBlue;

    void paintButton(juce::Graphics& g, bool highlighted, bool) override
    {
        const auto area = getLocalBounds().toFloat().reduced(0.5f);
        const auto on = getToggleState();

        g.setGradientFill(juce::ColourGradient(on ? juce::Colour(0xff163554) : juce::Colour(0xff0c131b),
                                               area.getX(), area.getY(), juce::Colour(0xff04080d),
                                               area.getX(), area.getBottom(), false));
        g.fillRoundedRectangle(area, 2.0f);
        g.setColour((on ? accent : line).withAlpha(on ? 1.0f : (highlighted ? 0.9f : 0.6f)));
        g.drawRoundedRectangle(area, 2.0f, 1.0f);
        if (on)
        {
            for (const auto thickness : {8.0f, 4.0f, 1.6f})
            {
                g.setColour(accent.withAlpha(thickness > 2.0f ? 0.13f : 1.0f));
                g.drawLine(area.getX() + 3, area.getBottom() - 2,
                           area.getRight() - 3, area.getBottom() - 2, thickness);
            }
        }
        // Qualified: Button has a private `text` member of its own that would
        // otherwise win the lookup here.
        g.setColour(on ? rhino::forge::ui::text : mutedText.withAlpha(highlighted ? 1.0f : 0.8f));
        g.setFont(panelFont(Face::header, 13.0f));
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
    g.setFont(panelFont(Face::label, 11.0f));
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
    g.setFont(panelFont(Face::reading, 10.0f));
    g.drawText(juce::String(number), gutter, juce::Justification::centred);
}

// --- A picker of waves --------------------------------------------------------
//
// Choices drawn as the shapes they are rather than named in a field. The sub
// oscillator is the first thing on the panel with a shape that is a choice
// instead of a sweep: POSITION travels a table and reads out the frames it
// passes, but a sub is set to one wave and left there, and six icons say which
// six are on offer and which one is on in a single glance.
//
// The wave is drawn from the same formula the table was generated from, so
// what the picker shows is what the engine reads, band limiting aside.
class WaveGrid final : public juce::Component, public juce::SettableTooltipClient
{
public:
    // One entry per choice, in the engine's own order, and the shape of each at
    // a point in its cycle. Handed in rather than looked up so this stays a
    // picker of waves and not a picker of sub waves.
    int choices = 0;
    std::function<float(int, float)> shapeAt;
    int chosen = 0;
    int columns = 2;
    juce::Colour accent = electricBlue;
    std::function<void(int)> onChoose;

    int rows() const { return choices <= 0 ? 0 : (choices + columns - 1) / columns; }

    // Tiled by dividing the box rather than by multiplying a cell size, so the
    // cells meet exactly and the last column and row end on the edge instead of
    // a pixel or two short of it.
    juce::Rectangle<int> cellBounds(int index) const
    {
        const auto area = getLocalBounds();
        const auto down = rows();
        if (choices <= 0 || down <= 0) return area;
        const auto column = index % columns, row = index / columns;
        const auto x = area.getX() + area.getWidth() * column / columns;
        const auto right = area.getX() + area.getWidth() * (column + 1) / columns;
        const auto y = area.getY() + area.getHeight() * row / down;
        const auto bottom = area.getY() + area.getHeight() * (row + 1) / down;
        return {x, y, right - x, bottom - y};
    }

    void paint(juce::Graphics& g) override
    {
        if (choices <= 0 || !shapeAt) return;
        const auto enabled = isEnabled();
        const auto alpha = enabled ? 1.0f : 0.35f;
        for (int i = 0; i < choices; ++i)
        {
            const auto cell = cellBounds(i).toFloat().reduced(1.5f);
            const auto live = i == chosen;
            const auto lit = enabled && i == hovered;
            g.setColour(live ? accent.withAlpha(alpha * 0.20f)
                             : juce::Colour(0xff0b0e18).withAlpha(alpha));
            g.fillRoundedRectangle(cell, 3.0f);
            g.setColour((live ? accent : line).withAlpha(alpha * (live ? 1.0f : lit ? 0.9f : 0.6f)));
            g.drawRoundedRectangle(cell, 3.0f, 1.0f);

            // Inset hard enough that a square's flat top does not sit on the
            // cell's own border, where the two would read as one line.
            const auto box = cell.reduced(cell.getWidth() * 0.18f, cell.getHeight() * 0.28f);
            const auto shape = i;
            const auto trace = wavePath(box, [this, shape] (float phase) { return shapeAt(shape, phase); }, 128);
            if (live)
            {
                strokeGlow(g, trace, accent, alpha);
            }
            else
            {
                g.setColour((lit ? text : mutedText).withAlpha(alpha * (lit ? 1.0f : 0.8f)));
                g.strokePath(trace, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded));
            }
        }
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (!isEnabled()) return;
        for (int i = 0; i < choices; ++i)
            if (cellBounds(i).contains(event.getPosition()))
            {
                if (i != chosen && onChoose) onChoose(i);
                return;
            }
    }

    void mouseMove(const juce::MouseEvent& event) override { hover(event.getPosition()); }
    void mouseEnter(const juce::MouseEvent& event) override { hover(event.getPosition()); }
    void mouseExit(const juce::MouseEvent&) override { hover({-1, -1}); }

private:
    void hover(juce::Point<int> where)
    {
        auto under = -1;
        for (int i = 0; i < choices; ++i)
            if (cellBounds(i).contains(where)) under = i;
        if (under == hovered) return;
        hovered = under;
        repaint();
    }

    int hovered = -1;
};
}
