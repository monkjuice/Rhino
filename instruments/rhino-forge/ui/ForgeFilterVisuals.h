#pragma once

#include "ForgeDisplays.h"

// The filter's own response, drawn from the transfer functions of Core's
// state-variable filter rather than from a generic curve — so what the
// display claims is being removed is what is being removed, including the
// peak resonance puts back at the corner.
namespace rhino::forge::ui
{
// --- The filter ---------------------------------------------------------------
//
// What the filter is taking out, drawn as the response it actually has. Core's
// filter is a zero-delay state variable, so all three taps come off one
// topology and the curves below are that topology's own transfer functions
// rather than a picture of a filter in general. With x the frequency in units
// of the cutoff and R Core's own damping term:
//
//     D  = (1 - x^2) + j 2R x
//     LP = 1 / D      BP = x / D      HP = x^2 / D
//
// The prototype is the analogue one. Core prewarps its cutoff, so the knee sits
// at the frequency the knob says whatever the sample rate is, and carrying the
// warp through the rest of the curve would move it by less than a pixel across
// the band the panel draws.

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

// Core's own damping term, named here so the two cannot drift apart.
inline float filterDamping(float resonance)
{
    return 1.0f / (1.0f + juce::jlimit(0.0f, 1.0f, resonance) * 15.0f);
}

// The gain this filter has at one frequency, in decibels.
inline float filterMagnitudeDb(rhino::forge::FilterType type, float cutoff, float resonance, float hz)
{
    const auto x = juce::jmax(1.0e-4f, hz) / juce::jmax(1.0e-4f, cutoff);
    const auto damping = filterDamping(resonance);
    const auto real = 1.0f - x * x;
    const auto imaginary = 2.0f * damping * x;
    const auto denominator = std::sqrt(real * real + imaginary * imaginary);
    if (denominator <= 0.0f) return filterTopDb;

    auto numerator = 1.0f;
    switch (type)
    {
        case rhino::forge::FilterType::highPass: numerator = x * x; break;
        case rhino::forge::FilterType::bandPass: numerator = x; break;
        case rhino::forge::FilterType::lowPass: break;
    }
    const auto gain = numerator / denominator;
    if (gain <= 1.0e-6f) return filterBottomDb;
    return juce::jlimit(filterBottomDb, filterTopDb, 20.0f * std::log10(gain));
}

// The response across the whole window. Sampled per pixel of width rather than
// at a fixed count, so a resonant spike is not stepped over on a wide panel and
// no time is spent oversampling a narrow one.
inline juce::Path filterResponsePath(juce::Rectangle<float> box, rhino::forge::FilterType type,
                                     float cutoff, float resonance)
{
    juce::Path path;
    const auto points = juce::jlimit(48, 512, juce::roundToInt(box.getWidth()));
    for (int i = 0; i <= points; ++i)
    {
        const auto x = box.getX() + box.getWidth() * static_cast<float>(i) / static_cast<float>(points);
        const auto y = filterDbToY(box, filterMagnitudeDb(type, cutoff, resonance, filterXToHz(box, x)));
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
inline void drawFilterResponse(juce::Graphics& g, juce::Rectangle<int> area, rhino::forge::FilterType type,
                               float cutoff, float resonance, juce::Colour colour, float alpha)
{
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(displayClip(area));

    const auto box = area.toFloat().reduced(0.0f, 6.0f);
    drawFilterGrid(g, box, alpha);

    const auto path = filterResponsePath(box, type, cutoff, resonance);
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
    const auto x = filterHzToX(box, cutoff);
    g.setColour(colour.withAlpha(0.45f * alpha));
    g.drawVerticalLine(juce::roundToInt(x), box.getY(), box.getBottom());
    g.setColour(colour.withAlpha(alpha));
    const auto readingFont = panelFont(Face::reading, 10.0f);
    g.setFont(readingFont);
    const auto reading = filterHzText(cutoff);
    const auto width = juce::jmax(46.0f, juce::GlyphArrangement::getStringWidth(readingFont, reading) + 8.0f);
    // Beside the marker, on whichever side of it there is room for.
    const auto right = x + 4.0f + width <= box.getRight();
    g.drawText(reading,
               juce::Rectangle<float>(right ? x + 4.0f : x - 4.0f - width, box.getY() + 2.0f, width, 12.0f)
                   .toNearestInt(),
               right ? juce::Justification::centredLeft : juce::Justification::centredRight);
}
}
