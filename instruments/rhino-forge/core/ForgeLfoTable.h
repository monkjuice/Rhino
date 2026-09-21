#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace rhino::forge
{
inline constexpr int maxLfoPoints = 33;

struct LfoPoint
{
    float x = 0.0f;
    float y = 0.0f;
};

// Fixed storage makes a table cheap to copy into the audio block's Patch.
// Endpoints stay at 0 and 1; interior points may move between their neighbours.
struct LfoTable
{
    std::array<LfoPoint, maxLfoPoints> points {};
    int count = 0;
    int columns = 8;
    int rows = 8;
    bool custom = false;

    float sample(float phase) const noexcept
    {
        if (count < 2) return 0.0f;
        const auto x = std::clamp(phase, 0.0f, 1.0f);
        int lower = 0, upper = count - 1;
        while (upper - lower > 1)
        {
            const auto middle = (lower + upper) / 2;
            if (x > points[static_cast<size_t>(middle)].x) lower = middle;
            else upper = middle;
        }
        const auto& left = points[static_cast<size_t>(lower)];
        const auto& right = points[static_cast<size_t>(upper)];
        const auto width = right.x - left.x;
        const auto mix = width > 0.0f ? (x - left.x) / width : 0.0f;
        return std::clamp(left.y + (right.y - left.y) * mix, -1.0f, 1.0f);
    }

    bool valid() const noexcept
    {
        if (count < 2 || count > maxLfoPoints || columns < 2 || columns > 32 || rows < 2 || rows > 32)
            return false;
        if (points[0].x != 0.0f || points[static_cast<size_t>(count - 1)].x != 1.0f) return false;
        for (int i = 0; i < count; ++i)
        {
            const auto& point = points[static_cast<size_t>(i)];
            if (!std::isfinite(point.x) || !std::isfinite(point.y) || point.y < -1.0f || point.y > 1.0f)
                return false;
            if (i > 0 && point.x <= points[static_cast<size_t>(i - 1)].x) return false;
        }
        return true;
    }
};
}
