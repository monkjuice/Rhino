#include "ErosionDevice.h"

namespace rhino
{
namespace
{
// Four milliseconds at 48 kHz: a dozen cycles of the default sine, enough to
// see the noise wander off it.
constexpr int picturePoints = 192;
}

ErosionEngine::Settings ErosionDevice::currentSettings() const
{
    ErosionEngine::Settings settings;
    settings.frequency = frequency.value();
    settings.amount = amount.value();
    settings.noiseBlend = noiseBlend.value();
    settings.filterWidth = filterWidth.value();
    settings.stereoWidth = stereoWidth.value();
    return settings;
}

void ErosionDevice::prepare(double newRate, int)
{
    pictureRate = newRate;
    engine.prepare(newRate);
}

void ErosionDevice::clear()
{
    engine.setSettings(currentSettings());
    engine.reset();
}

void ErosionDevice::process(RenderBlock& block)
{
    engine.setSettings(currentSettings());
    engine.process(block.channels, block.numChannels, block.numSamples);
}

// The modulator itself, from an engine of the picture's own: the shape of what
// swings the delay. The face scales a trace to fill, so Amount, which only
// scales it, is not shown.
void ErosionDevice::describe(DeviceDisplay& display)
{
    const auto now = currentSettings();
    if (!pictured.has_value() || !(*pictured == now))
    {
        picture.resize(static_cast<size_t>(picturePoints));
        ErosionEngine::modulationPicture(now, pictureRate, picture.data(), picturePoints);
        pictured = now;
    }
    display.traces.push_back({ "Modulator", picture });
}
}
