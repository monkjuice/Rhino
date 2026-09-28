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

// How much lighter a washed band is than the lane under it. Anything drawn
// over a lane has to know this: a grid line picked to read against the base
// shade disappears against the wash unless it is lifted by the same amount.
inline constexpr float barBandWash = 10.0f / 255.0f;

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

inline bool isGridLine(double beat, double interval)
{
    if (interval <= 0.0) return false;
    return std::abs(beat / interval - std::round(beat / interval)) < 0.00001;
}
}
