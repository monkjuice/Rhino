#pragma once
#include <juce_core/juce_core.h>
#include <cmath>

namespace rhino
{
// Wall-clock readings, as mm:ss or mm:ss:ms. Minutes are padded to two digits
// rather than widened past an hour: an arrangement that long would push the
// load meters out of the transport readout, and the bar number is the reading
// that matters at that length anyway.
//
// It has a header to itself because two places print a time - the readout in
// the control bar and the ruler at the foot of the timeline - and the two are
// read against each other. Spelling a time differently in either is the kind
// of difference nobody notices until it matters.
//
// A time row zoomed in past whole milliseconds reads in fractions of one, so a
// reading can carry up to three decimals of a millisecond: 00:01:000.488.
inline juce::String formatClock(double seconds, bool withMilliseconds, int millisecondDecimals = 0)
{
    if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
    const auto decimals = withMilliseconds ? juce::jlimit(0, 3, millisecondDecimals) : 0;
    juce::int64 unitsPerMs = 1;
    for (int i = 0; i < decimals; ++i) unitsPerMs *= 10;
    const auto totalUnits = static_cast<juce::int64>(seconds * 1000.0 * static_cast<double>(unitsPerMs) + 0.5);
    const auto totalMs = totalUnits / unitsPerMs;
    const auto milliseconds = static_cast<int>(totalMs % 1000);
    const auto totalSeconds = totalMs / 1000;
    auto text = juce::String(static_cast<int>(totalSeconds / 60)).paddedLeft('0', 2)
              + ":" + juce::String(static_cast<int>(totalSeconds % 60)).paddedLeft('0', 2);
    if (withMilliseconds) text += ":" + juce::String(milliseconds).paddedLeft('0', 3);
    if (decimals > 0) text += "." + juce::String(static_cast<int>(totalUnits % unitsPerMs)).paddedLeft('0', decimals);
    return text;
}

// How many decimals of a millisecond a run of readings this far apart needs:
// none while they are a tenth of a second or more apart, then one more for every
// tenfold closer, up to three, so no reading is out by more than a hundredth of
// the gap to the next.
inline int clockDecimalsForStep(double stepSeconds)
{
    auto decimals = 0;
    for (auto stepMs = stepSeconds * 1000.0; decimals < 3 && stepMs < 100.0; stepMs *= 10.0)
        ++decimals;
    return decimals;
}
}
