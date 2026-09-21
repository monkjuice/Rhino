#pragma once

#include "ForgeDisplays.h"

// The envelope and LFO displays: the axis a zoom setting means, the shape four
// times and a sustain make, the running indicator, and the grid behind them.
namespace rhino::forge::ui
{
// --- The envelope -----------------------------------------------------------

// Envelope stages, matching Core's ordering.
enum class Stage { idle, attack, decay, sustain, release };

// How long each stage lasts: what its knob says, at every sustain level.
//
// Sustain sets the level a stage arrives at, never how long one runs for, so it
// is deliberately not consulted here. An earlier version did consult it, on the
// grounds that Core leaves the decay stage the instant it enters it when there
// is nothing to fall to, and leaves release at once when sustain is nothing —
// and it collapsed those segments to no width at all. That was the wrong thing
// to model. This draws amplitude against time, not which stage the state
// machine is in, and the amplitude is perfectly well defined in both cases: full
// for the length of the decay when sustain is full, and nothing for the length
// of the release when sustain is nothing. Drawing them flat is truthful, keeps
// the DECAY and RELEASE knobs live at the ends of the sustain range instead of
// leaving them apparently dead, and closes a jump the engine does not have —
// decay took its whole time at 99.9% sustain and none at all at 100%.
struct EnvelopeTimes
{
    float attack = 0.0f, decay = 0.0f, release = 0.0f;
    float total() const { return attack + decay + release; }
};

inline EnvelopeTimes envelopeTimes(float attack, float decay, float release)
{
    return {juce::jmax(0.0f, attack), juce::jmax(0.0f, decay), juce::jmax(0.0f, release)};
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

// The window every envelope opens on, one per envelope. Written as a function
// so the panel's array of them cannot be one short of the envelopes there are.
inline constexpr std::array<int, envCount> envelopeZoomDefaults()
{
    std::array<int, envCount> zooms {};
    for (auto& zoom : zooms) zoom = envelopeDefaultZoom;
    return zooms;
}

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
    const auto times = envelopeTimes(attack, decay, release);
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
        g.setFont(panelFont(Face::emphasis, 13.0f));
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
    g.setFont(panelFont(Face::reading, 9.0f));
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
    g.setFont(panelFont(Face::reading, 9.0f));
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

// Idle names nothing, because idle is not a stage the envelope is in — it is
// the envelope not running. What the header says instead is whatever that
// particular envelope is for, which is a question about ENV 1 against ENV 2-4
// rather than about the state machine, so the panel answers it.
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
    return "";
}

// An LFO's shape, drawn as exactly one cycle with an indicator riding it. One
// cycle rather than several, and rather than a count that grows with the rate,
// so the width of the display is the length of the cycle: the indicator then
// sweeps the whole thing and its position is the phase, read directly. Drawing
// two cycles left the indicator stuck in the left-hand half, because a phase
// only ever covers one of them. The rate is written in the module header, so the
// picture does not have to carry it too.
//
// The indicator is the engine's own running phase, not an animation timed in the
// editor, so it cannot drift away from what is being heard.
inline juce::Rectangle<int> lfoPlotBounds(juce::Rectangle<int> area)
{
    return area.reduced(1, 8).withTrimmedBottom(19);
}

inline juce::Rectangle<int> lfoFooterBounds(juce::Rectangle<int> area)
{
    return area.withTop(area.getBottom() - 25);
}

inline juce::Rectangle<int> lfoColumnBounds(juce::Rectangle<int> area)
{
    return {area.getRight() - 126, area.getBottom() - 23, 56, 20};
}

inline juce::Rectangle<int> lfoRowBounds(juce::Rectangle<int> area)
{
    return {area.getRight() - 64, area.getBottom() - 23, 56, 20};
}

inline juce::Rectangle<int> lfoNameBounds(juce::Rectangle<int> area)
{
    return {area.getX() + 6, area.getBottom() - 23,
            juce::jmax(55, juce::jmin(200, lfoColumnBounds(area).getX() - area.getX() - 68)), 20};
}

inline juce::Rectangle<int> lfoPreviousBounds(juce::Rectangle<int> area)
{
    return {lfoNameBounds(area).getRight() + 4, area.getBottom() - 23, 22, 20};
}

inline juce::Rectangle<int> lfoNextBounds(juce::Rectangle<int> area)
{
    return lfoPreviousBounds(area).translated(24, 0);
}

inline juce::Rectangle<int> lfoGridStepBounds(juce::Rectangle<int> field, bool increase)
{
    auto arrows = field.withLeft(field.getRight() - 14);
    return increase ? arrows.removeFromTop(arrows.getHeight() / 2) : arrows.withTrimmedTop(arrows.getHeight() / 2);
}

inline void drawLfo(juce::Graphics& g, juce::Rectangle<int> area, rhino::forge::LfoShape shape,
                    const rhino::forge::LfoTable& table, const juce::String& name,
                    float phase, float held, juce::Colour colour, float alpha)
{
    // Clipped to the well and drawn its full width, so one cycle spans edge to
    // edge and the curve meets both sides instead of floating inside a margin.
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(displayClip(area));

    const auto box = lfoPlotBounds(area).toFloat();
    const auto plot = [&] (float at) { return box.getCentreY() - at * box.getHeight() * 0.48f; };

    g.setColour(colour.withAlpha(0.12f * alpha));
    for (int i = 1; i < table.columns; ++i)
        g.drawVerticalLine(juce::roundToInt(box.getX() + box.getWidth() * i / table.columns),
                           box.getY(), box.getBottom());
    for (int i = 1; i < table.rows; ++i)
        g.drawHorizontalLine(juce::roundToInt(box.getY() + box.getHeight() * i / table.rows),
                             box.getX(), box.getRight());

    juce::Path path;
    if (!table.custom && shape == rhino::forge::LfoShape::sampleHold)
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
            const auto at = table.custom ? table.sample(along) : rhino::forge::lfoWave(shape, along, held);
            const auto x = box.getX() + along * box.getWidth();
            if (i == 0) path.startNewSubPath(x, plot(at)); else path.lineTo(x, plot(at));
        }
    }
    strokeGlow(g, path, colour, alpha);

    const auto pointCount = table.custom ? table.count : 9;
    for (int i = 0; i < pointCount; ++i)
    {
        const auto along = table.custom ? table.points[static_cast<size_t>(i)].x : i / 8.0f;
        const auto value = table.custom ? table.points[static_cast<size_t>(i)].y
                                        : rhino::forge::lfoWave(shape, along, held);
        g.setColour(juce::Colours::white.withAlpha(0.8f * alpha));
        g.fillEllipse(juce::Rectangle<float>(5.0f, 5.0f).withCentre(
            {box.getX() + along * box.getWidth(), plot(value)}));
    }

    // One cycle wide, so the phase is the position along it directly and the
    // indicator always sits on the curve it is drawn over.
    const auto at = juce::jlimit(0.0f, 1.0f, phase);
    const auto x = box.getX() + at * box.getWidth();
    const auto y = plot(table.custom ? table.sample(phase)
                                     : rhino::forge::lfoWave(shape, phase, held));
    g.setColour(colour.withAlpha(0.3f * alpha));
    g.drawVerticalLine(juce::roundToInt(x), box.getY(), box.getBottom());
    g.setColour(juce::Colours::white.withAlpha(0.9f * alpha));
    g.fillEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre({x, y}));
    g.setColour(colour);
    g.fillEllipse(juce::Rectangle<float>(4.0f, 4.0f).withCentre({x, y}));

    const auto footer = lfoFooterBounds(area);
    g.setColour(juce::Colour(0xff0a0d16));
    g.fillRect(footer);
    g.setColour(colour.withAlpha(0.24f * alpha));
    g.drawHorizontalLine(footer.getY(), area.getX(), area.getRight());

    const auto nameBox = lfoNameBounds(area);
    g.setColour(juce::Colour(0xff151925));
    g.fillRoundedRectangle(nameBox.toFloat(), 3.0f);
    g.setColour(colour.withAlpha(0.28f * alpha));
    g.drawRoundedRectangle(nameBox.toFloat().reduced(0.5f), 3.0f, 1.0f);
    g.setColour(colour.withAlpha(alpha));
    g.setFont(juce::FontOptions(12.0f));
    const auto label = table.custom ? name : "Default / " + juce::String(rhino::forge::lfoFullShapeName(static_cast<int>(shape)));
    g.drawText(label, nameBox.reduced(8, 0).withTrimmedRight(13), juce::Justification::centredLeft, true);
    juce::Path down;
    down.addTriangle(static_cast<float>(nameBox.getRight() - 14), static_cast<float>(nameBox.getCentreY() - 2),
                     static_cast<float>(nameBox.getRight() - 6), static_cast<float>(nameBox.getCentreY() - 2),
                     static_cast<float>(nameBox.getRight() - 10), static_cast<float>(nameBox.getCentreY() + 2));
    g.fillPath(down);

    const auto chevron = [&] (juce::Rectangle<int> button, bool right)
    {
        const auto cx = static_cast<float>(button.getCentreX());
        const auto cy = static_cast<float>(button.getCentreY());
        const auto direction = right ? 1.0f : -1.0f;
        g.drawLine(cx - direction * 2.0f, cy - 4.0f, cx + direction * 2.0f, cy, 1.5f);
        g.drawLine(cx + direction * 2.0f, cy, cx - direction * 2.0f, cy + 4.0f, 1.5f);
    };
    chevron(lfoPreviousBounds(area), false);
    chevron(lfoNextBounds(area), true);

    const auto gridField = [&] (juce::Rectangle<int> field, int count, bool columns)
    {
        g.setColour(juce::Colour(0xff151925));
        g.fillRoundedRectangle(field.toFloat(), 3.0f);
        g.setColour(colour.withAlpha(0.28f * alpha));
        g.drawRoundedRectangle(field.toFloat().reduced(0.5f), 3.0f, 1.0f);
        g.setColour(colour.withAlpha(alpha));
        for (int i = 0; i < 3; ++i)
            if (columns) g.drawVerticalLine(field.getX() + 7 + i * 3, field.getY() + 5, field.getBottom() - 5);
            else g.drawHorizontalLine(field.getY() + 6 + i * 3, field.getX() + 5, field.getX() + 13);
        g.drawText(juce::String(count), field.withTrimmedLeft(17).withTrimmedRight(13),
                   juce::Justification::centred, true);
        for (const auto increase : {true, false})
        {
            const auto step = lfoGridStepBounds(field, increase);
            const auto cx = static_cast<float>(step.getCentreX());
            const auto cy = static_cast<float>(step.getCentreY());
            juce::Path triangle;
            if (increase) triangle.addTriangle(cx - 3.0f, cy + 1.0f, cx + 3.0f, cy + 1.0f, cx, cy - 2.0f);
            else triangle.addTriangle(cx - 3.0f, cy - 1.0f, cx + 3.0f, cy - 1.0f, cx, cy + 2.0f);
            g.fillPath(triangle);
        }
    };
    gridField(lfoColumnBounds(area), table.columns, true);
    gridField(lfoRowBounds(area), table.rows, false);
}
}
