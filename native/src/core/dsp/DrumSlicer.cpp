#include "DrumSlicer.h"
#include <algorithm>
#include <cmath>

namespace rhino
{
namespace
{
constexpr double hopSeconds = 0.005;
constexpr double shortestSliceSeconds = 0.06;
constexpr double preRollSeconds = 0.002;

float sampleAt(const DrumSample& sample, int index)
{
    const auto left = sample.left[static_cast<size_t>(index)];
    return sample.stereo() ? 0.5f * (left + sample.right[static_cast<size_t>(index)]) : left;
}

std::vector<int> transients(const DrumSample& sample, int from, int to, float sensitivity)
{
    std::vector<int> cuts { from };
    const auto hop = std::max(16, static_cast<int>(std::lround(sample.sampleRate * hopSeconds)));
    const auto window = 2 * hop;
    if (to - from < 2 * window)
        return cuts;

    std::vector<float> levels;
    for (int at = from; at + window <= to; at += hop)
    {
        auto energy = 0.0;
        for (int i = at; i < at + window; ++i)
        {
            const auto value = static_cast<double>(sampleAt(sample, i));
            energy += value * value;
        }
        levels.push_back(static_cast<float>(10.0 * std::log10(energy / window + 1.0e-12)));
    }
    const auto count = static_cast<int>(levels.size());
    const auto loudest = *std::max_element(levels.begin(), levels.end());
    const auto threshold = juce::jmap(sensitivity, 16.0f, 4.0f);
    const auto floor = loudest - juce::jmap(sensitivity, 30.0f, 60.0f);
    const auto gap = std::max(1, static_cast<int>(std::lround(shortestSliceSeconds * sample.sampleRate / hop)));

    // How much louder each window is than the one two before it, which ends
    // where this one starts and so has not yet heard the hit.
    std::vector<float> rises(static_cast<size_t>(count), 0.0f);
    for (int frame = 2; frame < count; ++frame)
        rises[static_cast<size_t>(frame)] = levels[static_cast<size_t>(frame)] - levels[static_cast<size_t>(frame - 2)];

    auto lastHit = 0;
    for (int frame = 2; frame < count && static_cast<int>(cuts.size()) < DrumSlicer::mostSlices; ++frame)
    {
        const auto rise = rises[static_cast<size_t>(frame)];
        const auto next = frame + 1 < count ? rises[static_cast<size_t>(frame + 1)] : -1.0e9f;
        if (rise < threshold || levels[static_cast<size_t>(frame)] < floor || rise < rises[static_cast<size_t>(frame - 1)]
            || rise <= next || frame - lastHit < gap)
            continue;
        lastHit = frame;
        // The hit is somewhere in the window. It starts at its first sample
        // to reach a third of its peak, and the cut falls just before it.
        const auto searchFrom = from + (frame - 1) * hop;
        const auto searchTo = std::min(to, from + frame * hop + window);
        auto peak = 0.0f;
        for (int i = searchFrom; i < searchTo; ++i)
            peak = std::max(peak, std::abs(sampleAt(sample, i)));
        auto onset = searchFrom;
        for (int i = searchFrom; i < searchTo; ++i)
            if (std::abs(sampleAt(sample, i)) >= peak / 3.0f)
            {
                onset = i;
                break;
            }
        const auto cut = std::max(from, onset - static_cast<int>(std::lround(preRollSeconds * sample.sampleRate)));
        // The part's own start is the first slice, and a hit at it is that
        // slice rather than a sliver before it.
        if (cut - cuts.back() >= static_cast<int>(shortestSliceSeconds * sample.sampleRate))
            cuts.push_back(cut);
    }
    return cuts;
}
}

std::vector<float> DrumSlicer::slices(const DrumSample& sample, const DrumRackEngine::Playback& wanted)
{
    const auto playback = wanted.clamped();
    const auto length = sample.length();
    if (length <= 0)
        return { playback.start };
    if (playback.sliceBy == DrumRackEngine::SliceBy::divisions)
    {
        std::vector<float> starts;
        const auto parts = std::clamp(playback.divisions, 1, mostSlices);
        for (int part = 0; part < parts; ++part)
            starts.push_back(playback.start + (playback.end - playback.start) * static_cast<float>(part) / parts);
        return starts;
    }
    const auto from = std::clamp(static_cast<int>(std::floor(playback.start * length)), 0, length - 1);
    const auto to = std::clamp(static_cast<int>(std::ceil(playback.end * length)), from + 1, length);
    std::vector<float> starts;
    for (const auto cut : transients(sample, from, to, playback.sensitivity))
        starts.push_back(cut == from ? playback.start : static_cast<float>(cut) / static_cast<float>(length));
    return starts;
}
}
