#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <charconv>
#include <cmath>
#include <optional>
#include <string>

namespace rhino
{
// How Rhino's settings files (.rnd, .rdk, .rdp) write a number: the shortest
// text that reads back as exactly the same float, so saving a preset and
// loading it changes nothing. JUCE's decimal formatting does not promise that.
inline juce::String exactFloatText(float value)
{
    std::array<char, 32> buffer {};
    const auto written = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    return juce::String(buffer.data(), static_cast<size_t>(written.ptr - buffer.data()));
}

// The other half: nothing for text that is not a whole, finite number.
inline std::optional<float> exactFloatFrom(const juce::String& text)
{
    const auto utf8 = text.trim().toStdString();
    auto value = 0.0f;
    const auto end = utf8.data() + utf8.size();
    const auto read = std::from_chars(utf8.data(), end, value);
    if (utf8.empty() || read.ec != std::errc() || read.ptr != end || !std::isfinite(value))
        return std::nullopt;
    return value;
}
}
