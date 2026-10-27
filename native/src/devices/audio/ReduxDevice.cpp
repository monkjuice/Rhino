#include "ReduxDevice.h"

namespace rhino
{
namespace
{
constexpr int picturePoints = 256;
}

ReduxEngine::Settings ReduxDevice::currentSettings() const
{
    ReduxEngine::Settings settings;
    settings.rateHz = rate.value();
    settings.jitter = jitter.value();
    settings.preFilter = preFilter.on();
    settings.postFilter = postFilter.on();
    settings.postOctave = postOctave.value();
    settings.bits = bits.value();
    settings.shape = shape.value();
    settings.dcShift = dcShift.on();
    settings.dryWet = dryWet.value();
    return settings;
}

void ReduxDevice::prepare(double newRate, int)
{
    pictureRate = newRate;
    engine.prepare(newRate);
}

void ReduxDevice::clear()
{
    engine.setSettings(currentSettings());
    engine.reset();
}

void ReduxDevice::process(RenderBlock& block)
{
    engine.setSettings(currentSettings());
    engine.process(block.channels, block.numChannels, block.numSamples);
}

// A sine before and after, worked out by an engine of the picture's own: the
// steps are the rate, their heights the bits.
void ReduxDevice::describe(DeviceDisplay& display)
{
    auto now = currentSettings();
    now.dryWet = 1.0f;
    if (!pictured.has_value() || !(*pictured == now))
    {
        pictureIn.resize(static_cast<size_t>(picturePoints));
        pictureOut.resize(static_cast<size_t>(picturePoints));
        ReduxEngine::picture(now, pictureRate, pictureIn.data(), pictureOut.data(), picturePoints);
        pictured = now;
    }
    display.traces.push_back({ "Redux", pictureOut });
    display.traces.push_back({ "Input", pictureIn });
}
}
