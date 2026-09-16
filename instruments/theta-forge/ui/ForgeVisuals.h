#pragma once

#include "ForgeLayout.h"
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
        auto area = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                            static_cast<float>(width), static_cast<float>(height)).reduced(4.0f);
        const auto diameter = juce::jmin(area.getWidth(), area.getHeight());
        auto knob = juce::Rectangle<float>(diameter, diameter).withCentre(area.getCentre());
        const auto centre = knob.getCentre();
        const auto radius = diameter * 0.5f;
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
        // move it, faint, because a reach is potential rather than a value; and
        // where the modulation actually has it this instant, bright, with a
        // marker at the end. That second arc fills and empties as the source
        // plays, which is what makes an envelope or an LFO visible on the knob
        // it is driving rather than only in the module it comes from.
        const auto& properties = slider.getProperties();
        const auto depth = static_cast<float>(properties.getWithDefault("modDepth", 0.0));
        const auto offset = static_cast<float>(properties.getWithDefault("modOffset", 0.0));
        if ((depth != 0.0f || offset != 0.0f) && enabled)
        {
            const auto ring = radius * 0.88f;
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

            arc(angleAt(depth), signalViolet.withAlpha(0.32f), 3.0f);
            if (offset != 0.0f)
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

inline float waveform(float phase, float position)
{
    const auto sine = std::sin(phase * juce::MathConstants<float>::twoPi);
    const auto triangle = 1.0f - 4.0f * std::abs(phase - 0.5f);
    const auto saw = phase * 2.0f - 1.0f;
    const auto square = phase < 0.5f ? 1.0f : -1.0f;
    const float frames[] {sine, triangle, saw, square};
    const auto scaled = juce::jlimit(0.0f, 1.0f, position) * 3.0f;
    const auto index = std::min(2, static_cast<int>(scaled));
    return juce::jmap(scaled - static_cast<float>(index), frames[index], frames[index + 1]);
}

inline void strokeGlow(juce::Graphics& g, const juce::Path& path, juce::Colour colour, float alpha)
{
    g.setColour(colour.withAlpha(0.16f * alpha));
    g.strokePath(path, juce::PathStrokeType(7.0f));
    g.setColour(colour.withAlpha(alpha));
    g.strokePath(path, juce::PathStrokeType(1.8f));
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

inline void drawDisplayWell(juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto box = area.toFloat();
    g.setColour(juce::Colour(0xff0b0e18));
    g.fillRoundedRectangle(box, 3.0f);
    g.setColour(line.withAlpha(0.6f));
    g.drawRoundedRectangle(box, 3.0f, 1.0f);
    g.setColour(line.withAlpha(0.45f));
    g.drawHorizontalLine(area.getCentreY(), box.getX() + 4.0f, box.getRight() - 4.0f);
}

inline void drawWaveform(juce::Graphics& g, juce::Rectangle<int> area, float position,
                         juce::Colour colour, float alpha)
{
    const auto face = crtFace(area);
    const auto box = face.reduced(6.0f, 5.0f);
    juce::Path path;
    constexpr int points = 180;
    for (int i = 0; i <= points; ++i)
    {
        const auto phase = static_cast<float>(i) / static_cast<float>(points);
        const auto x = box.getX() + phase * box.getWidth();
        const auto y = box.getCentreY() - waveform(phase, position) * box.getHeight() * 0.44f;
        if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
    }
    // Clipped to the glass, so the halo stops at the bezel rather than spilling
    // over the tube's edge.
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(crtPath(face));
    strokePhosphor(g, path, colour, alpha);
}

// Envelope stages, matching Core's ordering.
enum class Stage { idle, attack, decay, sustain, release };

// The current ADSR, with a playhead showing where a sounding note has reached.
// The playhead's height is the envelope's real value, so a note released
// during its attack visibly falls from the level it actually got to rather
// than from the sustain line.
inline void drawEnvelope(juce::Graphics& g, juce::Rectangle<int> area, float attack, float decay,
                         float sustain, float release, juce::Colour colour, float alpha,
                         Stage stage = Stage::idle, float level = 0.0f)
{
    const auto box = area.toFloat().reduced(8.0f, 9.0f);
    const auto span = attack + decay + release + 0.001f;
    const auto sustainWidth = box.getWidth() * 0.22f;
    const auto scale = (box.getWidth() - sustainWidth) / span;
    const auto floorY = box.getBottom();
    const auto peakY = box.getY();
    const auto held = juce::jlimit(0.0f, 1.0f, sustain);
    const auto sustainY = juce::jmap(held, floorY, peakY);

    const auto attackX = box.getX() + attack * scale;
    const auto decayX = attackX + decay * scale;
    const auto sustainX = decayX + sustainWidth;
    const auto endX = sustainX + release * scale;

    juce::Path path;
    path.startNewSubPath(box.getX(), floorY);
    path.lineTo(attackX, peakY);
    path.quadraticTo(attackX + (decayX - attackX) * 0.4f, sustainY, decayX, sustainY);
    path.lineTo(sustainX, sustainY);
    path.quadraticTo(sustainX + release * scale * 0.4f, floorY, endX, floorY);
    strokeGlow(g, path, colour, alpha);

    g.setColour(colour.withAlpha(0.25f * alpha));
    g.drawVerticalLine(juce::roundToInt(sustainX), sustainY, floorY);

    if (stage == Stage::idle) return;

    // Where along the drawn shape the note has reached. Each stage maps its own
    // progress onto its own segment; the plateau is held time, so sustain sits
    // at its end and release carries on from there without a jump.
    auto x = box.getX();
    switch (stage)
    {
        case Stage::attack:
            x = juce::jmap(juce::jlimit(0.0f, 1.0f, level), box.getX(), attackX);
            break;
        case Stage::decay:
            x = juce::jmap(juce::jlimit(0.0f, 1.0f, held < 1.0f ? (1.0f - level) / (1.0f - held) : 1.0f),
                           attackX, decayX);
            break;
        case Stage::sustain:
            x = sustainX;
            break;
        case Stage::release:
            x = juce::jmap(juce::jlimit(0.0f, 1.0f, held > 0.0f ? 1.0f - level / held : 1.0f),
                           sustainX, endX);
            break;
        case Stage::idle:
            break;
    }
    const auto y = juce::jmap(juce::jlimit(0.0f, 1.0f, level), floorY, peakY);

    g.setColour(colour.withAlpha(0.35f * alpha));
    g.drawVerticalLine(juce::roundToInt(x), y, floorY);
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

// A static read of the LFO shape. Shape selection and the running phase dot
// arrive with the LFO 1 milestone.
inline void drawLfo(juce::Graphics& g, juce::Rectangle<int> area, float cycles,
                    juce::Colour colour, float alpha)
{
    const auto box = area.toFloat().reduced(6.0f, 8.0f);
    juce::Path path;
    constexpr int points = 220;
    for (int i = 0; i <= points; ++i)
    {
        const auto phase = static_cast<float>(i) / static_cast<float>(points);
        const auto x = box.getX() + phase * box.getWidth();
        const auto y = box.getCentreY()
            - std::sin(phase * cycles * juce::MathConstants<float>::twoPi) * box.getHeight() * 0.42f;
        if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
    }
    strokeGlow(g, path, colour, alpha);
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
