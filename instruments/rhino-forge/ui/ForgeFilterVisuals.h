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

// A short label for a decade line, which has no room for a unit.
inline juce::String filterDecadeText(float hz)
{
    if (hz >= 1000.0f) return juce::String(juce::roundToInt(hz / 1000.0f)) + "k";
    return juce::String(juce::roundToInt(hz));
}

// Decade lines across the band, and unity across it. Without them the curve is
// a shape; with them it is a reading.
inline void drawFilterGrid(juce::Graphics& g, juce::Rectangle<float> box, float alpha)
{
    g.setFont(panelFont(Face::reading, 9.0f));
    for (const auto hz : {100.0f, 1000.0f, 10000.0f})
    {
        const auto x = filterHzToX(box, hz);
        g.setColour(line.withAlpha(0.5f * alpha));
        g.drawVerticalLine(juce::roundToInt(x), box.getY(), box.getBottom());
        g.setColour(mutedText.withAlpha(0.55f * alpha));
        g.drawText(filterDecadeText(hz),
                   juce::Rectangle<float>(x + 3.0f, box.getBottom() - 12.0f, 30.0f, 11.0f).toNearestInt(),
                   juce::Justification::centredLeft);
    }
    g.setColour(line.withAlpha(0.35f * alpha));
    g.drawHorizontalLine(juce::roundToInt(filterDbToY(box, 0.0f)), box.getX(), box.getRight());
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
    g.reduceClipRegion(displayClip(area));

    const auto box = area.toFloat().reduced(0.0f, 6.0f);
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
