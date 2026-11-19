#include "MultibandDynamicsDevice.h"

namespace rhino
{
namespace
{
juce::String ratioText(float ratio)
{
    return ratio >= MultibandDynamicsEngine::infiniteRatio ? juce::String("inf:1")
                                                           : juce::String(ratio, ratio < 10.0f ? 2 : 1) + ":1";
}

// Live's three figures: 5.00 ms, 50.0 ms, 500 ms. A whole number is rounded
// here, because juce::String given no decimal places prints all of them.
juce::String timeText(float milliseconds)
{
    if (milliseconds >= 1000.0f)
        return juce::String(milliseconds / 1000.0f, 2) + " s";
    if (milliseconds >= 100.0f)
        return juce::String(juce::roundToInt(milliseconds)) + " ms";
    return juce::String(milliseconds, milliseconds < 10.0f ? 2 : 1) + " ms";
}
}

MultibandDynamicsDevice::BandControls MultibandDynamicsDevice::bandControls(const juce::String& band, float attackMs,
                                                                             float releaseMs)
{
    const auto id = band.toLowerCase();
    const auto ratio = [this, &id, &band] (const char* which, const char* name)
    {
        return param(id + which, band + name).range(MultibandDynamicsEngine::minimumRatio, MultibandDynamicsEngine::infiniteRatio)
            .defaultValue(1.0f).skewAround(2.0f).format(ratioText).section(band, "Bands");
    };
    // Braced, so the controls are declared in the order written.
    return {
        param(id + "Active", band + " Active").toggle(true).section(band, "Bands"),
        param(id + "Solo", band + " Solo").toggle(false).section(band, "Bands"),
        param(id + "Input", band + " Input").range(-12.0f, 12.0f).defaultValue(0.0f).unit(ParamUnit::decibels)
            .section(band, "Bands"),
        param(id + "Output", band + " Output").range(-12.0f, 12.0f).defaultValue(0.0f).unit(ParamUnit::decibels)
            .section(band, "Bands"),
        param(id + "BelowThreshold", band + " Below Threshold").range(-80.0f, 0.0f).defaultValue(-60.0f)
            .unit(ParamUnit::decibels).section(band, "Bands"),
        ratio("BelowRatio", " Below Ratio"),
        param(id + "AboveThreshold", band + " Above Threshold").range(-80.0f, 0.0f).defaultValue(-20.0f)
            .unit(ParamUnit::decibels).section(band, "Bands"),
        ratio("AboveRatio", " Above Ratio"),
        param(id + "Attack", band + " Attack").range(0.1f, 1000.0f).defaultValue(attackMs).skewAround(10.0f)
            .unit(ParamUnit::milliseconds).format(timeText).section(band, "Bands"),
        param(id + "Release", band + " Release").range(1.0f, 3000.0f).defaultValue(releaseMs).skewAround(200.0f)
            .unit(ParamUnit::milliseconds).format(timeText).section(band, "Bands"),
    };
}

MultibandDynamicsEngine::Settings MultibandDynamicsDevice::currentSettings() const
{
    MultibandDynamicsEngine::Settings settings;
    settings.lowBand = lowBand.on();
    settings.highBand = highBand.on();
    settings.lowCrossoverHz = lowCrossover.value();
    settings.highCrossoverHz = highCrossover.value();
    settings.softKnee = softKnee.on();
    settings.rms = rms.on();
    settings.outputDb = output.value();
    settings.time = time.value();
    settings.amount = amount.value();
    for (size_t b = 0; b < bands.size(); ++b)
    {
        const auto& controls = bands[b];
        auto& band = settings.bands[b];
        band.active = controls.active.on();
        band.solo = controls.solo.on();
        band.inputDb = controls.input.value();
        band.outputDb = controls.output.value();
        band.belowThresholdDb = controls.belowThreshold.value();
        band.belowRatio = controls.belowRatio.value();
        band.aboveThresholdDb = controls.aboveThreshold.value();
        band.aboveRatio = controls.aboveRatio.value();
        band.attackMs = controls.attack.value();
        band.releaseMs = controls.release.value();
    }
    return settings;
}

void MultibandDynamicsDevice::prepare(double newRate, int)
{
    engine.prepare(newRate);
}

void MultibandDynamicsDevice::clear()
{
    // The settings first, so a cleared device starts where its controls are.
    engine.setSettings(currentSettings());
    engine.reset();
}

void MultibandDynamicsDevice::process(RenderBlock& block)
{
    engine.setSettings(currentSettings());
    engine.process(block.channels, block.numChannels, block.numSamples);
    const auto raise = [] (std::atomic<float>& loudest, float level)
    {
        auto held = loudest.load(std::memory_order_relaxed);
        while (level > held && !loudest.compare_exchange_weak(held, level, std::memory_order_relaxed))
        {
        }
    };
    const auto& levels = engine.levels();
    for (size_t b = 0; b < levels.size(); ++b)
    {
        raise(inputLevels[b], levels[b].input);
        raise(outputLevels[b], levels[b].output);
    }
}

MultibandDynamicsEngine::Levels MultibandDynamicsDevice::takeBandLevels(int band)
{
    if (!juce::isPositiveAndBelow(band, MultibandDynamicsEngine::bandCount))
        return {};
    const auto b = static_cast<size_t>(band);
    return { inputLevels[b].exchange(0.0f, std::memory_order_relaxed),
             outputLevels[b].exchange(0.0f, std::memory_order_relaxed) };
}
}
