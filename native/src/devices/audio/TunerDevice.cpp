#include "TunerDevice.h"

namespace rhino
{
void TunerDevice::prepare(double newRate, int maximumBlockSize)
{
    engine.prepare(newRate, maximumBlockSize);
}

void TunerDevice::clear()
{
    engine.reset();
}

void TunerDevice::process(RenderBlock& block)
{
    engine.process(block.channels, block.numChannels, block.numSamples);
}
}
