#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace rhino
{
enum class GridMode { off, adaptive, fixed };
enum class AdaptiveGridWidth { widest, wide, medium, narrow, narrowest };
// Past a sixty-fourth the divisions are named by their note: they matter only
// to a view zoomed in on single samples, and they keep halving down to 1/16384,
// the deepest the arrangement zooms.
enum class GridDivision { eightBars, fourBars, twoBars, bar, half, quarter, eighth, sixteenth, thirtySecond, sixtyFourth,
                          note128, note256, note512, note1024, note2048, note4096, note8192, note16384 };

struct GridSettings
{
    GridMode mode = GridMode::adaptive;
    AdaptiveGridWidth adaptiveWidth = AdaptiveGridWidth::medium;
    GridDivision fixedDivision = GridDivision::sixteenth;
    bool triplet = false;
};

inline constexpr std::array<GridDivision, 18> gridDivisions {
    GridDivision::eightBars, GridDivision::fourBars, GridDivision::twoBars, GridDivision::bar,
    GridDivision::half, GridDivision::quarter, GridDivision::eighth, GridDivision::sixteenth,
    GridDivision::thirtySecond, GridDivision::sixtyFourth,
    GridDivision::note128, GridDivision::note256, GridDivision::note512, GridDivision::note1024,
    GridDivision::note2048, GridDivision::note4096, GridDivision::note8192, GridDivision::note16384
};

// The divisions the Fixed menu offers. The finer ones are for a view zoomed in
// on single samples, where the adaptive grid reaches them by itself, and Ctrl+1
// still narrows a fixed grid into them.
inline constexpr size_t fixedGridDivisionCount = static_cast<size_t>(GridDivision::sixtyFourth) + 1;

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
        case GridDivision::note128: return 4.0 / 128.0;
        case GridDivision::note256: return 4.0 / 256.0;
        case GridDivision::note512: return 4.0 / 512.0;
        case GridDivision::note1024: return 4.0 / 1024.0;
        case GridDivision::note2048: return 4.0 / 2048.0;
        case GridDivision::note4096: return 4.0 / 4096.0;
        case GridDivision::note8192: return 4.0 / 8192.0;
        case GridDivision::note16384: return 4.0 / 16384.0;
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

// The finest division whose lines are at least the target over root two apart,
// which between divisions an octave apart is the one nearest the target. Asking
// for the finest that cleared the whole target left the lines between one and
// two targets apart, so two minutes across a 2,000 px lane came out in two-bar
// cells where Live shows one bar. Live draws denser still - one-bar cells 20 px
// apart, 1/8192 lines 14 px apart - but at that density the same two minutes
// would read in half bars.
inline GridDivision adaptiveGridDivision(double pixelsPerBeat, double beatsPerBar, AdaptiveGridWidth width)
{
    const auto target = adaptiveGridTargetPixels(width) / std::sqrt(2.0);
    for (auto it = gridDivisions.rbegin(); it != gridDivisions.rend(); ++it)
        if (gridDivisionBeats(*it, beatsPerBar) * pixelsPerBeat >= target)
            return *it;
    return GridDivision::eightBars;
}

// The deepest the arrangement zooms: the finest division drawn at the medium
// grid's own spacing. Held per beat rather than per second, so the deepest
// view shows 1/16384 at every tempo.
inline constexpr double deepestZoomPixelsPerBeat =
    adaptiveGridTargetPixels(AdaptiveGridWidth::medium) / gridDivisionBeats(gridDivisions.back(), 4.0);

// The step the grid's lines are drawn at. A fixed division can be finer than
// the zoom can show - 1/64 across two minutes is nearly four thousand lines -
// and the painter stops at a cap, so drawing every one left the right of the
// view bare. Lines too close together step out to the counts and then to the
// bars, which every signature's bar lines fall on, and bars too close together
// are drawn every other one, and so on, which a fixed one-bar grid across a
// long song needs. Snapping keeps the division.
inline constexpr double gridLineMinimumPixels = 4.0;

inline double drawnGridBeats(double gridBeats, double pixelsPerBeat, double countBeats, double barBeats)
{
    auto drawn = gridBeats;
    for (const auto coarser : {countBeats, barBeats})
        if (drawn * pixelsPerBeat < gridLineMinimumPixels && coarser > drawn)
            drawn = coarser;
    while (drawn >= barBeats && drawn * pixelsPerBeat < gridLineMinimumPixels && drawn < barBeats * 4096.0)
        drawn *= 2.0;
    return drawn;
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
        case GridDivision::note128: return "1/128";
        case GridDivision::note256: return "1/256";
        case GridDivision::note512: return "1/512";
        case GridDivision::note1024: return "1/1024";
        case GridDivision::note2048: return "1/2048";
        case GridDivision::note4096: return "1/4096";
        case GridDivision::note8192: return "1/8192";
        case GridDivision::note16384: return "1/16384";
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

// How far apart two labels have to be: a bar number at the top of the
// timeline, and a wall-clock reading in the row at its foot, which is a far
// wider string.
inline constexpr double barNumberMinimumPixels = 44.0;
inline constexpr double timeLabelMinimumPixels = barNumberMinimumPixels * 2.0;

// The step, in bars, between one bar number and the next: the smallest power
// of two that leaves at least minimumPixels between them.
inline double barLabelStep(double pixelsPerBar, double minimumPixels)
{
    double step = 1.0;
    while (step * pixelsPerBar < minimumPixels && step < 4096.0) step *= 2.0;
    return step;
}

// The marks between one reading of the time row and the next. At most this
// many to a reading, and never closer together than this many pixels, so the
// row fills in as the view is zoomed in and thins back out as it is zoomed out.
inline constexpr int timeTickDivisions = 8;
inline constexpr double timeTickMinimumPixels = 20.0;

// The time row reads round times, as Live's does: 1, 2 and 5 of each power of
// ten, and 15 and 30 of the seconds and minutes. Its readings stepped by bars
// until 2026-10-23, so a time always stood under a bar number, but that read
// 00:08:000, 00:16:000 at 120 where Live reads 0:10, 0:20, jumped two seconds
// at a time across a view zoomed in to 1/32, and printed nothing at all once a
// bar was wider than the view. In microseconds, so a reading is a whole number
// and prints exactly; the three below a millisecond only ever mark one.
inline constexpr std::array<long long, 25> roundTimeSteps {
    100, 200, 500,
    1'000, 2'000, 5'000, 10'000, 20'000, 50'000, 100'000, 200'000, 500'000,
    1'000'000, 2'000'000, 5'000'000, 10'000'000, 15'000'000, 30'000'000,
    60'000'000, 120'000'000, 300'000'000, 600'000'000, 900'000'000, 1'800'000'000, 3'600'000'000
};
inline constexpr long long roundTimeFinestReading = 1'000;

// The finest round step, a millisecond or more, that keeps two readings
// timeLabelMinimumPixels apart.
inline long long roundTimeLabelStep(double pixelsPerSecond)
{
    for (const auto step : roundTimeSteps)
        if (step >= roundTimeFinestReading && static_cast<double>(step) * 1.0e-6 * pixelsPerSecond >= timeLabelMinimumPixels)
            return step;
    return roundTimeSteps.back();
}

// The marks between two round readings: the finest round step that divides the
// gap into at most timeTickDivisions and keeps timeTickMinimumPixels between
// marks, so ten seconds are marked in twos, fifteen in fives and a millisecond
// in fifths. The gap itself, unmarked, when nothing finer fits.
inline long long roundTimeTickStep(double pixelsPerSecond, long long labelStep)
{
    for (const auto step : roundTimeSteps)
        if (step < labelStep && labelStep % step == 0 && labelStep / step <= timeTickDivisions
            && static_cast<double>(step) * 1.0e-6 * pixelsPerSecond >= timeTickMinimumPixels)
            return step;
    return labelStep;
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

// Zoomed in far enough, round times run out: Live's time row prints whole
// milliseconds and nothing finer, so at its deepest it shows one reading across
// the whole view. The time row then reads the bar ruler's own chain instead, on
// past the counts where the bar ruler stops, so each reading falls on a line
// the grid cuts the bar into. That takes over once it reads at least twice as
// often as whole milliseconds would; any sooner it would trade round readings
// for ones barely any closer together.
inline bool timeRowReadsGrid(double gridReadingSeconds)
{
    return gridReadingSeconds * 2.0 <= static_cast<double>(roundTimeFinestReading) * 1.0e-6;
}

// Counts from one of those readings to the next: the finest span on the chain
// that keeps timeLabelMinimumPixels between two of them. The whole bar when
// nothing finer fits. Bounded, because this runs inside paint and a lane that
// has not been laid out yet reports any width at all.
inline double timeLabelSpan(double pixelsPerCount, int countsPerBar)
{
    auto span = static_cast<double>(std::max(1, countsPerBar));
    while (span > 1.0e-6 && finerRulerSpan(span) * pixelsPerCount >= timeLabelMinimumPixels)
        span = finerRulerSpan(span);
    return span;
}

// The marks between them, down the same chain: at most timeTickDivisions to a
// reading and never closer than timeTickMinimumPixels.
inline double timeTickSpan(double pixelsPerCount, double labelSpan)
{
    auto span = labelSpan;
    while (labelSpan / finerRulerSpan(span) <= timeTickDivisions + 1.0e-9
           && finerRulerSpan(span) * pixelsPerCount >= timeTickMinimumPixels)
        span = finerRulerSpan(span);
    return span;
}

inline bool isGridLine(double beat, double interval)
{
    if (interval <= 0.0) return false;
    return std::abs(beat / interval - std::round(beat / interval)) < 0.00001;
}

// Where a point in the music falls, written as the transport readout and the
// status line write it: the bar, the count within the bar and the sixteenth
// within the count, each from one. A count is the note the signature counts in,
// as on the ruler: 6/8 reads six counts to the bar, and an eighth holds two
// sixteenths. Counting the engine's quarter-note beats instead read 6/8 as three
// beats a bar and gave 7/8 a fourth beat half as long as the others.
struct MusicalPosition
{
    int bar = 1, count = 1, sixteenth = 1;
};

inline MusicalPosition musicalPosition(double beats, int numerator, int denominator)
{
    // Tempo-map round trips land a hair short of a boundary; this is far below
    // anything a reading can show.
    constexpr double epsilon = 0.000001;
    const auto counts = std::max(1, numerator);
    const auto countLength = 4.0 / std::max(1, denominator);
    const auto barLength = countLength * counts;
    const auto bar = std::floor(std::max(0.0, beats) / barLength + epsilon);
    const auto intoBar = std::max(0.0, beats - bar * barLength);
    const auto count = std::clamp(static_cast<int>(std::floor(intoBar / countLength + epsilon)), 0, counts - 1);
    const auto intoCount = std::max(0.0, intoBar - count * countLength);
    const auto sixteenthsPerCount = std::max(1, static_cast<int>(std::lround(countLength / 0.25)));
    const auto sixteenth = std::clamp(static_cast<int>(std::floor(intoCount / 0.25 + epsilon)), 0, sixteenthsPerCount - 1);
    return {static_cast<int>(bar) + 1, count + 1, sixteenth + 1};
}
}
