#pragma once

#include "ForgeControls.h"

// How a rack is read by colour before a word on it is: each type's colour and
// its mark, the plate that names a slot, the shelf it sits on, and the rows
// of the signal-flow list down the left. What a type does to the signal is
// ForgeFxDsp.h's, and what it draws of itself is ForgeFxDisplay.h's.
namespace rhino::forge::ui
{
// The numbered card that picks which bank a module is showing. It hangs from
// the module's top edge and carries the lit strip across itself, so the six
// LFOs read as six tabs of one module rather than as six buttons parked in its
// header. A routing chip is still a ToggleChip: that one sits inside a control
// row, where there is no edge for a card to hang from.
// --- The effects, by eye ------------------------------------------------------
//
// A rack row is read at a glance or not at all: fixed-height slots and twelve
// controls wide, the thing you need first is *which effect is this*. So each
// type gets a colour and a mark of its own, and the slot wears both — the mark
// on its name plate, the colour on the plate, on its knobs and down its left
// edge.
//
// The colours are chosen to separate on this panel rather than to mean
// anything: eight hues far enough apart to tell apart at a glance, none of them
// the electric blue the signal path uses or the violet the modulators do, so an
// effect never reads as either.
inline juce::Colour fxTypeColour(int type)
{
    switch (type)
    {
        case 1: return juce::Colour(0xff35d6c4);   // REVERB, a cold room
        case 2: return juce::Colour(0xff4a9bff);   // DELAY
        case 3: return juce::Colour(0xffe86bd0);   // CHORUS
        case 4: return juce::Colour(0xffff6f4a);   // DIST, the one that burns
        case 5: return juce::Colour(0xffffc24d);   // EQ
        case 6: return juce::Colour(0xff6fdc5a);   // FILTER
        case 7: return juce::Colour(0xfff09a55);   // COMP
        case 8: return juce::Colour(0xffb58cff);   // PHASER
        default: break;
    }
    return line;                                   // OFF: an empty shelf
}

// The mark on a type's plate. Each one is the shape of what the effect does to
// a signal rather than a symbol standing for its name — a reverb's decaying
// burst, a delay's fading repeats, a distortion's flattened peaks — so the
// plates can be told apart before the words on them are read.
//
// Drawn into whatever box it is given, so the same marks serve a plate at any
// size the panel ends up at.
inline void drawFxMark(juce::Graphics& g, juce::Rectangle<float> box, int type, juce::Colour colour,
                       float alpha)
{
    const auto middle = box.getCentreY();
    const auto height = box.getHeight();
    g.setColour(colour.withAlpha(alpha));

    const auto stem = [&] (float x, float amount, float thickness)
    {
        const auto half = juce::jmax(1.0f, height * 0.5f * amount);
        g.fillRect(x - thickness * 0.5f, middle - half, thickness, half * 2.0f);
    };

    switch (type)
    {
        // A struck impulse and the cloud of reflections behind it, thinning as
        // it goes.
        case 1:
        {
            constexpr int count = 11;
            for (int i = 0; i < count; ++i)
            {
                const auto along = static_cast<float>(i) / (count - 1);
                const auto x = box.getX() + along * box.getWidth();
                g.setColour(colour.withAlpha(alpha * (1.0f - along * 0.75f)));
                stem(x, std::pow(1.0f - along, 1.6f) * 0.9f + 0.06f, 1.6f);
            }
            return;
        }
        // The same shape arriving four times, quieter each time.
        case 2:
        {
            for (int i = 0; i < 4; ++i)
            {
                const auto along = static_cast<float>(i) / 3.0f;
                g.setColour(colour.withAlpha(alpha * (1.0f - along * 0.7f)));
                stem(box.getX() + 3.0f + along * (box.getWidth() - 6.0f),
                     0.85f * std::pow(0.62f, static_cast<float>(i)), 2.4f);
            }
            return;
        }
        // Two copies of one wave, walking apart and back together.
        case 3:
        {
            for (int copy = 0; copy < 2; ++copy)
            {
                juce::Path wave;
                for (int i = 0; i <= 28; ++i)
                {
                    const auto along = static_cast<float>(i) / 28.0f;
                    const auto phase = along * juce::MathConstants<float>::twoPi
                                     + (copy == 0 ? 0.0f : 0.9f);
                    const auto y = middle - std::sin(phase) * height * 0.3f;
                    const auto x = box.getX() + along * box.getWidth();
                    if (i == 0) wave.startNewSubPath(x, y); else wave.lineTo(x, y);
                }
                g.setColour(colour.withAlpha(alpha * (copy == 0 ? 1.0f : 0.5f)));
                g.strokePath(wave, juce::PathStrokeType(1.6f));
            }
            return;
        }
        // A wave with its peaks flattened off, which is the whole of what a
        // clipper does.
        case 4:
        {
            juce::Path wave;
            for (int i = 0; i <= 40; ++i)
            {
                const auto along = static_cast<float>(i) / 40.0f;
                const auto raw = std::sin(along * juce::MathConstants<float>::twoPi * 1.5f) * 2.2f;
                const auto y = middle - juce::jlimit(-1.0f, 1.0f, raw) * height * 0.36f;
                const auto x = box.getX() + along * box.getWidth();
                if (i == 0) wave.startNewSubPath(x, y); else wave.lineTo(x, y);
            }
            g.strokePath(wave, juce::PathStrokeType(1.8f));
            return;
        }
        // A boost and a cut: the two bands the module actually has.
        case 5:
        {
            juce::Path curve;
            for (int i = 0; i <= 40; ++i)
            {
                const auto along = static_cast<float>(i) / 40.0f;
                const auto bump = std::exp(-std::pow((along - 0.28f) * 5.0f, 2.0f));
                const auto dip = std::exp(-std::pow((along - 0.72f) * 5.0f, 2.0f));
                const auto y = middle - (bump - dip) * height * 0.34f;
                const auto x = box.getX() + along * box.getWidth();
                if (i == 0) curve.startNewSubPath(x, y); else curve.lineTo(x, y);
            }
            g.strokePath(curve, juce::PathStrokeType(1.8f));
            return;
        }
        // A corner with resonance on it, falling away past the knee.
        case 6:
        {
            juce::Path curve;
            for (int i = 0; i <= 40; ++i)
            {
                const auto along = static_cast<float>(i) / 40.0f;
                const auto peak = std::exp(-std::pow((along - 0.6f) * 9.0f, 2.0f)) * 0.55f;
                const auto fall = along < 0.6f ? 0.0f : (along - 0.6f) * 2.6f;
                const auto y = middle + (fall - peak) * height * 0.62f - height * 0.12f;
                const auto x = box.getX() + along * box.getWidth();
                if (i == 0) curve.startNewSubPath(x, y); else curve.lineTo(x, y);
            }
            g.strokePath(curve, juce::PathStrokeType(1.8f));
            return;
        }
        // A transfer line bending below unity: what compression does above
        // its threshold.
        case 7:
        {
            juce::Path curve;
            curve.startNewSubPath(box.getX(), box.getBottom());
            curve.lineTo(box.getCentreX(), middle);
            curve.lineTo(box.getRight(), middle - height * 0.2f);
            g.strokePath(curve, juce::PathStrokeType(1.8f));
            return;
        }
        // A row of moving notches, the shape made by dry plus an allpass bank.
        case 8:
        {
            juce::Path curve;
            for (int i = 0; i <= 48; ++i)
            {
                const auto along = static_cast<float>(i) / 48.0f;
                const auto notches = std::abs(std::sin(along * juce::MathConstants<float>::pi * 4.0f));
                const auto y = box.getY() + height * (0.16f + notches * 0.62f);
                const auto x = box.getX() + along * box.getWidth();
                if (i == 0) curve.startNewSubPath(x, y); else curve.lineTo(x, y);
            }
            g.strokePath(curve, juce::PathStrokeType(1.8f));
            return;
        }
        default: break;
    }

    // OFF. A dashed line across an empty shelf, which says "nothing here" in a
    // way an empty box does not.
    for (int i = 0; i < 6; ++i)
    {
        const auto x = box.getX() + (static_cast<float>(i) + 0.5f) * box.getWidth() / 6.0f;
        g.fillRect(x - 4.0f, middle - 0.75f, 8.0f, 1.5f);
    }
}

// A slot's mode field, drawn as whatever the choice in front of you actually is.
//
// The field behind it is one float per slot, because what it steps through
// changes with the type. What it *looks* like should not be one thing for all of
// them: two or three named states are a switch, and eight are a list. So this
// draws itself either way, from the count the type declares.
//
//   two or three  every choice stacked, the live one lit, click one to take it
//   more than that  a name between two arrows: the arrows step, the name opens
//
// Stacked rather than side by side: the names are words of very different
// lengths — OFF against PING-PONG — and a row of them either crops the long
// ones or gives the short ones room they do not need. A column gives every
// choice the same width, which is the field's width, and reads down the way a
// list of alternatives is read.
//
// A stepper you drag served both badly. Dragging to reach "PING-PONG" from
// "NORMAL" is a gesture for a continuous value, and neither of these is one.
class FxSelector final : public juce::Component, public juce::SettableTooltipClient
{
public:
    // Copied rather than pointed at: the type in the slot changes underneath
    // this, and a pointer into the table for the type that *was* there is a
    // dangling read waiting for the next repaint.
    //
    // A vector rather than a fixed array because the fields this serves are not
    // all the same size: a reverb offers two choices and an oscillator's warp
    // offers twenty-six. How many there are is the vector's own length, so a
    // field cannot be told it has more choices than it was given.
    std::vector<const char*> choices;
    int chosen = 0;
    juce::Colour accent = electricBlue;
    std::function<void(int)> onChoose;
    // Opens the list, for a field with too many choices to show at once.
    std::function<void()> onOpenList;

    static constexpr int arrowWidth = 20;

    // The height a single row of the list style wants, centred in a box that is
    // tall enough to stack three. The reference draws this field as the largest
    // thing in its row — the one place on the panel where a choice is read at a
    // glance rather than looked for — so it is taller than the line of text in
    // it strictly needs.
    static constexpr int listHeight = 30;

    // A field with this many or fewer shows them all; past it, a list.
    static constexpr int inlineLimit = 3;

    int count() const { return static_cast<int>(choices.size()); }

    bool inlineChoices() const { return count() > 0 && count() <= inlineLimit; }

    juce::Rectangle<int> segmentBounds(int index) const
    {
        const auto area = getLocalBounds();
        if (count() <= 0) return area;
        const auto height = area.getHeight() / count();
        // The last one takes the remainder, so the column ends exactly where
        // the field does rather than a pixel or two short.
        return {area.getX(), area.getY() + index * height, area.getWidth(),
                index == count() - 1 ? area.getBottom() - (area.getY() + index * height) : height};
    }

    // The list style is one line whatever the box is, centred in it, so a field
    // of eight choices sits on the same line as a field of two.
    juce::Rectangle<int> listBounds() const
    {
        return juce::Rectangle<int>(getWidth(), juce::jmin(getHeight(), listHeight))
            .withCentre(getLocalBounds().getCentre());
    }

    void paint(juce::Graphics& g) override
    {
        const auto enabled = isEnabled();
        const auto alpha = enabled ? 1.0f : 0.35f;
        if (count() <= 0)
        {
            g.setColour(juce::Colour(0xff0b0e18).withAlpha(alpha * 0.6f));
            g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 3.0f);
            return;
        }

        if (inlineChoices())
        {
            for (int i = 0; i < count(); ++i)
            {
                const auto box = segmentBounds(i).toFloat().reduced(1.0f);
                const auto live = i == chosen;
                g.setColour((live ? accent.withAlpha(alpha * 0.22f) : juce::Colour(0xff0b0e18))
                                .withAlpha(live ? alpha * 0.22f : alpha));
                g.fillRoundedRectangle(box, 3.0f);
                g.setColour((live ? accent : line).withAlpha(alpha * (live ? 1.0f : 0.7f)));
                g.drawRoundedRectangle(box, 3.0f, 1.0f);
                g.setColour((live ? accent : mutedText).withAlpha(alpha));
                g.setFont(panelFont(Face::emphasis, 12.0f));
                g.drawText(choices[static_cast<size_t>(i)], box, juce::Justification::centred);
            }
            return;
        }

        const auto area = listBounds().toFloat().reduced(1.0f);
        g.setColour(juce::Colour(0xff0b0e18).withAlpha(alpha));
        g.fillRoundedRectangle(area, 3.0f);
        g.setColour(accent.withAlpha(alpha));
        g.drawRoundedRectangle(area, 3.0f, 1.0f);
        g.setFont(panelFont(Face::emphasis, 13.5f));
        g.drawText(choices[static_cast<size_t>(juce::jlimit(0, count() - 1, chosen))],
                   area.reduced(static_cast<float>(arrowWidth), 0.0f), juce::Justification::centred);

        // The two arrows, greyed at the end they cannot go past — a list that
        // does not wrap should say so before it is clicked.
        const auto arrow = [&] (juce::Rectangle<float> box, bool pointingLeft, bool live)
        {
            juce::Path path;
            const auto middle = box.getCentreY();
            const auto x = box.getCentreX();
            if (pointingLeft) { path.startNewSubPath(x + 3.4f, middle - 5.5f);
                                path.lineTo(x - 3.4f, middle); path.lineTo(x + 3.4f, middle + 5.5f); }
            else              { path.startNewSubPath(x - 3.4f, middle - 5.5f);
                                path.lineTo(x + 3.4f, middle); path.lineTo(x - 3.4f, middle + 5.5f); }
            g.setColour((live ? accent : line).withAlpha(alpha * (live ? 0.9f : 0.5f)));
            g.strokePath(path, juce::PathStrokeType(1.8f));
        };
        arrow(area.withWidth(static_cast<float>(arrowWidth)), true, chosen > 0);
        arrow(area.withLeft(area.getRight() - arrowWidth), false, chosen < count() - 1);
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (!isEnabled() || count() <= 0) return;
        if (inlineChoices())
        {
            for (int i = 0; i < count(); ++i)
                if (segmentBounds(i).contains(event.getPosition()))
                {
                    if (i != chosen && onChoose) onChoose(i);
                    return;
                }
            return;
        }
        if (!listBounds().contains(event.getPosition())) return;
        if (event.x < arrowWidth) { if (chosen > 0 && onChoose) onChoose(chosen - 1); return; }
        if (event.x > getWidth() - arrowWidth)
        {
            if (chosen < count() - 1 && onChoose) onChoose(chosen + 1);
            return;
        }
        if (onOpenList) onOpenList();
    }
};

// One slot's name plate: the type's mark, its name, and the colour both are in.
// It is the control that sets the type as well as the thing that says what it
// is — clicking it opens the list, which is how a rack slot is filled
// everywhere else that has one, and it means a slot's identity and its one
// structural choice are the same object rather than a badge beside a field.
class FxPlate final : public juce::Button
{
public:
    FxPlate() : juce::Button("FX") {}

    int type = 0;
    std::function<void()> onPlateClick;

    void paintButton(juce::Graphics& g, bool highlighted, bool held) override
    {
        const auto area = getLocalBounds().toFloat().reduced(1.0f);
        const auto colour = fxTypeColour(type);
        const auto filled = type != 0;
        const auto enabled = isEnabled();
        const auto alpha = enabled ? 1.0f : 0.4f;

        g.setColour(juce::Colour(0xff0b0e18).withAlpha(enabled ? 1.0f : 0.5f));
        g.fillRoundedRectangle(area, 4.0f);
        if (filled)
        {
            // A wash of the type's own colour, so a filled slot reads as
            // occupied across the width of the rack without being read.
            g.setGradientFill(juce::ColourGradient(colour.withAlpha(alpha * 0.22f), area.getTopLeft(),
                                                   colour.withAlpha(alpha * 0.04f), area.getBottomRight(),
                                                   false));
            g.fillRoundedRectangle(area, 4.0f);
        }
        g.setColour((filled ? colour : line).withAlpha(alpha * (highlighted || held ? 1.0f : 0.75f)));
        g.drawRoundedRectangle(area, 4.0f, 1.0f);

        // The mark takes the left third and the name the rest, so the names
        // line up down the rack whatever the marks are.
        auto body = area.reduced(7.0f, 5.0f);
        const auto mark = body.removeFromLeft(juce::jmin(34.0f, body.getWidth() * 0.4f));
        drawFxMark(g, mark, type, filled ? colour : mutedText, alpha * 0.9f);

        body.removeFromLeft(6.0f);
        g.setColour((filled ? colour : mutedText).withAlpha(alpha));
        g.setFont(panelFont(Face::emphasis, 13.0f));
        g.drawText(getName(), body, juce::Justification::centredLeft);
    }

    void clicked() override { if (onPlateClick) onPlateClick(); }
};

// One slot of a rack, drawn as a shelf: a well a shade below the module, with
// the type's colour lit down its left edge the way a module's is lit across its
// top. Stacked at a fixed height they form the rack, and the lit edges make the
// filled slots countable from across the room.
inline void drawFxShelf(juce::Graphics& g, juce::Rectangle<int> area, int type, bool on)
{
    const auto box = area.toFloat().reduced(0.0f, 2.0f);
    const auto filled = type != 0;
    const auto alpha = on ? 1.0f : 0.4f;

    g.setColour(juce::Colour(0xff0e1220).withAlpha(on ? 1.0f : 0.6f));
    g.fillRoundedRectangle(box, 4.0f);
    g.setColour(line.withAlpha(alpha * 0.55f));
    g.drawRoundedRectangle(box, 4.0f, 1.0f);
    if (!filled) return;

    g.setColour(fxTypeColour(type).withAlpha(alpha));
    g.fillRoundedRectangle(box.withWidth(moduleEdgeHeight), 1.5f);
}

// The list at the left of the rack is deliberately quieter than the editors
// beside it: it is the signal-flow map, not a second copy of the controls. Each
// row repeats the type's own mark and colour, then keeps only the two structural
// actions Serum's rack establishes as useful at this level -- auditioning a
// bypass and removing the module. Folded, the word and actions leave but the
// marks stay in their exact vertical positions.
inline void drawFxListItem(juce::Graphics& g, juce::Rectangle<int> area, int slot, int type,
                           bool bypassed, bool selected, bool open)
{
    const auto filled = type != 0;
    const auto colour = fxTypeColour(type);
    const auto box = area.toFloat().reduced(0.0f, 2.0f);

    g.setColour(selected ? colour.withAlpha(filled ? 0.16f : 0.08f)
                         : juce::Colour(0xff090c15));
    g.fillRoundedRectangle(box, 4.0f);
    g.setColour((selected ? colour : line).withAlpha(selected ? 0.9f : 0.55f));
    g.drawRoundedRectangle(box, 4.0f, selected ? 1.4f : 1.0f);
    if (filled)
    {
        g.setColour(colour.withAlpha(bypassed ? 0.35f : 1.0f));
        g.fillRoundedRectangle(box.withWidth(moduleEdgeHeight), 1.5f);
    }

    auto icon = juce::Rectangle<float>(open ? 44.0f : box.getWidth(), box.getHeight())
                    .withPosition(box.getX(), box.getY()).reduced(8.0f, 9.0f);
    drawFxMark(g, icon, type, filled ? colour : mutedText, bypassed ? 0.35f : 0.95f);

    if (!open)
    {
        g.setColour((filled ? colour : mutedText).withAlpha(0.9f));
        g.setFont(panelFont(Face::reading, 8.0f));
        g.drawText(juce::String(slot + 1), area.reduced(4).withHeight(12),
                   juce::Justification::topLeft);
        return;
    }

    auto textArea = area.withTrimmedLeft(50).withTrimmedRight(54);
    g.setColour((filled ? colour : mutedText).withAlpha(bypassed ? 0.42f : 1.0f));
    g.setFont(panelFont(Face::emphasis, 11.0f));
    g.drawText(fxTypeName(type), textArea.withTrimmedBottom(textArea.getHeight() / 2 - 2),
               juce::Justification::centredLeft);
    g.setColour(mutedText.withAlpha(0.65f));
    g.setFont(panelFont(Face::label, 8.5f));
    g.drawText("SLOT " + juce::String(slot + 1), textArea.withTrimmedTop(textArea.getHeight() / 2),
               juce::Justification::centredLeft);

    const auto bypass = fxListBypassBounds(area).toFloat();
    const auto centre = bypass.getCentre();
    const auto powerColour = bypassed ? juce::Colour(0xffff6f4a) : (filled ? colour : line);
    g.setColour(powerColour.withAlpha(filled ? 0.9f : 0.35f));
    g.drawEllipse(juce::Rectangle<float>(11.0f, 11.0f).withCentre(centre), 1.25f);
    g.fillRect(centre.x - 0.75f, centre.y - 7.0f, 1.5f, 7.0f);

    const auto remove = fxListRemoveBounds(area).toFloat().reduced(6.0f);
    g.setColour((filled ? mutedText : line).withAlpha(filled ? 0.75f : 0.3f));
    g.drawLine({remove.getTopLeft(), remove.getBottomRight()}, 1.2f);
    g.drawLine({remove.getTopRight(), remove.getBottomLeft()}, 1.2f);
}

inline void drawFxAddButton(juce::Graphics& g, juce::Rectangle<int> area, bool open, bool enabled)
{
    const auto box = area.toFloat().reduced(0.5f);
    const auto accent = signalViolet.withAlpha(enabled ? 0.9f : 0.3f);
    g.setColour(juce::Colour(0xff0d1120));
    g.fillRoundedRectangle(box, 3.0f);
    g.setColour(accent);
    g.drawRoundedRectangle(box, 3.0f, 1.0f);

    const auto centre = open ? juce::Point<float>(box.getX() + 12.0f, box.getCentreY())
                             : box.getCentre();
    g.drawLine(centre.x - 4.0f, centre.y, centre.x + 4.0f, centre.y, 1.3f);
    g.drawLine(centre.x, centre.y - 4.0f, centre.x, centre.y + 4.0f, 1.3f);
    if (open)
    {
        g.setFont(panelFont(Face::emphasis, 9.0f));
        g.setColour(enabled ? text.withAlpha(0.9f) : mutedText.withAlpha(0.45f));
        g.drawText(enabled ? "ADD EFFECT" : "RACK FULL",
                   area.withTrimmedLeft(24), juce::Justification::centredLeft);
    }
}

inline void drawFxListViewButton(juce::Graphics& g, juce::Rectangle<int> area, bool open)
{
    const auto box = area.toFloat().reduced(0.5f);
    g.setColour(juce::Colour(0xff090c15));
    g.fillRoundedRectangle(box, 3.0f);
    g.setColour(signalViolet.withAlpha(0.75f));
    g.drawRoundedRectangle(box, 3.0f, 1.0f);

    const auto rail = box.reduced(5.0f, 5.0f);
    g.setColour(signalViolet.withAlpha(open ? 0.9f : 0.45f));
    g.fillRoundedRectangle(rail.withWidth(open ? rail.getWidth() * 0.42f : 3.0f), 1.0f);
    juce::Path arrow;
    const auto x = rail.getRight() - 2.0f;
    const auto middle = rail.getCentreY();
    const auto direction = open ? -1.0f : 1.0f;
    arrow.startNewSubPath(x - direction * 3.0f, middle - 3.5f);
    arrow.lineTo(x + direction * 1.0f, middle);
    arrow.lineTo(x - direction * 3.0f, middle + 3.5f);
    g.strokePath(arrow, juce::PathStrokeType(1.2f));
}

inline void drawFxExpandButton(juce::Graphics& g, juce::Rectangle<int> area, bool expanded)
{
    const auto box = area.toFloat().reduced(0.5f);
    g.setColour(expanded ? signalViolet.withAlpha(0.2f) : juce::Colour(0xff090c15));
    g.fillRoundedRectangle(box, 3.0f);
    g.setColour(signalViolet.withAlpha(expanded ? 1.0f : 0.75f));
    g.drawRoundedRectangle(box, 3.0f, 1.0f);

    const auto inset = box.reduced(5.0f);
    if (expanded)
    {
        // The familiar overlapping-window mark: restore the rack to its
        // ordinary one-row footprint.
        const auto back = juce::Rectangle<float>(7.0f, 7.0f).withCentre(inset.getCentre())
                              .translated(2.0f, -2.0f);
        const auto front = back.translated(-3.0f, 3.0f);
        g.drawRoundedRectangle(back, 1.0f, 1.1f);
        g.drawRoundedRectangle(front, 1.0f, 1.1f);
    }
    else
    {
        // Four outward corners: grow the rack through the lower module row.
        juce::Path corners;
        const auto corner = [&corners] (float x, float y, float sx, float sy)
        {
            corners.startNewSubPath(x + sx * 3.5f, y);
            corners.lineTo(x, y);
            corners.lineTo(x, y + sy * 3.5f);
        };
        corner(inset.getX(), inset.getY(), 1.0f, 1.0f);
        corner(inset.getRight(), inset.getY(), -1.0f, 1.0f);
        corner(inset.getX(), inset.getBottom(), 1.0f, -1.0f);
        corner(inset.getRight(), inset.getBottom(), -1.0f, -1.0f);
        g.strokePath(corners, juce::PathStrokeType(1.15f));
    }
}
}
