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
inline juce::String formatClock(double seconds, bool withMilliseconds)
{
    if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
    const auto totalMs = static_cast<juce::int64>(seconds * 1000.0 + 0.5);
    const auto milliseconds = static_cast<int>(totalMs % 1000);
    const auto totalSeconds = totalMs / 1000;
    auto text = juce::String(static_cast<int>(totalSeconds / 60)).paddedLeft('0', 2)
              + ":" + juce::String(static_cast<int>(totalSeconds % 60)).paddedLeft('0', 2);
    if (withMilliseconds) text += ":" + juce::String(milliseconds).paddedLeft('0', 3);
    return text;
}
}
