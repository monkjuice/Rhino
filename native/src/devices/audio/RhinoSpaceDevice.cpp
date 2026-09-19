#include "RhinoSpaceDevice.h"
#include <algorithm>
#include <cmath>

namespace rhino
{
RhinoSpaceDevice::RhinoSpaceDevice(te::PluginCreationInfo info) : Plugin(info)
{
    mix.referTo(state, "mix", getUndoManager(), 0.35f);
    size.referTo(state, "size", getUndoManager(), 0.55f);
    smear.referTo(state, "smear", getUndoManager(), 0.32f);
    drive.referTo(state, "drive", getUndoManager(), 0.12f);
    width.referTo(state, "width", getUndoManager(), 1.15f);
    outputDb.referTo(state, "outputDb", getUndoManager(), 0.0f);

    mixParam = addParam("mix", "Mix", {0.0f, 1.0f});
    sizeParam = addParam("size", "Size", {0.0f, 1.0f});
    smearParam = addParam("smear", "Smear", {0.0f, 0.95f});
    driveParam = addParam("drive", "Drive", {0.0f, 1.0f});
    widthParam = addParam("width", "Width", {0.0f, 2.0f});
    outputParam = addParam("outputDb", "Output", {-24.0f, 12.0f});

    mixParam->attachToCurrentValue(mix);
    sizeParam->attachToCurrentValue(size);
    smearParam->attachToCurrentValue(smear);
    driveParam->attachToCurrentValue(drive);
    widthParam->attachToCurrentValue(width);
    outputParam->attachToCurrentValue(outputDb);

    mixParam->valueToStringFunction = [] (float v) { return juce::String(juce::roundToInt(v * 100.0f)) + "%"; };
    sizeParam->valueToStringFunction = [] (float v) { return juce::String(juce::roundToInt(v * 100.0f)) + "%"; };
    smearParam->valueToStringFunction = [] (float v) { return juce::String(juce::roundToInt(v * 100.0f)) + "%"; };
    driveParam->valueToStringFunction = [] (float v) { return juce::String(juce::roundToInt(v * 100.0f)) + "%"; };
    widthParam->valueToStringFunction = [] (float v) { return juce::String(v, 2) + "x"; };
    outputParam->valueToStringFunction = [] (float v) { return juce::String(v, 1) + " dB"; };
}

RhinoSpaceDevice::~RhinoSpaceDevice()
{
    notifyListenersOfDeletion();
    mixParam->detachFromCurrentValue();
    sizeParam->detachFromCurrentValue();
    smearParam->detachFromCurrentValue();
    driveParam->detachFromCurrentValue();
    widthParam->detachFromCurrentValue();
    outputParam->detachFromCurrentValue();
}

void RhinoSpaceDevice::initialise(const te::PluginInitialisationInfo& info)
{
    sampleRate = info.sampleRate > 0.0 ? info.sampleRate : 48000.0;
    const auto delaySamples = std::max(1, static_cast<int>(sampleRate * 2.0));
    delayL.assign(static_cast<size_t>(delaySamples), 0.0f);
    delayR.assign(static_cast<size_t>(delaySamples), 0.0f);
    dryL.assign(static_cast<size_t>(std::max(1, info.blockSizeSamples)), 0.0f);
    dryR.assign(static_cast<size_t>(std::max(1, info.blockSizeSamples)), 0.0f);
    writeIndex = 0;
    smoothedDelaySamples = static_cast<float>(juce::jlimit(1, delaySamples - 1,
        static_cast<int>((0.045f + std::clamp(sizeParam->getCurrentValue(), 0.0f, 1.0f) * 0.72f) * static_cast<float>(sampleRate))));
    reverb.setSampleRate(sampleRate);
    reset();
}

void RhinoSpaceDevice::reset()
{
    std::fill(delayL.begin(), delayL.end(), 0.0f);
    std::fill(delayR.begin(), delayR.end(), 0.0f);
    writeIndex = 0;
    if (delayL.size() > 2)
        smoothedDelaySamples = static_cast<float>(juce::jlimit(1, static_cast<int>(delayL.size()) - 1,
            static_cast<int>((0.045f + std::clamp(sizeParam->getCurrentValue(), 0.0f, 1.0f) * 0.72f) * static_cast<float>(sampleRate))));
    else
        smoothedDelaySamples = 1.0f;
    reverb.reset();
}

void RhinoSpaceDevice::applyToBuffer(const te::PluginRenderContext& context)
{
    if (context.destBuffer == nullptr || context.bufferNumSamples == 0 || delayL.empty() || delayR.empty())
        return;

    SCOPED_REALTIME_CHECK
    auto& buffer = *context.destBuffer;
    const auto channels = buffer.getNumChannels();
    if (channels == 0) return;

    const auto needed = static_cast<size_t>(context.bufferNumSamples);
    if (dryL.size() < needed)
    {
        dryL.resize(needed, 0.0f);
        dryR.resize(needed, 0.0f);
    }

    const auto wetMix = std::clamp(mixParam->getCurrentValue(), 0.0f, 1.0f);
    const auto room = std::clamp(sizeParam->getCurrentValue(), 0.0f, 1.0f);
    const auto feedback = std::clamp(smearParam->getCurrentValue(), 0.0f, 0.95f);
    const auto driveAmount = 1.0f + std::clamp(driveParam->getCurrentValue(), 0.0f, 1.0f) * 8.0f;
    const auto stereoWidth = std::clamp(widthParam->getCurrentValue(), 0.0f, 2.0f);
    const auto output = juce::Decibels::decibelsToGain(outputParam->getCurrentValue());
    const auto targetDelaySamples = static_cast<float>(juce::jlimit(1, static_cast<int>(delayL.size()) - 1,
        static_cast<int>((0.045 + room * 0.72) * sampleRate)));

    juce::Reverb::Parameters params;
    params.roomSize = room;
    params.damping = 0.2f + (1.0f - feedback) * 0.55f;
    params.wetLevel = 0.45f;
    params.dryLevel = 0.25f;
    params.width = std::clamp(stereoWidth * 0.5f, 0.0f, 1.0f);
    params.freezeMode = 0.0f;
    reverb.setParameters(params);

    for (int i = 0; i < context.bufferNumSamples; ++i)
    {
        const auto frame = context.bufferStartSample + i;
        const auto inL = buffer.getSample(0, frame);
        const auto inR = channels > 1 ? buffer.getSample(1, frame) : inL;
        dryL[static_cast<size_t>(i)] = inL;
        dryR[static_cast<size_t>(i)] = inR;

        smoothedDelaySamples += (targetDelaySamples - smoothedDelaySamples) * 0.0015f;
        smoothedDelaySamples = juce::jlimit(1.0f, static_cast<float>(delayL.size() - 2), smoothedDelaySamples);
        auto readPosition = static_cast<float>(writeIndex) - smoothedDelaySamples;
        while (readPosition < 0.0f)
            readPosition += static_cast<float>(delayL.size());
        const auto readIndex0 = static_cast<int>(readPosition) % static_cast<int>(delayL.size());
        const auto readIndex1 = (readIndex0 + 1) % static_cast<int>(delayL.size());
        const auto fraction = readPosition - std::floor(readPosition);
        const auto delayedL = delayL[static_cast<size_t>(readIndex0)]
            + (delayL[static_cast<size_t>(readIndex1)] - delayL[static_cast<size_t>(readIndex0)]) * fraction;
        const auto delayedR = delayR[static_cast<size_t>(readIndex0)]
            + (delayR[static_cast<size_t>(readIndex1)] - delayR[static_cast<size_t>(readIndex0)]) * fraction;
        const auto drivenL = std::tanh((inL + delayedR * 0.18f) * driveAmount) / std::tanh(driveAmount);
        const auto drivenR = std::tanh((inR + delayedL * 0.18f) * driveAmount) / std::tanh(driveAmount);
        delayL[static_cast<size_t>(writeIndex)] = std::clamp(drivenL + delayedL * feedback, -1.5f, 1.5f);
        delayR[static_cast<size_t>(writeIndex)] = std::clamp(drivenR + delayedR * feedback, -1.5f, 1.5f);
        writeIndex = (writeIndex + 1) % static_cast<int>(delayL.size());

        buffer.setSample(0, frame, delayedL);
        if (channels > 1)
            buffer.setSample(1, frame, delayedR);
    }

    auto* left = buffer.getWritePointer(0, context.bufferStartSample);
    auto* right = channels > 1 ? buffer.getWritePointer(1, context.bufferStartSample) : left;
    reverb.processStereo(left, right, context.bufferNumSamples);

    for (int i = 0; i < context.bufferNumSamples; ++i)
    {
        const auto frame = context.bufferStartSample + i;
        auto wetL = buffer.getSample(0, frame);
        auto wetR = channels > 1 ? buffer.getSample(1, frame) : wetL;
        const auto mid = (wetL + wetR) * 0.5f;
        const auto side = (wetL - wetR) * 0.5f * stereoWidth;
        wetL = mid + side;
        wetR = mid - side;
        const auto outL = (dryL[static_cast<size_t>(i)] * (1.0f - wetMix) + wetL * wetMix) * output;
        const auto outR = (dryR[static_cast<size_t>(i)] * (1.0f - wetMix) + wetR * wetMix) * output;
        buffer.setSample(0, frame, std::clamp(outL, -1.0f, 1.0f));
        if (channels > 1)
            buffer.setSample(1, frame, std::clamp(outR, -1.0f, 1.0f));
    }
}

void RhinoSpaceDevice::restorePluginStateFromValueTree(const juce::ValueTree& source)
{
    te::copyPropertiesToCachedValues(source, mix, size, smear, drive, width, outputDb);
    mixParam->updateFromAttachedValue();
    sizeParam->updateFromAttachedValue();
    smearParam->updateFromAttachedValue();
    driveParam->updateFromAttachedValue();
    widthParam->updateFromAttachedValue();
    outputParam->updateFromAttachedValue();
}
}
