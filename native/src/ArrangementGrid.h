#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace rhino
{
enum class GridMode { off, adaptive, fixed };
enum class AdaptiveGridWidth { widest, wide, medium, narrow, narrowest };
enum class GridDivision { eightBars, fourBars, twoBars, bar, half, quarter, eighth, sixteenth, thirtySecond, sixtyFourth };

struct GridSettings
{
    GridMode mode = GridMode::adaptive;
    AdaptiveGridWidth adaptiveWidth = AdaptiveGridWidth::medium;
    GridDivision fixedDivision = GridDivision::sixteenth;
    bool triplet = false;
};

inline constexpr std::array<GridDivision, 10> gridDivisions {
    GridDivision::eightBars, GridDivision::fourBars, GridDivision::twoBars, GridDivision::bar,
    GridDivision::half, GridDivision::quarter, GridDivision::eighth, GridDivision::sixteenth,
    GridDivision::thirtySecond, GridDivision::sixtyFourth
};

inline constexpr double adaptiveGridTargetPixels(AdaptiveGridWidth width)
{
    switch (width)
    {
        case AdaptiveGridWidth::widest: return 72.0;
        case AdaptiveGridWidth::wide: return 48.0;
        case AdaptiveGridWidth::medium: return 32.0;
        case AdaptiveGridWidth::narrow: return 20.0;
        case AdaptiveGridWidth::narrowest: return 12.0;
    }
    return 32.0;
}

inline constexpr double gridDivisionBeats(GridDivision division, double beatsPerBar)
{
    switch (division)
    {
        case GridDivision::eightBars: return beatsPerBar * 8.0;
        case GridDivision::fourBars: return beatsPerBar * 4.0;
        case GridDivision::twoBars: return beatsPerBar * 2.0;
        case GridDivision::bar: return beatsPerBar;
        case GridDivision::half: return 2.0;
        case GridDivision::quarter: return 1.0;
        case GridDivision::eighth: return 0.5;
        case GridDivision::sixteenth: return 0.25;
        case GridDivision::thirtySecond: return 0.125;
        case GridDivision::sixtyFourth: return 0.0625;
    }
    return 0.25;
}

inline constexpr GridDivision narrowerGridDivision(GridDivision division)
{
    const auto index = static_cast<size_t>(division);
    return gridDivisions[std::min(index + 1, gridDivisions.size() - 1)];
}

inline constexpr GridDivision widerGridDivision(GridDivision division)
{
    const auto index = static_cast<size_t>(division);
    return gridDivisions[index == 0 ? 0 : index - 1];
}

inline GridDivision adaptiveGridDivision(double pixelsPerBeat, double beatsPerBar, AdaptiveGridWidth width)
{
    const auto target = adaptiveGridTargetPixels(width);
    for (auto it = gridDivisions.rbegin(); it != gridDivisions.rend(); ++it)
        if (gridDivisionBeats(*it, beatsPerBar) * pixelsPerBeat >= target)
            return *it;
    return GridDivision::eightBars;
}

inline double resolvedGridBeats(const GridSettings& settings, double pixelsPerBeat, double beatsPerBar)
{
    const auto division = settings.mode == GridMode::adaptive
        ? adaptiveGridDivision(pixelsPerBeat, beatsPerBar, settings.adaptiveWidth)
        : settings.fixedDivision;
    return gridDivisionBeats(division, beatsPerBar) * (settings.triplet ? 2.0 / 3.0 : 1.0);
}

inline const char* gridDivisionLabel(GridDivision division)
{
    switch (division)
    {
        case GridDivision::eightBars: return "8 Bars";
        case GridDivision::fourBars: return "4 Bars";
        case GridDivision::twoBars: return "2 Bars";
        case GridDivision::bar: return "1 Bar";
        case GridDivision::half: return "1/2";
        case GridDivision::quarter: return "1/4";
        case GridDivision::eighth: return "1/8";
        case GridDivision::sixteenth: return "1/16";
        case GridDivision::thirtySecond: return "1/32";
        case GridDivision::sixtyFourth: return "1/64";
    }
    return "1/16";
}

// How many bars one shaded band covers. The arrangement shades every other
// band so the bar grouping reads at a glance - three beats in 3/4, four in
// 4/4 - and the time signature is the only thing that knows how wide a bar is.
// A bar too narrow to read alone is grouped with its neighbours in powers of
// two, which is why a 1/4 project bands four bars where a 4/4 project at the
// same zoom bands one.
inline constexpr double barBandMinimumPixels = 120.0;

// How much of the lane a washed band takes away. Anything drawn over a lane
// has to know this: a grid line picked to read against the base shade
// disappears against the wash unless it is taken down by the same fraction.
// Read as a fraction rather than as counts of 255 because the lanes are light
// and the wash is black - it shades what is under it by a proportion of
// itself, where the old white one added a flat amount.
inline constexpr float barBandWash = 0.16f;

inline int barsPerBand(double pixelsPerBar)
{
    int bars = 1;
    // Bounded rather than trusting the width: a collapsed or not-yet-laid-out
    // lane reports no width at all, and this runs inside paint.
    while (bars < 256 && pixelsPerBar * bars < barBandMinimumPixels)
        bars *= 2;
    return bars;
}

// Whether the wash covers a given bar. Counted from bar zero and in whole
// bands, exactly as the painting counts, so the answer is a property of the
// music and does not flip as the arrangement is scrolled.
inline bool isWashedBar(double bar, int bandBars)
{
    if (bandBars <= 0) return false;
    const auto band = std::floor(bar / static_cast<double>(bandBars));
    return std::fmod(std::fmod(band, 2.0) + 2.0, 2.0) >= 1.0;
}

// How far apart two labelled bars have to be. The ruler prints a bar number at
// the top of the timeline and the time ruler under it prints that same bar's
// wall-clock time, which is a far wider string - so the two ask for different
// spacing and would otherwise thin out onto different bars.
inline constexpr double barNumberMinimumPixels = 44.0;
inline constexpr double timeLabelMinimumPixels = barNumberMinimumPixels * 2.0;

// The step, in bars, between one label and the next: the smallest power of two
// that leaves at least minimumPixels between them. Both rows step this way and
// both minimums are powers of two apart, so whichever row is labelling less
// often is labelling a subset of the other's bars - which is what keeps a time
// under every bar number it sits below.
inline double barLabelStep(double pixelsPerBar, double minimumPixels)
{
    double step = 1.0;
    while (step * pixelsPerBar < minimumPixels && step < 4096.0) step *= 2.0;
    return step;
}

// The marks between one labelled time and the next. At most this many to a
// label, and never closer together than this many pixels - so the row fills in
// as the view is zoomed in and thins back out as it is zoomed out, while every
// mark stays a binary subdivision of the gap between two labels and therefore
// lands on a musical division rather than wherever the arithmetic falls.
inline constexpr int timeTickDivisions = 8;
inline constexpr double timeTickMinimumPixels = 20.0;

// How many of those a label step is actually divided into at this zoom.
inline int timeTicksPerLabel(double pixelsPerBar, double labelStep)
{
    auto divisions = timeTickDivisions;
    while (divisions > 1 && labelStep * pixelsPerBar / divisions < timeTickMinimumPixels)
        divisions /= 2;
    return divisions;
}

// The first labelled bar at or before a given one, so the run of labels is a
// property of the music rather than of where the view happens to start.
inline double firstLabelledBar(double bar, double step)
{
    return std::floor((bar - 1.0) / step) * step + 1.0;
}

// Between the bar numbers. A bar wide enough to hold more than its own number
// is labelled at its counts as well - 1.2, 1.3, 1.4 in 4/4 - with short marks
// between the labels. A count is the note the time signature counts in, a
// quarter in 4/4 and an eighth in 6/8, so the ruler reads the way the
// signature is written rather than in the engine's quarter-note beats.
//
// Every span the ruler steps by, in counts, sits on one chain where each span
// divides the one before it: whole bars halve down to one bar, a bar halves
// while it stays a whole number of counts and then drops to a single count,
// and a count halves twice more. 4/4 runs 4, 2, 1, 1/2, 1/4; 6/8 runs 6, 3, 1,
// so its bar is cut at the dotted quarter before the eighth; 7/8 goes straight
// from the bar to its counts. A mark is therefore always a division of the
// labels either side of it.
inline double finerRulerSpan(double counts)
{
    const auto whole = std::round(counts);
    if (counts > 1.0 && std::abs(counts - whole) < 0.000001)
        return static_cast<long long>(whole) % 2 == 0 ? whole / 2.0 : 1.0;
    return counts * 0.5;
}

// A sixteenth in 4/4. Finer marks than that are a texture, not a reading.
inline constexpr double rulerFinestSpan = 0.25;
inline constexpr double rulerTickMinimumPixels = 10.0;

// Counts from one label to the next once the bar numbers are a bar apart: the
// finest span on the chain, and never finer than one count, that keeps
// minimumPixels between two labels. The whole bar when nothing finer fits.
inline double rulerLabelSpan(double pixelsPerCount, int countsPerBar, double minimumPixels)
{
    auto span = static_cast<double>(std::max(1, countsPerBar));
    while (span > 1.0 && finerRulerSpan(span) * pixelsPerCount >= minimumPixels)
        span = finerRulerSpan(span);
    return span;
}

// Counts from one mark to the next between labels a given span apart: as far
// down the chain as the marks stay rulerTickMinimumPixels apart. The label
// span itself when not even one mark fits between two labels.
inline double rulerTickSpan(double pixelsPerCount, double labelSpan)
{
    auto span = labelSpan;
    while (span > rulerFinestSpan && finerRulerSpan(span) * pixelsPerCount >= rulerTickMinimumPixels)
        span = finerRulerSpan(span);
    return span;
}

inline bool isGridLine(double beat, double interval)
{
    if (interval <= 0.0) return false;
    return std::abs(beat / interval - std::round(beat / interval)) < 0.00001;
}
}
