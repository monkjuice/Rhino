#include "TunerEngine.h"
#include <algorithm>
#include <cmath>

namespace rhino
{
namespace
{
// Within this of the reading, a new analysis is the same note wavering and is
// averaged in; past it the reading jumps, because a new note was played.
constexpr float sameNoteCents = 40.0f;
// How much of each new analysis the reading takes: enough to settle in about
// a tenth of a second, little enough that the needle does not shiver.
constexpr float smoothing = 0.35f;
// How long the last pitch stays up after the input stops having one, so a
// plucked string's decay does not blink the display off between analyses.
constexpr double holdSeconds = 0.3;
}

void TunerEngine::prepare(double sampleRate, int maximumBlockSize)
{
    const auto rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    tracker.prepare(rate, lowestHz, highestHz);
    // A tuner is read to the cent, high strings included.
    tracker.setMultiplePeriodRefinement(true);
    analysisRate = rate / tracker.hopSize();
    holdAnalyses = std::max(1, static_cast<int>(holdSeconds * analysisRate));
    mono.assign(static_cast<size_t>(std::max(1, maximumBlockSize)), 0.0f);
    reset();
}

void TunerEngine::reset()
{
    tracker.reset();
    seen = 0;
    smoothed = 0.0f;
    unvoicedRun = 0;
    shownFrequency.store(0.0f);
    shownClarity.store(0.0f);
    shownVoiced.store(false);
}

void TunerEngine::process(const float* const* channels, int numChannels, int count)
{
    if (numChannels <= 0 || mono.empty())
        return;
    for (int done = 0; done < count;)
    {
        // Never more than a hop at a time, so no analysis lands unpublished
        // behind another in the same chunk.
        const auto chunk = std::min({ count - done, static_cast<int>(mono.size()), tracker.hopSize() });
        for (int i = 0; i < chunk; ++i)
        {
            auto sum = 0.0f;
            for (int c = 0; c < numChannels; ++c)
                sum += channels[c][done + i];
            mono[static_cast<size_t>(i)] = sum / static_cast<float>(numChannels);
        }
        tracker.process(mono.data(), chunk);
        if (tracker.analysisCount() != seen)
        {
            seen = tracker.analysisCount();
            publish(tracker.latest());
        }
        done += chunk;
    }
}

void TunerEngine::publish(const PitchTracker::Estimate& estimate)
{
    const auto inBand = estimate.frequency >= lowestHz && estimate.frequency <= highestHz;
    auto heard = 0.0f;
    if (estimate.voiced && inBand)
    {
        const auto cents = smoothed > 0.0f ? 1200.0f * std::log2(estimate.frequency / smoothed) : 1.0e6f;
        // Averaged in the log of the frequency, so a waver either side of the
        // note pulls equally hard.
        smoothed = std::abs(cents) < sameNoteCents ? smoothed * std::pow(estimate.frequency / smoothed, smoothing)
                                                   : estimate.frequency;
        unvoicedRun = 0;
        heard = smoothed;
    }
    else if (++unvoicedRun > holdAnalyses)
    {
        smoothed = 0.0f;
    }
    shownFrequency.store(smoothed, std::memory_order_relaxed);
    shownClarity.store(estimate.clarity, std::memory_order_relaxed);
    shownVoiced.store(smoothed > 0.0f, std::memory_order_relaxed);

    const auto at = written.load(std::memory_order_relaxed);
    ring[static_cast<size_t>(at % historyLength)].store(heard, std::memory_order_relaxed);
    written.store(at + 1, std::memory_order_release);
}

TunerEngine::Reading TunerEngine::reading() const
{
    return { shownFrequency.load(std::memory_order_relaxed), shownClarity.load(std::memory_order_relaxed),
             shownVoiced.load(std::memory_order_relaxed) };
}

int TunerEngine::history(float* frequencies, int count) const
{
    const auto total = written.load(std::memory_order_acquire);
    const auto available = std::min({ count, total, historyLength });
    for (int i = 0; i < available; ++i)
        frequencies[i] = ring[static_cast<size_t>((total - available + i) % historyLength)].load(std::memory_order_relaxed);
    return available;
}

float TunerEngine::noteFor(float frequency, float referenceHz)
{
    return frequency > 0.0f && referenceHz > 0.0f ? 69.0f + 12.0f * std::log2(frequency / referenceHz) : 0.0f;
}
}
