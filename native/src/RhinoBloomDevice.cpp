#include "RhinoBloomDevice.h"
#include <algorithm>
#include <cmath>

namespace rhino
{
RhinoBloomDevice::RhinoBloomDevice(te::PluginCreationInfo info) : Plugin(info)
{
    bloom.referTo(state, "bloom", getUndoManager(), 0.38f);
    chorus.referTo(state, "chorus", getUndoManager(), 0.45f);
    clouds.referTo(state, "clouds", getUndoManager(), 0.34f);
    plate.referTo(state, "plate", getUndoManager(), 0.28f);
    colour.referTo(state, "colour", getUndoManager(), 0.18f);
    outputDb.referTo(state, "outputDb", getUndoManager(), -3.0f);

    bloomParam = addParam("bloom", "Bloom", {0.0f, 1.0f});
    chorusParam = addParam("chorus", "Chorus", {0.0f, 1.0f});
    cloudsParam = addParam("clouds", "Clouds", {0.0f, 1.0f});
    plateParam = addParam("plate", "Plate", {0.0f, 1.0f});
    colourParam = addParam("colour", "Colour", {0.0f, 1.0f});
    outputParam = addParam("outputDb", "Output", {-24.0f, 6.0f});

    bloomParam->attachToCurrentValue(bloom);
    chorusParam->attachToCurrentValue(chorus);
    cloudsParam->attachToCurrentValue(clouds);
    plateParam->attachToCurrentValue(plate);
    colourParam->attachToCurrentValue(colour);
    outputParam->attachToCurrentValue(outputDb);

    auto percentText = [] (float v) { return juce::String(juce::roundToInt(v * 100.0f)) + "%"; };
    bloomParam->valueToStringFunction = percentText;
    chorusParam->valueToStringFunction = percentText;
    cloudsParam->valueToStringFunction = percentText;
    plateParam->valueToStringFunction = percentText;
    colourParam->valueToStringFunction = percentText;
    outputParam->valueToStringFunction = [] (float v) { return juce::String(v, 1) + " dB"; };
}

RhinoBloomDevice::~RhinoBloomDevice()
{
    notifyListenersOfDeletion();
    bloomParam->detachFromCurrentValue();
    chorusParam->detachFromCurrentValue();
    cloudsParam->detachFromCurrentValue();
    plateParam->detachFromCurrentValue();
    colourParam->detachFromCurrentValue();
    outputParam->detachFromCurrentValue();
}

void RhinoBloomDevice::initialise(const te::PluginInitialisationInfo& info)
{
    sampleRate = info.sampleRate > 0.0 ? info.sampleRate : 48000.0;
    const auto delaySamples = std::max(1, static_cast<int>(sampleRate * 2.4));
    delayL.assign(static_cast<size_t>(delaySamples), 0.0f);
    delayR.assign(static_cast<size_t>(delaySamples), 0.0f);
    dryL.assign(static_cast<size_t>(std::max(1, info.blockSizeSamples)), 0.0f);
    dryR.assign(static_cast<size_t>(std::max(1, info.blockSizeSamples)), 0.0f);
    reverb.setSampleRate(sampleRate);
    reset();
}

void RhinoBloomDevice::reset()
{
    std::fill(delayL.begin(), delayL.end(), 0.0f);
    std::fill(delayR.begin(), delayR.end(), 0.0f);
    writeIndex = 0;
    chorusPhase = 0.0f;
    reverb.reset();
}

float RhinoBloomDevice::readDelay(const std::vector<float>& delay, float offsetSamples) const
{
    if (delay.empty())
        return 0.0f;

    auto readPosition = static_cast<float>(writeIndex) - offsetSamples;
    const auto size = static_cast<float>(delay.size());
    while (readPosition < 0.0f)
        readPosition += size;
    while (readPosition >= size)
        readPosition -= size;

    const auto index0 = static_cast<int>(readPosition) % static_cast<int>(delay.size());
    const auto index1 = (index0 + 1) % static_cast<int>(delay.size());
    const auto fraction = readPosition - std::floor(readPosition);
    return delay[static_cast<size_t>(index0)]
        + (delay[static_cast<size_t>(index1)] - delay[static_cast<size_t>(index0)]) * fraction;
}

void RhinoBloomDevice::applyToBuffer(const te::PluginRenderContext& context)
{
    if (context.destBuffer == nullptr || context.bufferNumSamples == 0 || delayL.empty() || delayR.empty())
        return;

    SCOPED_REALTIME_CHECK
    auto& buffer = *context.destBuffer;
    const auto channels = buffer.getNumChannels();
    if (channels == 0)
        return;

    const auto needed = static_cast<size_t>(context.bufferNumSamples);
    if (dryL.size() < needed)
    {
        dryL.resize(needed, 0.0f);
        dryR.resize(needed, 0.0f);
    }

    const auto bloomAmount = std::clamp(bloomParam->getCurrentValue(), 0.0f, 1.0f);
    const auto chorusAmount = std::clamp(chorusParam->getCurrentValue(), 0.0f, 1.0f);
    const auto cloudAmount = std::clamp(cloudsParam->getCurrentValue(), 0.0f, 1.0f);
    const auto plateAmount = std::clamp(plateParam->getCurrentValue(), 0.0f, 1.0f);
    const auto colourAmount = std::clamp(colourParam->getCurrentValue(), 0.0f, 1.0f);
    const auto output = juce::Decibels::decibelsToGain(outputParam->getCurrentValue());
    const auto maxDelay = static_cast<float>(delayL.size() - 2);

    juce::Reverb::Parameters params;
    params.roomSize = 0.42f + plateAmount * 0.52f;
    params.damping = 0.72f - cloudAmount * 0.22f;
    params.wetLevel = 0.18f + plateAmount * 0.44f;
    params.dryLevel = 0.2f;
    params.width = 1.0f;
    params.freezeMode = 0.0f;
    reverb.setParameters(params);

    for (int i = 0; i < context.bufferNumSamples; ++i)
    {
        const auto frame = context.bufferStartSample + i;
        const auto inL = buffer.getSample(0, frame);
        const auto inR = channels > 1 ? buffer.getSample(1, frame) : inL;
        dryL[static_cast<size_t>(i)] = inL;
        dryR[static_cast<size_t>(i)] = inR;

        const auto lfo = std::sin(chorusPhase);
        const auto lfoQuadrature = std::sin(chorusPhase + juce::MathConstants<float>::halfPi);
        chorusPhase += static_cast<float>((0.08 + chorusAmount * 0.34) * juce::MathConstants<double>::twoPi / sampleRate);
        if (chorusPhase > juce::MathConstants<float>::twoPi)
            chorusPhase -= juce::MathConstants<float>::twoPi;

        const auto chorusBase = static_cast<float>(sampleRate) * (0.012f + chorusAmount * 0.014f);
        const auto chorusDepth = static_cast<float>(sampleRate) * (0.002f + chorusAmount * 0.010f);
        const auto cloudDelayA = juce::jlimit(1.0f, maxDelay, static_cast<float>(sampleRate) * (0.18f + bloomAmount * 0.25f));
        const auto cloudDelayB = juce::jlimit(1.0f, maxDelay, static_cast<float>(sampleRate) * (0.37f + bloomAmount * 0.52f));

        const auto chorusL = readDelay(delayL, juce::jlimit(1.0f, maxDelay, chorusBase + lfo * chorusDepth));
        const auto chorusR = readDelay(delayR, juce::jlimit(1.0f, maxDelay, chorusBase + lfoQuadrature * chorusDepth));
        const auto cloudL = readDelay(delayR, cloudDelayA) * 0.62f + readDelay(delayL, cloudDelayB) * 0.38f;
        const auto cloudR = readDelay(delayL, cloudDelayA * 1.17f) * 0.62f + readDelay(delayR, cloudDelayB * 0.83f) * 0.38f;

        const auto colouredL = std::tanh((inL + cloudR * 0.16f) * (1.0f + colourAmount * 2.8f));
        const auto colouredR = std::tanh((inR + cloudL * 0.16f) * (1.0f + colourAmount * 2.8f));
        const auto feedback = 0.14f + cloudAmount * 0.58f;
        delayL[static_cast<size_t>(writeIndex)] = std::clamp(colouredL + cloudL * feedback + chorusR * chorusAmount * 0.12f, -1.5f, 1.5f);
        delayR[static_cast<size_t>(writeIndex)] = std::clamp(colouredR + cloudR * feedback + chorusL * chorusAmount * 0.12f, -1.5f, 1.5f);
        writeIndex = (writeIndex + 1) % static_cast<int>(delayL.size());

        const auto wetL = chorusL * (0.18f + chorusAmount * 0.42f) + cloudL * (cloudAmount * 0.78f);
        const auto wetR = chorusR * (0.18f + chorusAmount * 0.42f) + cloudR * (cloudAmount * 0.78f);
        buffer.setSample(0, frame, wetL);
        if (channels > 1)
            buffer.setSample(1, frame, wetR);
    }

    auto* left = buffer.getWritePointer(0, context.bufferStartSample);
    auto* right = channels > 1 ? buffer.getWritePointer(1, context.bufferStartSample) : left;
    reverb.processStereo(left, right, context.bufferNumSamples);

    const auto wetMix = std::clamp(0.18f + bloomAmount * 0.62f, 0.0f, 0.86f);
    for (int i = 0; i < context.bufferNumSamples; ++i)
    {
        const auto frame = context.bufferStartSample + i;
        auto wetL = buffer.getSample(0, frame);
        auto wetR = channels > 1 ? buffer.getSample(1, frame) : wetL;
        const auto side = (wetL - wetR) * (0.56f + chorusAmount * 0.44f);
        const auto mid = (wetL + wetR) * 0.5f;
        wetL = mid + side;
        wetR = mid - side;
        const auto outL = (dryL[static_cast<size_t>(i)] * (1.0f - wetMix) + wetL * wetMix) * output;
        const auto outR = (dryR[static_cast<size_t>(i)] * (1.0f - wetMix) + wetR * wetMix) * output;
        buffer.setSample(0, frame, std::clamp(outL, -1.0f, 1.0f));
        if (channels > 1)
            buffer.setSample(1, frame, std::clamp(outR, -1.0f, 1.0f));
    }
}

void RhinoBloomDevice::restorePluginStateFromValueTree(const juce::ValueTree& source)
{
    te::copyPropertiesToCachedValues(source, bloom, chorus, clouds, plate, colour, outputDb);
    bloomParam->updateFromAttachedValue();
    chorusParam->updateFromAttachedValue();
    cloudsParam->updateFromAttachedValue();
    plateParam->updateFromAttachedValue();
    colourParam->updateFromAttachedValue();
    outputParam->updateFromAttachedValue();
}
}
