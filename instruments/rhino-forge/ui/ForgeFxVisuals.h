#pragma once

#include "ForgeControls.h"
#include "ForgeFxStrips.h"

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

// The equaliser's second band's colour. A band is read by its tint before its
// words, and two tints of one family keep the two bands one effect: the type's
// amber for the low band, a pale cream for the high.
inline const juce::Colour fxEqHighColour {0xfffff1c4};

// An equaliser band's shape as a picture, for the glyph stacks hugging its
// curve: 0 low shelf, 1 bell, 2 high pass, 3 high shelf, 4 low pass.
inline void drawEqBandGlyph(juce::Graphics& g, juce::Rectangle<float> box, int shape, juce::Colour colour)
{
    const auto x0 = box.getX(), x1 = box.getRight(), cx = box.getCentreX();
    const auto top = box.getY() + box.getHeight() * 0.2f, bottom = box.getBottom() - box.getHeight() * 0.2f;
    const auto middle = box.getCentreY();
    juce::Path path;
    switch (shape)
    {
        case 0:
            path.startNewSubPath(x0, top);
            path.lineTo(cx - box.getWidth() * 0.15f, top);
            path.quadraticTo(cx, top, cx + box.getWidth() * 0.1f, middle);
            path.quadraticTo(cx + box.getWidth() * 0.2f, bottom, x1, bottom);
            break;
        case 3:
            path.startNewSubPath(x0, bottom);
            path.quadraticTo(cx - box.getWidth() * 0.2f, bottom, cx - box.getWidth() * 0.1f, middle);
            path.quadraticTo(cx, top, cx + box.getWidth() * 0.15f, top);
            path.lineTo(x1, top);
            break;
        case 1:
            path.startNewSubPath(x0, bottom);
            path.quadraticTo(cx - box.getWidth() * 0.18f, bottom, cx, top);
            path.quadraticTo(cx + box.getWidth() * 0.18f, bottom, x1, bottom);
            break;
        case 2:
            path.startNewSubPath(x0, box.getBottom());
            path.quadraticTo(x0 + box.getWidth() * 0.35f, top, cx, top);
            path.lineTo(x1, top);
            break;
        default:
            path.startNewSubPath(x0, top);
            path.lineTo(cx, top);
            path.quadraticTo(x0 + box.getWidth() * 0.65f, top, x1, box.getBottom());
            break;
    }
    g.setColour(colour);
    g.strokePath(path, juce::PathStrokeType(1.5f));
}

// A filter's tap as a picture: a low pass falls away to the right of its
// corner, a high pass rises into it from the left.
inline void drawFilterTapGlyph(juce::Graphics& g, juce::Rectangle<float> box, bool highPass, juce::Colour colour)
{
    juce::Path path;
    const auto top = box.getY() + box.getHeight() * 0.3f;
    if (highPass)
    {
        path.startNewSubPath(box.getX(), box.getBottom());
        path.quadraticTo(box.getX() + box.getWidth() * 0.35f, top, box.getCentreX(), top);
        path.lineTo(box.getRight(), top);
    }
    else
    {
        path.startNewSubPath(box.getX(), top);
        path.lineTo(box.getCentreX(), top);
        path.quadraticTo(box.getX() + box.getWidth() * 0.65f, top, box.getRight(), box.getBottom());
    }
    g.setColour(colour);
    g.strokePath(path, juce::PathStrokeType(1.5f));
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
    // What to show in place of the chosen name. The table field needs it: the
    // table an oscillator plays need not be any of the files it lists -- a
    // preset's own, one drawn over -- and the field still has to say what is
    // playing. It sets `chosen` to -1 then, which greys the left arrow and
    // lets the right one step onto the first choice.
    juce::String caption;
    juce::Colour accent = electricBlue;
    std::function<void(int)> onChoose;
    // Opens the list, for a field with too many choices to show at once.
    std::function<void()> onOpenList;

    // What each arrow is given at a field with room for it. It is mostly
    // padding: the chevron itself is seven pixels across, and the rest is there
    // so the two ends are comfortable to hit.
    static constexpr int arrowWidth = 20;

    // How much of *this* field each arrow actually takes. A fixed twenty either
    // side is nothing on the filter's field and most of the noise module's,
    // whose plate is two columns of twenty-four and narrower still at the
    // smallest window — forty pixels of padding around a word that then has no
    // room to be read. So the padding comes off first and the chevron is the
    // last thing to go: a fifth of the field each, never less than the chevron
    // plus a margin, never more than the twenty a roomy field gets.
    //
    // Paint and hit-testing both read this, so what the arrows are drawn at is
    // what clicking them steps at.
    int arrowSpan() const { return juce::jlimit(12, arrowWidth, getWidth() / 5); }

    // The height a single row of the list style wants, centred in a box that is
    // tall enough to stack three. The reference draws this field as the largest
    // thing in its row — the one place on the panel where a choice is read at a
    // glance rather than looked for — so it is taller than the line of text in
    // it strictly needs.
    static constexpr int listHeight = 30;

    // The size the chosen name is drawn at, and the smallest it may be squeezed
    // to before the field gives up and lets it crop. Nine points is about where
    // this face stops being readable at a glance, which is the only thing this
    // field is for.
    static constexpr float listTextHeight = 13.5f;
    static constexpr float minimumListTextHeight = 8.5f;

    // A field with this many or fewer shows them all; past it, a list.
    static constexpr int inlineLimit = 3;

    // Unless the field says otherwise. Stacking every choice needs a box tall
    // enough to stack them in, and a field *seated* in a display has a strip
    // rather than a box — an oscillator's MODE lies along the top edge of the
    // waveform in 26 pixels, where two stacked segments are 13 pixels each and
    // the names spill over the picture below. The list style is one line
    // whatever the box is, so it fits a strip exactly, and it is also what
    // Serum draws this particular field as (p. 104).
    bool forceList = false;

    // How a rack slot's strip asks for this field to be drawn. None leaves it
    // to the count: a stack for two or three choices, a list past that. The
    // others are what a strip can ask for instead; see FxPresentation.
    FxPresentation presentation = FxPresentation::none;
    // The picture for one choice, for a field drawn as glyphs: an equaliser
    // band's shelf, bell and pass. Given the choice and the box to draw in.
    std::function<void(juce::Graphics&, juce::Rectangle<float>, int choice, juce::Colour)> glyph;

    int count() const { return static_cast<int>(choices.size()); }

    // What is actually drawn, once the count has had its say: a field asked
    // for a stack that has too many choices to stack draws a list instead.
    FxPresentation drawnAs() const
    {
        if (count() <= 0) return FxPresentation::none;
        switch (presentation)
        {
            case FxPresentation::chip:          return count() == 2 ? presentation : FxPresentation::list;
            case FxPresentation::route:         return count() == 5 ? presentation : FxPresentation::list;
            case FxPresentation::captionSwitch:
            case FxPresentation::segmented:     return presentation;
            case FxPresentation::glyphs:
                return glyph != nullptr && count() <= inlineLimit ? presentation : FxPresentation::list;
            case FxPresentation::list:          return FxPresentation::list;
            case FxPresentation::stack:
            case FxPresentation::none:          break;
        }
        return !forceList && count() <= inlineLimit ? FxPresentation::stack : FxPresentation::list;
    }

    bool inlineChoices() const { return drawnAs() == FxPresentation::stack; }

    // The distortion's filter is one field of five choices -- OFF, PRE HP,
    // PRE LP, POST HP, POST LP -- drawn as the two things it is: where the
    // filter stands (OFF, PRE, POST) and which tap it takes (LP, HP). These
    // turn one into the other, in both directions.
    static int routePlacement(int choice) { return choice <= 0 ? 0 : choice <= 2 ? 1 : 2; }
    static bool routeHighPass(int choice) { return choice == 1 || choice == 3; }
    static int routeChoice(int placement, bool highPass)
    {
        if (placement <= 0) return 0;
        return placement == 1 ? (highPass ? 1 : 2) : (highPass ? 3 : 4);
    }

    // The route's two columns: placement on the left, the tap on the right.
    juce::Rectangle<int> routePlacementBounds() const
    {
        return getLocalBounds().withWidth(getWidth() * 3 / 5);
    }
    juce::Rectangle<int> routeTapBounds() const
    {
        return getLocalBounds().withTrimmedLeft(getWidth() * 3 / 5 + 3);
    }

    // One of `parts` equal segments of `area`, down it or across it. The last
    // takes the remainder, so the segments end exactly where the area does.
    static juce::Rectangle<int> partOf(juce::Rectangle<int> area, int index, int parts, bool across)
    {
        if (parts <= 0) return area;
        if (across)
        {
            const auto width = area.getWidth() / parts;
            return {area.getX() + index * width, area.getY(),
                    index == parts - 1 ? area.getRight() - (area.getX() + index * width) : width, area.getHeight()};
        }
        const auto height = area.getHeight() / parts;
        return {area.getX(), area.getY() + index * height, area.getWidth(),
                index == parts - 1 ? area.getBottom() - (area.getY() + index * height) : height};
    }

    juce::Rectangle<int> segmentBounds(int index) const
    {
        // The last one takes the remainder, so the column ends exactly where
        // the field does rather than a pixel or two short.
        return partOf(getLocalBounds(), index, count(), drawnAs() == FxPresentation::segmented);
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
            g.setColour(fieldFloor.withAlpha(alpha * 0.6f));
            g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 3.0f);
            return;
        }

        // One choice's box: lit when it is the live one.
        const auto drawChoice = [&] (juce::Rectangle<float> box, bool live, const juce::String& name,
                                     float textSize, int glyphChoice)
        {
            g.setColour(live ? accent.withAlpha(alpha * 0.22f) : fieldFloor.withAlpha(alpha));
            g.fillRoundedRectangle(box, 3.0f);
            g.setColour((live ? accent : line).withAlpha(alpha * (live ? 1.0f : 0.7f)));
            g.drawRoundedRectangle(box, 3.0f, 1.0f);
            const auto ink = (live ? accent : mutedText).withAlpha(alpha);
            if (glyphChoice >= 0 && glyph != nullptr)
            {
                glyph(g, box.reduced(5.0f, 3.0f), glyphChoice, ink);
                return;
            }
            g.setColour(ink);
            g.setFont(panelFont(Face::emphasis, textSize));
            g.drawText(name, box, juce::Justification::centred);
        };

        switch (drawnAs())
        {
            case FxPresentation::stack:
            case FxPresentation::segmented:
            case FxPresentation::glyphs:
            {
                const auto glyphs = drawnAs() == FxPresentation::glyphs;
                for (int i = 0; i < count(); ++i)
                    drawChoice(segmentBounds(i).toFloat().reduced(drawnAs() == FxPresentation::segmented
                                                                      ? 0.5f : 1.0f),
                               i == chosen, choices[static_cast<size_t>(i)],
                               drawnAs() == FxPresentation::segmented ? 11.0f : 12.0f, glyphs ? i : -1);
                return;
            }
            case FxPresentation::chip:
            {
                // One latching button, lit on the second choice: BPM, AUTO.
                // The first choice is what it is when it is not lit.
                const auto box = getLocalBounds().toFloat().reduced(0.5f);
                const auto on = chosen == 1;
                g.setColour(on ? accent.withAlpha(alpha * 0.2f) : chipFloor.withAlpha(alpha));
                g.fillRoundedRectangle(box, 3.0f);
                g.setColour((on ? accent : line).withAlpha(alpha));
                g.drawRoundedRectangle(box, 3.0f, 1.0f);
                const auto lamp = juce::Rectangle<float>(5.0f, 5.0f)
                                      .withCentre({box.getX() + 8.0f, box.getCentreY()});
                if (on)
                {
                    g.setColour(accent.withAlpha(alpha * 0.3f));
                    g.fillEllipse(lamp.expanded(2.5f));
                }
                g.setColour(on ? accent.withAlpha(alpha) : lampOff.withAlpha(alpha));
                g.fillEllipse(lamp);
                g.setColour((on ? accent : mutedText).withAlpha(alpha));
                g.setFont(panelFont(Face::emphasis, 11.0f));
                g.drawText(choices[1], box.withTrimmedLeft(14.0f), juce::Justification::centred);
                return;
            }
            case FxPresentation::captionSwitch:
            {
                // The caption over a knob that is that knob's mode: a pill
                // naming the live choice. Clicking it steps to the next.
                const auto box = getLocalBounds().toFloat().reduced(0.5f);
                g.setColour(accent.withAlpha(alpha * 0.18f));
                g.fillRoundedRectangle(box, box.getHeight() * 0.5f);
                g.setColour(accent.withAlpha(alpha));
                g.drawRoundedRectangle(box, box.getHeight() * 0.5f, 1.0f);
                g.setFont(panelFont(Face::emphasis, 11.0f));
                g.drawText(choices[static_cast<size_t>(juce::jlimit(0, count() - 1, chosen))], box,
                           juce::Justification::centred);
                return;
            }
            case FxPresentation::route:
            {
                const auto placement = routePlacement(chosen);
                const auto highPass = routeHighPass(chosen);
                static const char* const places[] {"OFF", "PRE", "POST"};
                for (int i = 0; i < 3; ++i)
                    drawChoice(partOf(routePlacementBounds(), i, 3, false).toFloat().reduced(1.0f),
                               i == placement, places[i], 11.5f, -1);
                // The tap is meaningless with the filter out of the path, so
                // it greys with it rather than going away.
                const auto tapAlpha = placement == 0 ? 0.35f : 1.0f;
                for (int i = 0; i < 2; ++i)
                {
                    const auto box = partOf(routeTapBounds(), i, 2, false).toFloat().reduced(1.0f);
                    const auto live = placement != 0 && (i == 1) == highPass;
                    g.setColour(live ? accent.withAlpha(alpha * 0.22f) : fieldFloor.withAlpha(alpha));
                    g.fillRoundedRectangle(box, 3.0f);
                    g.setColour((live ? accent : line).withAlpha(alpha * tapAlpha * (live ? 1.0f : 0.7f)));
                    g.drawRoundedRectangle(box, 3.0f, 1.0f);
                    drawFilterTapGlyph(g, box.reduced(5.0f, 4.0f), i == 1,
                                       (live ? accent : mutedText).withAlpha(alpha * tapAlpha));
                }
                return;
            }
            case FxPresentation::list:
            case FxPresentation::none:
                break;
        }

        const auto area = listBounds().toFloat().reduced(1.0f);
        g.setColour(fieldFloor.withAlpha(alpha));
        g.fillRoundedRectangle(area, 3.0f);
        g.setColour(accent.withAlpha(alpha));
        g.drawRoundedRectangle(area, 3.0f, 1.0f);
        // The name at the size it is meant to be read at, or as much smaller
        // as it takes to fit between the arrows.
        //
        // Ellipsising is what JUCE does when it does not fit, and an ellipsis
        // is the one thing a field like this must never show: the whole of its
        // job is to say which of the choices you are on, and BRO... does not.
        // A field wide enough for its longest name is unaffected -- the filter
        // and the warp both are -- so this only ever costs a point or two on a
        // narrow one. The noise module's is narrow because its plate is two
        // columns of twenty-four, and two of its four sources are longer words
        // than that plate is wide.
        const auto name = caption.isNotEmpty()
            ? caption : juce::String(choices[static_cast<size_t>(juce::jlimit(0, count() - 1, chosen))]);
        const auto textArea = area.reduced(static_cast<float>(arrowSpan()), 0.0f);
        auto font = panelFont(Face::emphasis, listTextHeight);
        const auto needed = juce::GlyphArrangement::getStringWidth(font, name);
        if (needed > textArea.getWidth() && needed > 0.0f)
            font = font.withHeight(juce::jmax(minimumListTextHeight,
                                              listTextHeight * textArea.getWidth() / needed));
        g.setFont(font);
        g.drawText(name, textArea, juce::Justification::centred);

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
        arrow(area.withWidth(static_cast<float>(arrowSpan())), true, chosen > 0);
        arrow(area.withLeft(area.getRight() - static_cast<float>(arrowSpan())), false,
              chosen < count() - 1);
    }

    // Whether the wheel steps this field. A rack slot's mode says no: the
    // wheel over the rack scrolls the rack, and a field inside it swallowing
    // that would make the chain unscrollable wherever a mode happened to be
    // under the pointer. The three fields whose list is a fixed one of their
    // own — the filter's type, an oscillator's warp, the noise source — sit in
    // nothing that scrolls, and a list of thirty-four is exactly what a wheel
    // is for.
    bool wheelSteps = false;

    // One notch is one step. A mouse sends about 0.2 a notch and a trackpad a
    // drizzle of much smaller deltas, so the travel adds up until it is worth a
    // step rather than each delta being one — the same threshold the envelope's
    // zoom is stepped on, and for the same reason.
    static constexpr float wheelPerStep = 0.15f;

    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        if (!wheelSteps || !isEnabled() || count() <= 0 || onChoose == nullptr)
        {
            juce::Component::mouseWheelMove(event, wheel);
            return;
        }
        wheelTravel += wheel.isReversed ? -wheel.deltaY : wheel.deltaY;
        if (std::abs(wheelTravel) < wheelPerStep) return;
        // Up is along the list, the way up is more on every knob beside it.
        const auto step = wheelTravel > 0.0f ? 1 : -1;
        wheelTravel = 0.0f;
        const auto next = juce::jlimit(0, count() - 1, chosen + step);
        if (next != chosen) onChoose(next);
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (!isEnabled() || count() <= 0) return;
        const auto at = event.getPosition();
        switch (drawnAs())
        {
            case FxPresentation::stack:
            case FxPresentation::segmented:
            case FxPresentation::glyphs:
                for (int i = 0; i < count(); ++i)
                    if (segmentBounds(i).contains(at))
                    {
                        if (i != chosen && onChoose) onChoose(i);
                        return;
                    }
                return;
            case FxPresentation::chip:
                if (onChoose) onChoose(chosen == 1 ? 0 : 1);
                return;
            case FxPresentation::captionSwitch:
                if (onChoose) onChoose((juce::jmax(0, chosen) + 1) % count());
                return;
            case FxPresentation::route:
            {
                const auto placement = routePlacement(chosen);
                const auto highPass = routeHighPass(chosen);
                auto next = chosen;
                for (int i = 0; i < 3; ++i)
                    if (partOf(routePlacementBounds(), i, 3, false).contains(at)) next = routeChoice(i, highPass);
                if (placement != 0)
                    for (int i = 0; i < 2; ++i)
                        if (partOf(routeTapBounds(), i, 2, false).contains(at)) next = routeChoice(placement, i == 1);
                if (next != chosen && onChoose) onChoose(next);
                return;
            }
            case FxPresentation::list:
            case FxPresentation::none:
                break;
        }
        if (!listBounds().contains(event.getPosition())) return;
        if (event.x < arrowSpan()) { if (chosen > 0 && onChoose) onChoose(chosen - 1); return; }
        if (event.x > getWidth() - arrowSpan())
        {
            if (chosen < count() - 1 && onChoose) onChoose(chosen + 1);
            return;
        }
        if (onOpenList) onOpenList();
    }

private:
    // Wheel travel not yet spent on a step.
    float wheelTravel = 0.0f;
};

// The red a bypassed effect's power light turns, in the list and on its card.
// The same red whatever the effect, so a bypass reads as a bypass rather than
// as one more type colour.
inline const juce::Colour fxBypassedColour {0xffff6f4a};

// The power glyph, centred in `box`: a ring and a stem from its top to its
// middle. The list draws it beside every row and the card in its corner, so
// the two read as the same switch.
inline void drawFxPowerGlyph(juce::Graphics& g, juce::Rectangle<float> box, juce::Colour colour)
{
    const auto centre = box.getCentre();
    const auto size = juce::jmin(box.getWidth(), box.getHeight());
    g.setColour(colour);
    g.drawEllipse(juce::Rectangle<float>(size, size).withCentre(centre), 1.25f);
    g.fillRect(centre.x - 0.75f, centre.y - size * 7.0f / 11.0f, 1.5f, size * 7.0f / 11.0f);
}

// One slot's identity card: the type's mark, its name, and the switch that
// bypasses it. Clicking anywhere on it takes the effect out or puts it back:
// the card is lit while the effect is heard and dark while it is bypassed,
// with the list's power glyph in its corner saying the same thing.
//
// It no longer chooses the type. Effects are added and removed in the list;
// what is in a slot is not something to change by accident while reaching for
// a knob beside it.
class FxPlate final : public juce::Button
{
public:
    FxPlate() : juce::Button("FX") {}

    int type = 0;
    // Whether the effect is in. Follows the slot's bypass parameter, which is
    // what the card switches.
    bool lit = true;
    std::function<void()> onPlateClick;
    // A right-click: the bypass parameter's own menu, for MIDI learn.
    std::function<void()> onPlateMenu;

    // Where the power light sits, in the card's own coordinates.
    juce::Rectangle<float> powerBounds() const
    {
        const auto area = getLocalBounds().toFloat();
        return {area.getRight() - 20.0f, area.getY() + 4.0f, 15.0f, 15.0f};
    }

    void paintButton(juce::Graphics& g, bool highlighted, bool held) override
    {
        const auto area = getLocalBounds().toFloat().reduced(1.0f);
        const auto colour = fxTypeColour(type);
        const auto filled = type != 0;
        const auto on = filled && lit;
        const auto enabled = isEnabled();
        const auto alpha = enabled ? 1.0f : 0.4f;

        g.setColour(fieldFloor.withAlpha(enabled ? 1.0f : 0.5f));
        g.fillRoundedRectangle(area, 4.0f);
        if (on)
        {
            // Lit: a wash of the type's own colour, and the edge glowing.
            g.setGradientFill(juce::ColourGradient(colour.withAlpha(alpha * 0.24f), area.getTopLeft(),
                                                   colour.withAlpha(alpha * 0.04f), area.getBottomRight(),
                                                   false));
            g.fillRoundedRectangle(area, 4.0f);
            g.setColour(colour.withAlpha(alpha * 0.16f));
            g.drawRoundedRectangle(area.reduced(1.5f), 3.0f, 3.0f);
        }
        const auto edge = on ? colour.withAlpha(alpha * (highlighted || held ? 1.0f : 0.85f))
                             : line.withAlpha(alpha * (highlighted || held ? 1.0f : 0.8f));
        g.setColour(edge);
        g.drawRoundedRectangle(area, 4.0f, 1.0f);

        // The mark takes the left of the card and the name the rest, so the
        // names line up down the rack whatever the marks are. The power
        // light keeps the top right corner.
        const auto power = powerBounds();
        auto body = area.reduced(7.0f, 6.0f);
        const auto mark = body.removeFromLeft(juce::jmin(28.0f, body.getWidth() * 0.3f))
                              .withSizeKeepingCentre(juce::jmin(28.0f, body.getWidth() * 0.3f),
                                                     juce::jmin(24.0f, body.getHeight() * 0.6f));
        drawFxMark(g, mark, type, on ? colour : mutedText, alpha * (on ? 0.9f : 0.55f));
        body.removeFromLeft(5.0f);
        body.setRight(juce::jmin(body.getRight(), power.getX() + 4.0f));
        g.setColour((on ? colour : mutedText).withAlpha(alpha * (on ? 1.0f : 0.75f)));
        g.setFont(panelFont(Face::emphasis, 13.0f));
        g.drawFittedText(getName(), body.toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);

        if (filled)
        {
            if (on)
            {
                g.setColour(colour.withAlpha(alpha * 0.18f));
                g.fillEllipse(power.expanded(2.0f));
            }
            drawFxPowerGlyph(g, power.withSizeKeepingCentre(11.0f, 11.0f),
                             (on ? colour : fxBypassedColour).withAlpha(alpha * 0.95f));
        }
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu())
        {
            if (onPlateMenu) onPlateMenu();
            return;
        }
        juce::Button::mouseDown(event);
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

    g.setColour(shelfFloor.withAlpha(on ? 1.0f : 0.6f));
    g.fillRoundedRectangle(box, 4.0f);
    g.setColour(line.withAlpha(alpha * 0.55f));
    g.drawRoundedRectangle(box, 4.0f, 1.0f);
    if (!filled) return;

    g.setColour(fxTypeColour(type).withAlpha(alpha));
    g.fillRoundedRectangle(box.withWidth(moduleEdgeHeight), 1.5f);
}

// The hatch that starts every strip's output tail: diagonal rules in the
// identity plate's hatch colour, so MIX and LEVEL read as the same fixture on
// every strip whatever the body before them is.
inline void drawFxHatch(juce::Graphics& g, juce::Rectangle<int> area, float alpha)
{
    if (area.isEmpty()) return;
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(area);
    juce::Path rules;
    const auto box = area.toFloat();
    for (auto x = box.getX() - box.getHeight(); x < box.getRight(); x += 5.0f)
    {
        rules.startNewSubPath(x, box.getBottom());
        rules.lineTo(x + box.getHeight(), box.getY());
    }
    g.setColour(identityHatch.withAlpha(alpha * 0.45f));
    g.strokePath(rules, juce::PathStrokeType(1.0f));
}

// A strip's panel: a frame round controls the DSP couples, its caption
// engraved up the lane at one edge, and a lamp at the top of the lane where
// the panel has one. Dimmed whole when the mode that gates it switches it out.
inline void drawFxPanel(juce::Graphics& g, const FxPanel& group, juce::Colour colour, bool dim,
                        float lamp, float alpha)
{
    const auto frame = group.frame.toFloat().reduced(0.5f);
    g.setColour(colour.withAlpha(alpha * (dim ? 0.015f : 0.035f)));
    g.fillRoundedRectangle(frame, 4.0f);
    g.setColour(colour.withAlpha(alpha * (dim ? 0.16f : 0.38f)));
    g.drawRoundedRectangle(frame, 4.0f, 1.0f);
    const auto lane = group.lane.toFloat();
    const auto edge = group.legendRight ? lane.getX() : lane.getRight();
    g.setColour(colour.withAlpha(alpha * (dim ? 0.12f : 0.28f)));
    g.drawLine(edge, frame.getY() + 4.0f, edge, frame.getBottom() - 4.0f, 1.0f);

    const auto hasLamp = lamp >= 0.0f;
    {
        juce::Graphics::ScopedSaveState state(g);
        const auto centre = lane.getCentre().translated(0.0f, hasLamp ? 5.0f : 0.0f);
        g.addTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi, centre.x, centre.y));
        g.setColour(colour.withAlpha(alpha * (dim ? 0.35f : 0.95f)));
        g.setFont(panelFont(Face::header, 8.0f).withExtraKerningFactor(0.2f));
        g.drawText(group.caption, juce::Rectangle<float>(lane.getHeight(), lane.getWidth()).withCentre(centre),
                   juce::Justification::centred, false);
    }
    if (hasLamp)
    {
        const auto on = juce::jlimit(0.0f, 1.0f, lamp) * (dim ? 0.4f : 1.0f);
        const auto dot = juce::Rectangle<float>(5.6f, 5.6f).withCentre({lane.getCentreX(), lane.getY() + 9.0f});
        if (on > 0.02f)
        {
            g.setColour(colour.withAlpha(alpha * 0.32f * on));
            g.fillEllipse(dot.expanded(4.5f));
        }
        g.setColour(lampOff.interpolatedWith(colour, on).withAlpha(alpha));
        g.fillEllipse(dot);
    }
}

// Which way the signal runs between two panels: a chevron pointing from the
// block that hears it first, its word under it. Nothing but a rule when the
// block it points from is out of the path.
inline void drawFxChevron(juce::Graphics& g, juce::Rectangle<int> area, int direction, const char* word,
                          juce::Colour colour, float alpha)
{
    if (area.isEmpty()) return;
    const auto box = area.toFloat();
    const auto centre = box.getCentre().translated(0.0f, -5.0f);
    if (direction == 0)
    {
        g.setColour(line.withAlpha(alpha));
        g.drawLine(centre.x - 5.0f, centre.y, centre.x + 5.0f, centre.y, 1.5f);
        return;
    }
    const auto d = static_cast<float>(direction);
    juce::Path chevron;
    chevron.startNewSubPath(centre.x - 4.5f * d, centre.y - 7.0f);
    chevron.lineTo(centre.x + 4.5f * d, centre.y);
    chevron.lineTo(centre.x - 4.5f * d, centre.y + 7.0f);
    g.setColour(colour.withAlpha(alpha));
    g.strokePath(chevron, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setFont(panelFont(Face::reading, 7.5f));
    g.drawText(word, box.withTop(centre.y + 10.0f).withHeight(10.0f), juce::Justification::centredTop, false);
}

// The list at the left of the rack is deliberately quieter than the editors
// beside it: it is the signal-flow map, not a second copy of the controls, and
// it is read down by name. Each row is one line: the type's colour as a tick,
// its mark drawn no bigger than the name beside it, the name itself in the
// panel's text colour so a column of them reads at a glance, and only the two
// structural actions Serum's rack establishes as useful at this level --
// auditioning a bypass and removing the module. Folded, the name and actions
// leave and the tick and mark stay on the row they were on.
inline void drawFxListItem(juce::Graphics& g, juce::Rectangle<int> area, int type,
                           bool bypassed, bool selected, bool open)
{
    constexpr auto nameSize = 12.5f;
    const auto filled = type != 0;
    const auto colour = fxTypeColour(type);
    const auto box = area.toFloat().reduced(0.0f, 1.0f);
    const auto dim = bypassed ? 0.4f : 1.0f;

    if (selected)
    {
        g.setColour(colour.withAlpha(filled ? 0.16f : 0.08f));
        g.fillRoundedRectangle(box, 3.0f);
        g.setColour(colour.withAlpha(0.85f));
        g.drawRoundedRectangle(box.reduced(0.5f), 3.0f, 1.1f);
    }

    auto body = box.reduced(6.0f, 0.0f);
    if (filled)
    {
        g.setColour(colour.withAlpha(dim));
        g.fillRoundedRectangle(body.removeFromLeft(3.0f).reduced(0.0f, 6.0f), 1.0f);
    }
    else
        body.removeFromLeft(3.0f);
    body.removeFromLeft(open ? 6.0f : 0.0f);

    // As tall as the name's capitals, so the mark sits in the line of text
    // rather than above it.
    constexpr auto markWidth = 18.0f;
    const auto markArea = open ? body.removeFromLeft(markWidth) : body;
    drawFxMark(g, juce::Rectangle<float>(markWidth, nameSize * 0.8f).withCentre(markArea.getCentre()),
               type, filled ? colour : mutedText, 0.95f * dim);
    if (!open) return;

    body.removeFromLeft(7.0f);
    body.removeFromRight(static_cast<float>(area.getRight()) - fxListBypassBounds(area).toFloat().getX());
    g.setColour((filled ? text : mutedText).withAlpha(bypassed ? 0.42f : 0.95f));
    g.setFont(panelFont(Face::emphasis, nameSize));
    g.drawFittedText(fxTypeName(type), body.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);

    const auto bypass = fxListBypassBounds(area).toFloat();
    const auto powerColour = bypassed ? fxBypassedColour : (filled ? colour : line);
    drawFxPowerGlyph(g, juce::Rectangle<float>(11.0f, 11.0f).withCentre(bypass.getCentre()),
                     powerColour.withAlpha(filled ? 0.9f : 0.35f));

    const auto remove = fxListRemoveBounds(area).toFloat().reduced(6.0f);
    g.setColour((filled ? mutedText : line).withAlpha(filled ? 0.75f : 0.3f));
    g.drawLine({remove.getTopLeft(), remove.getBottomRight()}, 1.2f);
    g.drawLine({remove.getTopRight(), remove.getBottomLeft()}, 1.2f);
}

inline void drawFxAddButton(juce::Graphics& g, juce::Rectangle<int> area, bool open, bool enabled)
{
    const auto box = area.toFloat().reduced(0.5f);
    const auto accent = signalViolet.withAlpha(enabled ? 0.9f : 0.3f);
    g.setColour(chipFloor);
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
    g.setColour(rackFloor);
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
    g.setColour(expanded ? signalViolet.withAlpha(0.2f) : rackFloor);
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
