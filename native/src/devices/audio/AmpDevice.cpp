#include "AmpDevice.h"
#include <algorithm>

namespace rhino
{
AmpEngine::Settings AmpDevice::currentSettings() const
{
    AmpEngine::Settings settings;
    settings.model = static_cast<AmpEngine::Model>(std::clamp(type.index(), 0, AmpEngine::modelCount - 1));
    settings.gain = gain.value();
    settings.bass = bass.value();
    settings.middle = middle.value();
    settings.treble = treble.value();
    settings.presence = presence.value();
    settings.volume = volume.value();
    settings.dual = output.index() == 1;
    settings.dryWet = dryWet.value();
    return settings;
}

void AmpDevice::prepare(double newRate, int maximumBlockSize)
{
    engine.prepare(newRate, maximumBlockSize);
}

void AmpDevice::clear()
{
    // The settings first, so a cleared amp starts where its knobs are.
    engine.setSettings(currentSettings());
    engine.reset();
}

void AmpDevice::process(RenderBlock& block)
{
    engine.setSettings(currentSettings());
    engine.process(block.channels, block.numChannels, block.numSamples);
}
}
