#include "SaturatorEngine.h"
#include <algorithm>
#include <cmath>

namespace rhino
{
namespace
{
double gainOf(double decibels)
{
    return std::pow(10.0, decibels / 20.0);
}

// Width runs from a narrow bell (Q 4) to a broad one (Q 0.3), evenly in
// octaves of Q.
double colourQ(float width)
{
    return 4.0 * std::pow(0.3 / 4.0, std::clamp(static_cast<double>(width), 0.0, 1.0));
}

double smoothingFor(double seconds, double sampleRate)
{
    return 1.0 - std::exp(-1.0 / (seconds * sampleRate));
}

saturator::WaveshaperShape clampedShape(const saturator::WaveshaperShape& shape)
{
    auto clamped = shape;
    for (auto* value : { &clamped.drive, &clamped.curve, &clamped.depth, &clamped.linear, &clamped.damp, &clamped.period })
        *value = std::clamp(*value, 0.0, 1.0);
    return clamped;
}
}

void SaturatorEngine::prepare(double sampleRate, int maximumBlockSize)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    maximumBlockSize = std::max(1, maximumBlockSize);
    oversampler.prepare(oversampling, maximumChannels, maximumBlockSize);
    innerSmoothing = smoothingFor(0.02, rate * oversampling);
    outerSmoothing = smoothingFor(0.02, rate);
    wet.assign(static_cast<size_t>(maximumBlockSize), 0.0f);
    for (auto& channel : channels)
        channel.dryLine.assign(static_cast<size_t>(std::max(1, oversampler.latencySamples())), 0.0f);
    updateColour();
    reset();
}

void SaturatorEngine::reset()
{
    oversampler.reset();
    for (auto& channel : channels)
    {
        for (auto* filter : { &channel.lowIn, &channel.bellIn, &channel.lowOut, &channel.bellOut })
            filter->reset();
        std::fill(channel.dryLine.begin(), channel.dryLine.end(), 0.0f);
        channel.dryAt = 0;
        // A cleared device starts at its settings rather than gliding to them.
        channel.drive = gainOf(settings.driveDb);
        channel.output = gainOf(settings.outputDb);
        channel.mix = std::clamp(static_cast<double>(settings.dryWet), 0.0, 1.0);
    }
}

void SaturatorEngine::setSettings(const Settings& next)
{
    const auto colourMoved = next.colourLow != settings.colourLow || next.colourHigh != settings.colourHigh
        || next.colourFrequency != settings.colourFrequency || next.colourWidth != settings.colourWidth;
    settings = next;
    if (colourMoved)
        updateColour();
}

void SaturatorEngine::updateColour()
{
    const auto innerRate = rate * oversampling;
    const auto lowGain = colourRangeDb * std::clamp(static_cast<double>(settings.colourLow), -1.0, 1.0);
    const auto highGain = colourRangeDb * std::clamp(static_cast<double>(settings.colourHigh), -1.0, 1.0);
    const auto frequency = std::clamp(static_cast<double>(settings.colourFrequency), 20.0, rate * 0.45);
    const auto low = eqdetail::lowShelf(2.0 * eqdetail::pi * colourLowCornerHz / innerRate, 0.70710678, lowGain);
    const auto bell = eqdetail::bell(2.0 * eqdetail::pi * frequency / innerRate, colourQ(settings.colourWidth), highGain);
    for (auto& channel : channels)
    {
        channel.lowIn.coefficients = low;
        channel.bellIn.coefficients = bell;
        channel.lowOut.coefficients = inverseOf(low);
        channel.bellOut.coefficients = inverseOf(bell);
    }
}

void SaturatorEngine::process(float* const* audio, int numChannels, int count)
{
    const auto curve = settings.curve;
    const auto threshold = gainOf(std::clamp(static_cast<double>(settings.bassThresholdDb), -50.0, 0.0));
    const auto waves = clampedShape(settings.waves);
    const auto post = settings.postClip;
    const auto colour = settings.colour;
    const auto driveTarget = gainOf(std::clamp(static_cast<double>(settings.driveDb), -36.0, 36.0));
    const auto outputTarget = gainOf(std::clamp(static_cast<double>(settings.outputDb), -36.0, 12.0));
    const auto mixTarget = std::clamp(static_cast<double>(settings.dryWet), 0.0, 1.0);
    const auto innerCount = count * oversampling;

    for (int c = 0; c < std::min(numChannels, maximumChannels); ++c)
    {
        auto& channel = channels[static_cast<size_t>(c)];
        auto* samples = audio[c];
        auto* inner = oversampler.up(c, samples, count);
        for (int i = 0; i < innerCount; ++i)
        {
            channel.drive += (driveTarget - channel.drive) * innerSmoothing;
            channel.output += (outputTarget - channel.output) * innerSmoothing;
            auto x = static_cast<double>(inner[i]) * channel.drive;
            if (colour)
                x = channel.bellIn.process(channel.lowIn.process(x));
            auto y = saturator::shape(curve, x, threshold, waves);
            if (colour)
                y = channel.bellOut.process(channel.lowOut.process(y));
            inner[i] = static_cast<float>(saturator::postClip(post, y) * channel.output);
        }
        oversampler.down(c, wet.data(), count);

        auto& line = channel.dryLine;
        const auto length = static_cast<int>(line.size());
        for (int i = 0; i < count; ++i)
        {
            // The dry signal waits as long as the wet one took.
            const auto dry = line[static_cast<size_t>(channel.dryAt)];
            line[static_cast<size_t>(channel.dryAt)] = samples[i];
            channel.dryAt = (channel.dryAt + 1) % length;
            channel.mix += (mixTarget - channel.mix) * outerSmoothing;
            // Weighted rather than interpolated, so all wet is exactly the wet
            // signal and all dry exactly the dry one.
            samples[i] = static_cast<float>(dry * (1.0 - channel.mix) + wet[static_cast<size_t>(i)] * channel.mix);
        }
    }
}

void SaturatorEngine::transferCurve(const Settings& settings, float* points, int count)
{
    const auto drive = gainOf(std::clamp(static_cast<double>(settings.driveDb), -36.0, 36.0));
    const auto output = gainOf(std::clamp(static_cast<double>(settings.outputDb), -36.0, 12.0));
    const auto threshold = gainOf(std::clamp(static_cast<double>(settings.bassThresholdDb), -50.0, 0.0));
    const auto waves = clampedShape(settings.waves);
    for (int i = 0; i < count; ++i)
    {
        const auto x = count > 1 ? -1.0 + 2.0 * i / (count - 1) : 0.0;
        const auto y = saturator::shape(settings.curve, x * drive, threshold, waves);
        points[i] = static_cast<float>(saturator::postClip(settings.postClip, y) * output);
    }
}
}
