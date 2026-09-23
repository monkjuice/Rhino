#pragma once

#include "ForgeDisplays.h"
#include "../core/ForgeFilter.h"

// The filter's own response, drawn from the transfer functions in
// ForgeFilter.h rather than from a generic curve — so what the display claims
// is being removed is what is being removed, including the peak resonance puts
// back at the corner.
namespace rhino::forge::ui
{
// --- The filter ---------------------------------------------------------------
//
// What the filter is taking out, drawn as the response it actually has. The
// arithmetic is not here: `filterMagnitude` in ForgeFilter.h is the same
// function the engine's coefficients are worked out beside, so a curve on this
// display cannot be a reading of a filter the voice is not running. This file
// is the window it is drawn in — the axes, the grid, the fill and the marker.

// The window every filter display is drawn against: the audible band, and a
// decibel range with room above unity for a resonant peak to rise into.
inline constexpr float filterLowHz = 20.0f;
inline constexpr float filterHighHz = 20000.0f;
inline constexpr float filterTopDb = 18.0f;
inline constexpr float filterBottomDb = -48.0f;

// Frequency is read across a log axis, because that is how pitch is heard: an
// octave takes the same width wherever it sits.
inline float filterHzToX(juce::Rectangle<float> box, float hz)
{
    const auto at = std::log(juce::jlimit(filterLowHz, filterHighHz, hz) / filterLowHz)
                    / std::log(filterHighHz / filterLowHz);
    return box.getX() + at * box.getWidth();
}

inline float filterXToHz(juce::Rectangle<float> box, float x)
{
    if (box.getWidth() <= 0.0f) return filterLowHz;
    const auto at = juce::jlimit(0.0f, 1.0f, (x - box.getX()) / box.getWidth());
    return filterLowHz * std::exp(at * std::log(filterHighHz / filterLowHz));
}

inline float filterDbToY(juce::Rectangle<float> box, float db)
{
    const auto at = (filterTopDb - juce::jlimit(filterBottomDb, filterTopDb, db))
                    / (filterTopDb - filterBottomDb);
    return box.getY() + at * box.getHeight();
}

// The gain a shape has at one frequency, clipped into the window it is drawn
// in. The gain itself comes from ForgeFilter.h; all this adds is the decibels
// and the ceiling.
inline float filterMagnitudeDb(const rhino::forge::FilterShape& shape, float hz)
{
    const auto gain = rhino::forge::filterMagnitude(shape, hz);
    if (!std::isfinite(gain) || gain <= 1.0e-6f) return filterBottomDb;
    return juce::jlimit(filterBottomDb, filterTopDb, 20.0f * std::log10(gain));
}

// The response across the whole window. Sampled per pixel of width rather than
// at a fixed count, so a resonant spike is not stepped over on a wide panel and
// no time is spent oversampling a narrow one.
//
// A comb tuned high has more teeth than the panel has pixels, and no sampled
// curve can draw those. What is drawn is one tooth per pixel, which reads as
// the band the teeth live in — the honest limit of a curve this wide, and part
// of why a comb is set by ear at the bottom of the knob rather than by eye at
// the top of it.
inline juce::Path filterResponsePath(juce::Rectangle<float> box,
                                     const rhino::forge::FilterShape& shape)
{
    juce::Path path;
    const auto points = juce::jlimit(48, 512, juce::roundToInt(box.getWidth()));
    for (int i = 0; i <= points; ++i)
    {
        const auto x = box.getX() + box.getWidth() * static_cast<float>(i) / static_cast<float>(points);
        const auto y = filterDbToY(box, filterMagnitudeDb(shape, filterXToHz(box, x)));
        if (i == 0) path.startNewSubPath(x, y);
        else path.lineTo(x, y);
    }
    return path;
}

inline juce::String filterHzText(float hz)
{
    if (hz >= 1000.0f) return juce::String(hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz";
    return juce::String(juce::roundToInt(hz)) + " Hz";
}

// What the corner marker is called. A formant filter has no corner — the knob
// moves the mouth instead — so it says which vowel rather than inventing a
// frequency for one.
inline juce::String filterCornerText(const rhino::forge::FilterShape& shape)
{
    if (shape.type == rhino::forge::FilterType::formant)
        return juce::String("VOWEL ") + rhino::forge::filterVowelName(shape.cutoff);
    return filterHzText(shape.cutoff);
}

// A short label for a scale mark, which has no room for a unit.
inline juce::String filterDecadeText(float hz)
{
    if (hz >= 1000.0f) return juce::String(juce::roundToInt(hz / 1000.0f)) + "k";
    return juce::String(juce::roundToInt(hz));
}

// The strip along the foot of the display where the frequencies are named, and
// the box the response itself is drawn in above it.
//
// The names sit outside the box rather than along the bottom of it, which is
// where they used to be. A label inside the box is something the curve has to
// be read *through*, and the one place a low pass is most often read — the last
// octave, where the skirt is heading for the floor — is exactly where they sat.
inline constexpr int filterScaleHeight = 12;

inline juce::Rectangle<int> filterCurveBounds(juce::Rectangle<int> plot)
{
    return plot.withTrimmedBottom(filterScaleHeight);
}

inline juce::Rectangle<int> filterScaleBounds(juce::Rectangle<int> plot)
{
    return plot.withTop(plot.getBottom() - filterScaleHeight);
}

// The frequencies the axis is marked at: the ends of the band and the decades
// between them. Five is what fits without the labels touching at the narrowest
// the module ever is.
inline const std::array<float, 5>& filterScaleMarks()
{
    static const std::array<float, 5> marks {20.0f, 100.0f, 1000.0f, 10000.0f, 20000.0f};
    return marks;
}

// The rulings: decades across, and a few lines of gain down. Without them the
// curve is a shape; with them it is a reading.
//
// Between the decades sit the 2, 3, 5 and 7 of each — the marks of a log scale,
// faint enough to read as ruling rather than as content. They are what makes
// the spacing legible as logarithmic: three evenly spaced lines say nothing
// about what is between them, and the ear hears an octave, not a decade.
inline void drawFilterGrid(juce::Graphics& g, juce::Rectangle<float> box, float alpha)
{
    for (const auto decade : {10.0f, 100.0f, 1000.0f, 10000.0f})
        for (const auto step : {2.0f, 3.0f, 5.0f, 7.0f})
        {
            const auto hz = decade * step;
            if (hz <= filterLowHz || hz >= filterHighHz) continue;
            g.setColour(line.withAlpha(0.20f * alpha));
            g.drawVerticalLine(juce::roundToInt(filterHzToX(box, hz)), box.getY(), box.getBottom());
        }
    for (const auto hz : {100.0f, 1000.0f, 10000.0f})
    {
        g.setColour(line.withAlpha(0.45f * alpha));
        g.drawVerticalLine(juce::roundToInt(filterHzToX(box, hz)), box.getY(), box.getBottom());
    }
    // Gain, every twelve decibels: one line an octave of a twelve-decibel
    // skirt, so a slope can be counted off the grid rather than guessed at.
    for (const auto db : {12.0f, -12.0f, -24.0f, -36.0f})
    {
        const auto y = filterDbToY(box, db);
        if (y <= box.getY() + 1.0f || y >= box.getBottom() - 1.0f) continue;
        g.setColour(line.withAlpha(0.16f * alpha));
        g.drawHorizontalLine(juce::roundToInt(y), box.getX(), box.getRight());
    }
    g.setColour(line.withAlpha(0.38f * alpha));
    g.drawHorizontalLine(juce::roundToInt(filterDbToY(box, 0.0f)), box.getX(), box.getRight());
}

// The axis, named — laid out from the two ends inwards rather than by putting
// every mark where its ruling is.
//
// The two ends are what say how far the axis reaches, so they are drawn first
// and pulled inside the box: centred on their own marks they would hang half of
// each over the edge of the display. Every decade between them is then drawn
// only where it clears what is already down.
//
// That rule is here because 10k and 20k are a tenth of this axis apart — the
// last decade is one octave wide — and their labels are not. Laid out
// independently they overlap into a smear at every width the module is ever
// drawn at, which is exactly what a fixed set of marks did.
inline void drawFilterScale(juce::Graphics& g, juce::Rectangle<int> plot, float alpha)
{
    const auto strip = filterScaleBounds(plot).toFloat();
    const auto box = filterCurveBounds(plot).toFloat();
    const auto font = panelFont(Face::reading, 9.0f);
    g.setColour(mutedText.withAlpha(0.6f * alpha));
    g.setFont(font);

    const auto& marks = filterScaleMarks();
    const auto widthOf = [&font] (const juce::String& text)
    {
        return juce::GlyphArrangement::getStringWidth(font, text);
    };
    const auto put = [&g, strip] (const juce::String& text, float at, float width)
    {
        g.drawText(text, juce::Rectangle<float>(at, strip.getY(), width, strip.getHeight()).toNearestInt(),
                   juce::Justification::centred);
    };

    const auto low = filterDecadeText(marks.front());
    const auto high = filterDecadeText(marks.back());
    const auto lowWidth = widthOf(low);
    const auto highWidth = widthOf(high);
    put(low, box.getX(), lowWidth);
    put(high, box.getRight() - highWidth, highWidth);

    // Clear of the mark before it and of the one at the far end. Six pixels,
    // which is about a character: two numbers a hair apart read as one number.
    constexpr float clearance = 6.0f;
    auto taken = box.getX() + lowWidth;
    const auto ceiling = box.getRight() - highWidth;
    for (size_t i = 1; i + 1 < marks.size(); ++i)
    {
        const auto text = filterDecadeText(marks[i]);
        const auto width = widthOf(text);
        const auto at = filterHzToX(box, marks[i]) - width * 0.5f;
        if (at < taken + clearance || at + width > ceiling - clearance) continue;
        put(text, at, width);
        taken = at + width;
    }
}

// The response, with what passes filled under the curve and what is being taken
// out washed in above it. The two regions meet along the curve, so the band the
// filter is removing is a shape on the display rather than something to be
// inferred from where the line happens to fall.
inline void drawFilterResponse(juce::Graphics& g, juce::Rectangle<int> area,
                               const rhino::forge::FilterShape& shape,
                               juce::Colour colour, float alpha)
{
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(area);

    drawFilterScale(g, area, alpha);
    const auto plot = filterCurveBounds(area);
    const auto box = plot.toFloat().reduced(1.0f);
    // The curve has a box of its own inside the well, as the reference draws
    // it: the well now holds the selector and the routing strip as well, and
    // without a frame the response reads as floating between them rather than
    // as the thing they are attached to.
    g.setColour(juce::Colour(0xff070a12).withAlpha(0.55f * alpha));
    g.fillRect(plot);
    g.setColour(line.withAlpha(0.45f * alpha));
    g.drawRect(plot, 1);
    g.reduceClipRegion(plot.reduced(1));
    drawFilterGrid(g, box, alpha);

    const auto path = filterResponsePath(box, shape);
    const auto unity = filterDbToY(box, 0.0f);

    // Everything under the curve: the part of the band that is getting through.
    auto passing = path;
    passing.lineTo(box.getRight(), box.getBottom());
    passing.lineTo(box.getX(), box.getBottom());
    passing.closeSubPath();
    g.setGradientFill({colour.withAlpha(0.30f * alpha), box.getCentreX(), box.getY(),
                       colour.withAlpha(0.05f * alpha), box.getCentreX(), box.getBottom(), false});
    g.fillPath(passing);

    // Everything between the curve and unity: the part being taken out. Clipped
    // below the unity line so a resonant peak, which is gain rather than loss,
    // is not shaded as though it were being removed.
    {
        juce::Graphics::ScopedSaveState cut(g);
        g.reduceClipRegion(juce::Rectangle<float>(box.getX(), unity, box.getWidth(),
                                                  box.getBottom() - unity).toNearestInt());
        auto removed = path;
        removed.lineTo(box.getRight(), box.getY());
        removed.lineTo(box.getX(), box.getY());
        removed.closeSubPath();
        g.setColour(mutedText.withAlpha(0.12f * alpha));
        g.fillPath(removed);
    }

    strokeGlow(g, path, colour, alpha);

    // The corner itself, marked and named: the display is here to say which
    // frequencies are going, and this is the one the knob is holding.
    const auto x = filterHzToX(box, shape.cutoff);
    g.setColour(colour.withAlpha(0.45f * alpha));
    g.drawVerticalLine(juce::roundToInt(x), box.getY(), box.getBottom());
    // The second corner, where the type has one, marked more faintly than the
    // first: a dual filter has two frequencies and only one of them is under
    // the CUTOFF knob, so the display has to say where the other one went.
    if (rhino::forge::filterCategoryOf(shape.type) == rhino::forge::FilterCategory::dual)
    {
        const auto second = filterHzToX(box, rhino::forge::filterSecondHz(shape));
        g.setColour(colour.withAlpha(0.22f * alpha));
        g.drawVerticalLine(juce::roundToInt(second), box.getY(), box.getBottom());
    }
    g.setColour(colour.withAlpha(alpha));
    const auto readingFont = panelFont(Face::reading, 10.0f);
    g.setFont(readingFont);
    const auto reading = filterCornerText(shape);
    const auto width = juce::jmax(46.0f, juce::GlyphArrangement::getStringWidth(readingFont, reading) + 8.0f);
    // Beside the marker, on whichever side of it there is room for.
    const auto right = x + 4.0f + width <= box.getRight();
    g.drawText(reading,
               juce::Rectangle<float>(right ? x + 4.0f : x - 4.0f - width, box.getY() + 2.0f, width, 12.0f)
                   .toNearestInt(),
               right ? juce::Justification::centredLeft : juce::Justification::centredRight);

    // A type whose curve is not its own transfer function says so, because a
    // flat line means two quite different things: on the diffusor it means
    // "this moves the phase and not the magnitude", and on a low pass it would
    // mean the filter was broken.
    if (!rhino::forge::filterHasResponse(shape.type))
    {
        g.setColour(mutedText.withAlpha(0.7f * alpha));
        g.setFont(panelFont(Face::reading, 9.0f));
        g.drawText("PHASE ONLY", box.withTrimmedBottom(box.getHeight() * 0.5f).toNearestInt(),
                   juce::Justification::centredTop);
    }
}
}
