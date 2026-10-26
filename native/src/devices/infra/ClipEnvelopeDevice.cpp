#include "infra/ClipEnvelopeDevice.h"
#include "dsp/ClipEnvelope.h"
#include <cmath>

// The hidden plugin behind an audio clip's Gain and Pan envelopes: see
// ClipEnvelopeDevice.h.

namespace rhino
{
namespace
{
const juce::Identifier gainID {"gain"};
const juce::Identifier panID {"pan"};
const juce::Identifier fadeID {"fade"};

juce::String shareText(float share)
{
    const auto decibels = clipEnvelopeGainDecibels(share);
    return std::isfinite(decibels) ? juce::String(decibels, 1) + " dB" : juce::String("-inf dB");
}

juce::String balanceText(float pan)
{
    if (std::abs(pan) < 0.005f)
        return "C";
    return (pan < 0.0f ? "L" : "R") + juce::String(std::abs(pan) * 100.0f, 0);
}
}

ClipEnvelopeDevice::ClipEnvelopeDevice(te::PluginCreationInfo info) : te::Plugin(info)
{
    auto* undoManager = getUndoManager();
    const auto gainRange = clipEnvelopeRange(ClipEnvelopeKind::gain);
    const auto panRange = clipEnvelopeRange(ClipEnvelopeKind::pan);
    gain = addParam("gain", "Gain", {gainRange.minimum, gainRange.maximum}, shareText,
                    [] (const juce::String& text) { return juce::Decibels::decibelsToGain(text.getFloatValue()); });
    pan = addParam("pan", "Pan", {panRange.minimum, panRange.maximum}, balanceText,
                   [] (const juce::String& text) { return juce::jlimit(-1.0f, 1.0f, text.getFloatValue()); });
    fade = addParam("fade", "Fade", {0.0f, 1.0f},
                    [] (float value) { return juce::String(juce::roundToInt(value * 100.0f)) + "%"; },
                    [] (const juce::String& text) { return juce::jlimit(0.0f, 1.0f, text.getFloatValue() / 100.0f); });
    gainValue.referTo(state, gainID, undoManager, gainRange.neutral);
    panValue.referTo(state, panID, undoManager, panRange.neutral);
    fadeValue.referTo(state, fadeID, undoManager, 1.0f);
    gain->attachToCurrentValue(gainValue);
    pan->attachToCurrentValue(panValue);
    fade->attachToCurrentValue(fadeValue);
}

ClipEnvelopeDevice::~ClipEnvelopeDevice()
{
    notifyListenersOfDeletion();
    gain->detachFromCurrentValue();
    pan->detachFromCurrentValue();
    fade->detachFromCurrentValue();
}

void ClipEnvelopeDevice::initialise(const te::PluginInitialisationInfo&)
{
    primed = false;
}

void ClipEnvelopeDevice::reset()
{
    primed = false;
}

void ClipEnvelopeDevice::applyToBuffer(const te::PluginRenderContext& context)
{
    auto* buffer = context.destBuffer;
    if (buffer == nullptr || context.bufferNumSamples <= 0)
        return;
    const auto share = clampClipEnvelopeValue(ClipEnvelopeKind::gain, gain->getCurrentValue())
                     * juce::jlimit(0.0f, 1.0f, fade->getCurrentValue());
    const auto channels = buffer->getNumChannels();
    auto left = 1.0f, right = 1.0f;
    if (channels >= 2)
        clipEnvelopeBalance(pan->getCurrentValue(), left, right);
    left *= share;
    right *= share;
    if (!primed)
    {
        leftGain = left;
        rightGain = right;
        shareGain = share;
        primed = true;
    }
    for (int channel = 0; channel < channels; ++channel)
    {
        // Channels past the second have no side, so they take the share alone.
        const auto from = channel == 0 ? leftGain : channel == 1 ? rightGain : shareGain;
        const auto to = channel == 0 ? left : channel == 1 ? right : share;
        if (from == 1.0f && to == 1.0f)
            continue;
        buffer->applyGainRamp(channel, context.bufferStartSample, context.bufferNumSamples, from, to);
    }
    leftGain = left;
    rightGain = right;
    shareGain = share;
}

void ClipEnvelopeDevice::restorePluginStateFromValueTree(const juce::ValueTree& source)
{
    te::copyPropertiesToCachedValues(source, gainValue, panValue, fadeValue);
    for (auto* parameter : getAutomatableParameters())
        parameter->updateFromAttachedValue();
}
}
