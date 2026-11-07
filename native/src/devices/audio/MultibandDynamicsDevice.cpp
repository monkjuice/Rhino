#include "MultibandDynamicsDevice.h"
#include <algorithm>

namespace rhino
{
namespace
{
// The picture spans the levels Live's display does, -80 to 0 dB, a point a
// decibel. A level is drawn as (dB + 40) / 40, so the unchanged line runs
// corner to corner; what falls below -80 dB is drawn at the floor, and
// nothing is drawn more than 20 dB over 0.
constexpr int picturePoints = 81;
constexpr double pictureFloorDb = -80.0;

float drawnAt(double decibels)
{
    return static_cast<float>(std::clamp((decibels + 40.0) / 40.0, -1.0, 1.5));
}

juce::String ratioText(float ratio)
{
    return ratio >= MultibandDynamicsEngine::infiniteRatio ? juce::String("inf:1")
                                                           : juce::String(ratio, ratio < 10.0f ? 2 : 1) + ":1";
}

juce::String timeText(float milliseconds)
{
    return milliseconds >= 1000.0f ? juce::String(milliseconds / 1000.0f, 2) + " s"
                                   : juce::String(milliseconds, milliseconds < 10.0f ? 2 : milliseconds < 100.0f ? 1 : 0) + " ms";
}

constexpr const char* bandNames[MultibandDynamicsEngine::bandCount] { "Low", "Mid", "High" };
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
}

// A line per band that is split off, the level it puts out against a steady
// level in, and the line a ratio of 1 draws, which is also what keeps the
// face's scale where it is.
void MultibandDynamicsDevice::describe(DeviceDisplay& display)
{
    const auto now = currentSettings();
    if (!pictured.has_value() || !(*pictured == now))
    {
        picture.clear();
        const auto curve = [] (auto levelOut)
        {
            std::vector<float> points(static_cast<size_t>(picturePoints));
            for (int k = 0; k < picturePoints; ++k)
                points[static_cast<size_t>(k)] = drawnAt(levelOut(pictureFloorDb + static_cast<double>(k)));
            return points;
        };
        for (int b = 0; b < MultibandDynamicsEngine::bandCount; ++b)
            if (MultibandDynamicsEngine::bandExists(now, b))
                picture.push_back({ bandNames[b], curve([&now, b] (double level)
                                                        { return MultibandDynamicsEngine::transferDb(now, b, level); }) });
        picture.push_back({ "1:1", curve([] (double level) { return level; }) });
        pictured = now;
    }
    display.traces.insert(display.traces.end(), picture.begin(), picture.end());
}
}
