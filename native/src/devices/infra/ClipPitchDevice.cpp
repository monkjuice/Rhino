#include "infra/ClipPitchDevice.h"
#include "dsp/ClipEnvelope.h"
#include <algorithm>
#include <cmath>

// The hidden plugin behind an audio clip's Pitch envelope: see
// ClipPitchDevice.h.

namespace rhino
{
namespace
{
const juce::Identifier semitonesID {"semitones"};
// What the stretcher is asked to emit per call, which is also the largest
// piece of a block it is handed at once: a block larger than the engine
// promised is worked in pieces of this rather than overrunning the queues.
constexpr int stretchBlock = 512;
}

ClipPitchDevice::ClipPitchDevice(te::PluginCreationInfo info) : te::Plugin(info)
{
    const auto range = clipEnvelopeRange(ClipEnvelopeKind::pitch);
    semitones = addParam("semitones", "Pitch", {range.minimum, range.maximum},
                         [] (float value)
                         {
                             return std::abs(value) < 0.005f ? juce::String("0 st")
                                                            : juce::String(value, 2) + " st";
                         },
                         [] (const juce::String& text) { return text.getFloatValue(); });
    semitonesValue.referTo(state, semitonesID, getUndoManager(), range.neutral);
    semitones->attachToCurrentValue(semitonesValue);
}

ClipPitchDevice::~ClipPitchDevice()
{
    notifyListenersOfDeletion();
    semitones->detachFromCurrentValue();
}

void ClipPitchDevice::initialise(const te::PluginInitialisationInfo& info)
{
    sampleRate = info.sampleRate > 0.0 ? info.sampleRate : 44100.0;
    stretcher = std::make_unique<te::TimeStretcher>();
    stretcher->initialise(sampleRate, stretchBlock, 2, stretchMode, {}, true);
    if (!stretcher->isInitialised())
    {
        stretcher.reset();
        buffered = latencySamples = 0;
        return;
    }
    // The output queue starts with enough silence to make the whole delay
    // the fixed one, which is never less than the frames the stretcher may
    // ask for at once. The queues hold that, what the stretcher has buffered
    // and a few blocks besides, sized here so a block never allocates.
    const auto wanted = juce::roundToInt(latencySeconds * sampleRate);
    buffered = std::max(stretcher->getMaxFramesNeeded(), wanted - stretcher->getLatencySamples());
    latencySamples = buffered + stretcher->getLatencySamples();
    jassert(latencySamples == wanted);
    input.setSize(2, buffered + stretchBlock * 4);
    output.setSize(2, buffered + stretchBlock * 4);
    scratch.setSize(2, stretchBlock);
    restart(clampClipEnvelopeValue(ClipEnvelopeKind::pitch, semitones->getCurrentValue()));
}

void ClipPitchDevice::restart(float shift)
{
    if (stretcher == nullptr)
        return;
    stretcher->reset();
    stretcher->setSpeedAndPitch(1.0f, shift);
    input.reset();
    output.reset();
    output.writeSilence(buffered);
}

void ClipPitchDevice::reset()
{
    restart(clampClipEnvelopeValue(ClipEnvelopeKind::pitch, semitones->getCurrentValue()));
}

// Everything the clip is delayed by: the frames the queue lays down so a block
// can always be answered, and the stretcher's own delay on top. The engine's
// pitch shifter reported only the first.
double ClipPitchDevice::getLatencySeconds()
{
    // Asked before initialise too, as the graph asks whether a clip has any
    // delay at all.
    return latencySamples > 0 ? latencySamples / sampleRate : latencySeconds;
}

double ClipPitchDevice::getTailLength() const
{
    // An impulse leaves after the delay, smeared over about one more
    // stretcher window.
    return latencySeconds + 0.25;
}

void ClipPitchDevice::applyToBuffer(const te::PluginRenderContext& context)
{
    auto* buffer = context.destBuffer;
    if (stretcher == nullptr || buffer == nullptr || buffer->getNumChannels() == 0 || context.bufferNumSamples <= 0)
        return;
    const auto shift = clampClipEnvelopeValue(ClipEnvelopeKind::pitch, semitones->getCurrentValue());
    for (int done = 0; done < context.bufferNumSamples;)
    {
        const auto count = std::min(stretchBlock, context.bufferNumSamples - done);
        processChunk(*buffer, context.bufferStartSample + done, count, shift);
        done += count;
    }
}

void ClipPitchDevice::processChunk(juce::AudioBuffer<float>& buffer, int start, int count, float shift)
{
    // The stretcher works in stereo: a mono clip goes in on both sides and
    // comes back from the first.
    const auto channels = buffer.getNumChannels();
    for (int channel = 0; channel < 2; ++channel)
        scratch.copyFrom(channel, 0, buffer, std::min(channel, channels - 1), start, count);
    input.write(scratch, 0, count);
    stretcher->setSpeedAndPitch(1.0f, shift);
    for (auto needed = stretcher->getFramesNeeded();
         input.getNumReady() >= needed && output.getFreeSpace() >= stretchBlock;
         needed = stretcher->getFramesNeeded())
    {
        stretcher->processData(input, needed, output);
        if (output.getNumReady() >= count)
            break;
    }
    if (output.getNumReady() >= count)
        output.read(scratch, 0, count);
    else
        scratch.clear(0, count);
    for (int channel = 0; channel < channels; ++channel)
        buffer.copyFrom(channel, start, scratch, std::min(channel, 1), 0, count);
}

void ClipPitchDevice::restorePluginStateFromValueTree(const juce::ValueTree& source)
{
    te::copyPropertiesToCachedValues(source, semitonesValue);
    for (auto* parameter : getAutomatableParameters())
        parameter->updateFromAttachedValue();
}
}
