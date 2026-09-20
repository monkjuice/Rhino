#pragma once

#include "ForgeStyle.h"

// The knob, the slider and the menu, as JUCE asks for them. One place, so a
// control that is drawn by JUCE and one that draws itself still look like
// parts of the same machine.
namespace rhino::forge::ui
{
class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    // LOAD, SAVE and anything else JUCE draws the text of itself. Without
    // these two the panel would be set in Rajdhani and Chakra Petch with the
    // platform's default face showing through wherever a stock component
    // draws its own string.
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override
    {
        return panelFont(Face::emphasis, juce::jmin(12.0f, static_cast<float>(buttonHeight) * 0.48f));
    }

    juce::Font getPopupMenuFont() override { return panelFont(Face::label, 13.0f); }

    // Steppers and bars are both bar-style sliders, so JUCE routes them here.
    // A vertical one is a plain numeric field: for a tuning value the number is
    // the information and a fill proportion would be noise. A horizontal one is
    // a matrix amount, where the fill is the whole point — it runs out from
    // wherever zero falls in the range, so the sign of a depth reads across the
    // table without the number being looked at.
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float, float, float, juce::Slider::SliderStyle style,
                          juce::Slider& slider) override
    {
        if (style == juce::Slider::LinearVertical)
        {
            drawFader(g, {x, y, width, height}, slider);
            return;
        }
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

        // A dot on the top edge at the value's place in its range. The number
        // says what the value is and the dot says where that is, which is the
        // one thing a bare numeric field makes you work out for yourself —
        // whether +7 st is most of the way up or barely off the bottom.
        //
        // Only on a field showing a number. The panel's other steppers show a
        // name, and a position along a range means nothing for one of those, so
        // the reading itself is what decides: a digit or a sign gets the tick.
        const auto reading = slider.getTextFromValue(slider.getValue());
        const auto numeric = reading.isNotEmpty()
                             && (juce::CharacterFunctions::isDigit(reading[0])
                                 || reading[0] == '-' || reading[0] == '+');
        if (!horizontal && numeric)
        {
            const auto range = slider.getRange();
            if (range.getLength() > 0.0)
            {
                const auto at = juce::jlimit(0.0, 1.0, (slider.getValue() - range.getStart())
                                                           / range.getLength());
                const auto x = area.getX() + 3.0f
                               + static_cast<float>(at) * (area.getWidth() - 6.0f);
                g.setColour(accent.withAlpha(enabled ? 0.9f : 0.25f));
                g.fillEllipse(juce::Rectangle<float>(3.6f, 3.6f).withCentre({x, area.getY()}));
            }
        }

        drawModBar(g, area, slider, enabled);

        // Over a fill, the value is read against the accent rather than in it.
        g.setColour((horizontal || !active ? text : accent).withAlpha(enabled ? 1.0f : 0.35f));
        g.setFont(panelFont(Face::reading, 13.5f));
        g.drawText(slider.getTextFromValue(slider.getValue()), area, juce::Justification::centred);
    }

    // A modulated field carries the strip the knob carries a ring: how far the
    // matrix could move this value, faint, and — while a note is sounding —
    // where it has it this instant, bright, with a marker at the end. Both are
    // laid out across the field's own range from wherever the value sits now,
    // so the same drop reads the same way on a numeric field as on a knob.
    //
    // A field nothing is pointed at draws none of it, which is every field on
    // the panel but the two the matrix can reach.
    void drawModBar(juce::Graphics& g, juce::Rectangle<float> field, juce::Slider& slider,
                    bool enabled)
    {
        const auto& properties = slider.getProperties();
        const auto depth = static_cast<float>(properties.getWithDefault("modDepth", 0.0));
        const auto offset = static_cast<float>(properties.getWithDefault("modOffset", 0.0));
        const auto live = static_cast<bool>(properties.getWithDefault("modLive", false));
        if ((depth == 0.0f && offset == 0.0f) || !enabled) return;

        const auto bar = modBarBounds(field);
        const auto range = slider.getRange();
        const auto span = static_cast<float>(range.getLength());
        const auto position = span > 0.0f
            ? static_cast<float>(slider.getValue() - range.getStart()) / span : 0.0f;
        const auto xAt = [&] (float amount)
        {
            return bar.getX() + juce::jlimit(0.0f, 1.0f, position + amount) * bar.getWidth();
        };
        const auto now = xAt(0.0f);
        const auto reach = [&] (float to, juce::Colour colour, float thickness)
        {
            if (std::abs(to - now) < 1.0f) return;
            g.setColour(colour);
            g.fillRoundedRectangle({juce::jmin(now, to), bar.getBottom() - thickness,
                                    std::abs(to - now), thickness}, thickness * 0.5f);
        };

        const auto hovered = static_cast<bool>(properties.getWithDefault("modHover", false));
        reach(xAt(depth), signalViolet.withAlpha(hovered ? 0.75f : 0.32f), bar.getHeight());
        if (live)
        {
            const auto at = xAt(offset);
            reach(at, signalViolet.withAlpha(0.95f), bar.getHeight());
            g.setColour(signalViolet.interpolatedWith(juce::Colours::white, 0.75f));
            g.fillEllipse(juce::Rectangle<float>(3.5f, 3.5f)
                              .withCentre({at, bar.getCentreY()}));
        }
    }

    // A mixer fader. A slot cut into the panel, the part of it that has been
    // brought up lit, and a wide thumb across it: the thumb is what the hand
    // aims at and what the eye reads a balance from, so it is drawn as a bar
    // rather than as a cap with a line on it.
    //
    // It prints no value, for the same reason no knob does — the reading
    // appears in the bubble beside the hand asking for it. What is drawn beside
    // the slot instead is the mark for the level the fader opens at, which is
    // the one position on it worth finding without reading a number.
    //
    // Where the thumb sits is worked out from the slider's own value rather
    // than from the position JUCE passes in, which is a pixel coordinate along
    // the axis and not the proportion the rest of this panel is drawn from.
    void drawFader(juce::Graphics& g, juce::Rectangle<int> bounds, juce::Slider& slider)
    {
        const auto area = bounds.toFloat().reduced(1.0f);
        const auto accent = slider.findColour(juce::Slider::rotarySliderFillColourId);
        const auto enabled = slider.isEnabled();
        const auto alpha = enabled ? 1.0f : 0.35f;

        const auto range = slider.getRange();
        const auto proportionOf = [range] (double plain) { return faderProportion(range, plain); };

        const auto slotWidth = juce::jmin(9.0f, area.getWidth());
        const auto yFor = [&area] (float proportion)
        {
            return faderThumbY(area, proportion);
        };
        const auto thumbY = yFor(proportionOf(slider.getValue()));
        const auto slot = juce::Rectangle<float>(slotWidth, area.getHeight())
                              .withCentre(area.getCentre());

        g.setColour(juce::Colour(0xff0b0e18).withAlpha(enabled ? 1.0f : 0.5f));
        g.fillRoundedRectangle(slot, 3.0f);
        if (thumbY < slot.getBottom() - 1.0f)
        {
            g.setColour(accent.withAlpha(alpha * 0.45f));
            g.fillRoundedRectangle(slot.withTop(thumbY), 3.0f);
        }
        g.setColour(line.withAlpha(alpha * 0.9f));
        g.drawRoundedRectangle(slot, 3.0f, 1.0f);

        // The level the fader opens at, ticked down both sides of the slot
        // rather than across it so the thumb never covers it.
        const auto markY = yFor(proportionOf(slider.getDoubleClickReturnValue()));
        const auto tick = juce::jmax(2.0f, (area.getWidth() - slotWidth) * 0.5f - 2.0f);
        g.setColour(line.withAlpha(alpha));
        g.fillRect(area.getX(), markY - 0.5f, tick, 1.0f);
        g.fillRect(area.getRight() - tick, markY - 0.5f, tick, 1.0f);

        const auto thumb = juce::Rectangle<float>(area.getWidth(), faderThumbHeight)
                               .withCentre({area.getCentreX(), thumbY});
        g.setColour(juce::Colour(0xff0b0e18).withAlpha(alpha));
        g.fillRoundedRectangle(thumb, 2.0f);
        g.setColour(juce::Colour(0xff39405c).withAlpha(alpha));
        g.drawRoundedRectangle(thumb.reduced(0.5f), 2.0f, 1.0f);
        g.setColour(accent.withAlpha(alpha));
        g.fillRoundedRectangle(thumb.reduced(2.5f, 2.5f), 1.0f);
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

        // A knob is two parts, not one disc: a dark collar with the scale
        // engraved into it, and a smaller cap sitting proud in the middle. That
        // separation is the whole of why a machined knob reads as machined —
        // one disc with a rim around it reads as a circle of paint however well
        // the gradient is chosen, which is what the first pass at this was.
        //
        // The collar's outer edge stays where the body always was, at 0.81 of
        // the radius, because the modulation ring outside it and the band the
        // ring is grabbed in are measured against that. Everything new here
        // happens inside it.
        const auto body = knob.reduced(radius * 0.19f);

        // The shadow the whole knob throws onto the plate.
        g.setColour(juce::Colours::black.withAlpha(enabled ? 0.45f : 0.16f));
        g.fillEllipse(body.translated(0.0f, 2.0f).expanded(1.6f));

        // The collar.
        g.setColour(juce::Colour(0xff090b11).withAlpha(enabled ? 1.0f : 0.55f));
        g.fillEllipse(body.expanded(1.0f));
        juce::ColourGradient collar(plateEdgeLit.withAlpha(enabled ? 0.5f : 0.18f),
                                    body.getCentreX(), body.getY(),
                                    juce::Colours::black.withAlpha(0.0f),
                                    body.getCentreX(), body.getCentreY(), false);
        g.setGradientFill(collar);
        g.drawEllipse(body.reduced(0.4f), 1.2f);

        // The scale, engraved into the collar rather than printed on the plate
        // around it. Every third mark is longer, so the travel can be read in
        // thirds without counting.
        juce::Path ticks;
        for (int i = 0; i <= 18; ++i)
        {
            const auto tickAngle = juce::jmap(static_cast<float>(i) / 18.0f, startAngle, endAngle);
            const auto inner = polar(radius * (i % 3 == 0 ? 0.64f : 0.68f), tickAngle);
            ticks.startNewSubPath(inner); ticks.lineTo(polar(radius * 0.76f, tickAngle));
        }
        g.setColour(plateEdgeLit.withAlpha(enabled ? 0.55f : 0.18f));
        g.strokePath(ticks, juce::PathStrokeType(1.0f));

        // The cap. A machined rim lit along its top edge and black along its
        // foot — the same bevel a plate has, bent into a circle — with an
        // anodised dome inside it.
        const auto cap = body.reduced(body.getWidth() * 0.5f - radius * 0.52f);
        juce::ColourGradient rim(juce::Colour(0xff858da1).withAlpha(enabled ? 1.0f : 0.4f),
                                 cap.getCentreX(), cap.getY(),
                                 juce::Colour(0xff04060c).withAlpha(enabled ? 1.0f : 0.4f),
                                 cap.getCentreX(), cap.getBottom(), false);
        g.setGradientFill(rim);
        g.fillEllipse(cap.expanded(juce::jmax(2.2f, radius * 0.1f)));

        juce::ColourGradient dome(juce::Colour(0xff32363f), cap.getX() + cap.getWidth() * 0.28f,
                                  cap.getY() + cap.getHeight() * 0.18f,
                                  juce::Colour(0xff0a0c10), cap.getRight(), cap.getBottom(), true);
        dome.addColour(0.55, juce::Colour(0xff171a21));
        g.setGradientFill(dome);
        g.fillEllipse(cap);

        // The specular: a sliver of the lamp above the desk, across the top of
        // the dome. Sized to sit inside the cap rather than clipped to it — a
        // clip push per knob per frame measured at seven points of one core on
        // its own, and an ellipse that already fits needs no clip.
        {
            const auto gloss = cap.reduced(cap.getWidth() * 0.16f, cap.getHeight() * 0.3f)
                                  .translated(0.0f, -cap.getHeight() * 0.16f);
            juce::ColourGradient sheen(juce::Colours::white.withAlpha(enabled ? 0.14f : 0.04f),
                                       gloss.getCentreX(), gloss.getY(),
                                       juce::Colours::white.withAlpha(0.0f),
                                       gloss.getCentreX(), gloss.getBottom(), false);
            g.setGradientFill(sheen);
            g.fillEllipse(gloss);
        }

        // The one hard highlight on the knob: a crisp line along the cap's top
        // edge. Stroked with a gradient rather than a flat colour so it is
        // brightest at twelve o'clock and gone by the sides, which is where the
        // light on this panel comes from.
        {
            juce::Path crown;
            crown.addCentredArc(centre.x, centre.y, cap.getWidth() * 0.5f, cap.getHeight() * 0.5f,
                                0.0f, -juce::MathConstants<float>::halfPi,
                                juce::MathConstants<float>::halfPi, true);
            juce::ColourGradient light(juce::Colours::white.withAlpha(enabled ? 0.45f : 0.12f),
                                       cap.getCentreX(), cap.getY(),
                                       juce::Colours::white.withAlpha(0.0f),
                                       cap.getCentreX(), cap.getCentreY(), false);
            g.setGradientFill(light);
            g.strokePath(crown, juce::PathStrokeType(1.3f));
        }

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

        // The indicator, run out to just short of the lit rim so the line and
        // the LED read as one reading rather than two marks at odd radii. It is
        // cut into the cap rather than printed on it: a dark line a pixel below
        // the light one is the whole of that, and it is what stops the pointer
        // looking like a sticker on a dome.
        // The indicator, cut into the cap and stopping inside it — the lit mark
        // out on the scale is what says where the value is, and the line is
        // what lets the angle be judged from across the panel. Nothing is drawn
        // at the centre: the reference has no mark there, and with an LED
        // already lit out on the collar a second accent dot in the middle was
        // the knob saying the same thing twice.
        juce::Path pointer;
        pointer.startNewSubPath(polar(radius * 0.1f, angle));
        pointer.lineTo(polar(radius * 0.44f, angle));
        const auto stroke = juce::PathStrokeType(2.2f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded);
        g.setColour(juce::Colours::black.withAlpha(enabled ? 0.6f : 0.2f));
        g.strokePath(pointer, stroke, juce::AffineTransform::translation(0.0f, 1.3f));
        g.setColour(text.withAlpha(enabled ? 1.0f : 0.35f));
        g.strokePath(pointer, stroke);
    }
};
}
