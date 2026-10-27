#include "SaturatorDevice.h"
#include <algorithm>

namespace rhino
{
namespace
{
// Points across the curve's picture, -1 to 1 of input.
constexpr int picturePoints = 129;
}

SaturatorEngine::Settings SaturatorDevice::currentSettings() const
{
    SaturatorEngine::Settings settings;
    settings.driveDb = drive.value();
    settings.curve = static_cast<saturator::Curve>(std::clamp(curve.index(), 0, saturator::curveCount - 1));
    settings.bassThresholdDb = bassThreshold.value();
    settings.postClip = static_cast<saturator::PostClip>(std::clamp(postClip.index(), 0, 2));
    settings.colour = colour.on();
    settings.colourLow = colourLow.value();
    settings.colourHigh = colourHigh.value();
    settings.colourFrequency = colourFrequency.value();
    settings.colourWidth = colourWidth.value();
    settings.waves.drive = wsDrive.value();
    settings.waves.curve = wsCurve.value();
    settings.waves.depth = wsDepth.value();
    settings.waves.linear = wsLinear.value();
    settings.waves.damp = wsDamp.value();
    settings.waves.period = wsPeriod.value();
    settings.outputDb = output.value();
    settings.dryWet = dryWet.value();
    return settings;
}

void SaturatorDevice::prepare(double newRate, int maximumBlockSize)
{
    engine.prepare(newRate, maximumBlockSize);
}

void SaturatorDevice::clear()
{
    // The settings first, so a cleared device starts where its controls are.
    engine.setSettings(currentSettings());
    engine.reset();
}

void SaturatorDevice::process(RenderBlock& block)
{
    engine.setSettings(currentSettings());
    engine.process(block.channels, block.numChannels, block.numSamples);
}

void SaturatorDevice::describe(DeviceDisplay& display)
{
    const auto now = currentSettings();
    if (!pictured.has_value() || !(*pictured == now))
    {
        picture.resize(static_cast<size_t>(picturePoints));
        SaturatorEngine::transferCurve(now, picture.data(), picturePoints);
        pictured = now;
    }
    display.traces.push_back({ juce::String(saturator::curveNames[static_cast<int>(now.curve)]), picture });
}
}
