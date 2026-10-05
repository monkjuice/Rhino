#include "RhinoSpaceDevice.h"
#include <algorithm>
#include <cmath>

namespace rhino
{
void RhinoSpaceDevice::prepare(double newRate, int maximumBlockSize)
{
    rate = newRate;
    const auto delaySamples = static_cast<size_t>(std::max(1, static_cast<int>(rate * 2.0)));
    delayL.assign(delaySamples, 0.0f);
    delayR.assign(delaySamples, 0.0f);
    // The base never hands over more than this at once, so the dry copy
    // never has to grow on the audio thread.
    dryL.assign(static_cast<size_t>(maximumBlockSize), 0.0f);
    dryR.assign(static_cast<size_t>(maximumBlockSize), 0.0f);
    reverb.setSampleRate(rate);
}

void RhinoSpaceDevice::clear()
{
    std::fill(delayL.begin(), delayL.end(), 0.0f);
    std::fill(delayR.begin(), delayR.end(), 0.0f);
    writeIndex = 0;
    smoothedDelaySamples = delayL.size() > 2 ? delayFor(size.value()) : 1.0f;
    reverb.reset();
}

float RhinoSpaceDevice::delayFor(float room) const
{
    return static_cast<float>(juce::jlimit(1, static_cast<int>(delayL.size()) - 1,
                                           static_cast<int>((0.045 + std::clamp(room, 0.0f, 1.0f) * 0.72) * rate)));
}

// The echo's repeats until they are 60 dB down, plus the reverb's own decay.
// Read on the message thread from the settings as they stand.
double RhinoSpaceDevice::tailSeconds() const
{
    const auto room = std::clamp(size.value(), 0.0f, 1.0f);
    const auto feedback = std::clamp(static_cast<double>(smear.value()), 0.01, 0.95);
    const auto delaySeconds = 0.045 + room * 0.72;
    const auto repeats = std::log(0.001) / std::log(feedback);
    return std::min(30.0, delaySeconds * repeats + 4.0);
}

void RhinoSpaceDevice::process(RenderBlock& block)
{
    if (block.numChannels == 0)
        return;
    const auto channels = block.numChannels;
    const auto numSamples = block.numSamples;
    auto* left = block.channels[0];
    auto* right = channels > 1 ? block.channels[1] : left;
    const auto wetMix = std::clamp(mix.value(), 0.0f, 1.0f);
    const auto room = std::clamp(size.value(), 0.0f, 1.0f);
    const auto feedback = std::clamp(smear.value(), 0.0f, 0.95f);
    const auto driveAmount = 1.0f + std::clamp(drive.value(), 0.0f, 1.0f) * 8.0f;
    const auto driveScale = std::tanh(driveAmount);
    const auto stereoWidth = std::clamp(width.value(), 0.0f, 2.0f);
    const auto output = juce::Decibels::decibelsToGain(outputDb.value());
    const auto targetDelaySamples = delayFor(room);
    const auto length = static_cast<int>(delayL.size());

    juce::Reverb::Parameters params;
    params.roomSize = room;
    params.damping = 0.2f + (1.0f - feedback) * 0.55f;
    params.wetLevel = 0.45f;
    params.dryLevel = 0.25f;
    params.width = std::clamp(stereoWidth * 0.5f, 0.0f, 1.0f);
    params.freezeMode = 0.0f;
    reverb.setParameters(params);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto inL = left[i];
        const auto inR = channels > 1 ? right[i] : inL;
        dryL[static_cast<size_t>(i)] = inL;
        dryR[static_cast<size_t>(i)] = inR;

        smoothedDelaySamples += (targetDelaySamples - smoothedDelaySamples) * 0.0015f;
        smoothedDelaySamples = juce::jlimit(1.0f, static_cast<float>(length - 2), smoothedDelaySamples);
        auto readPosition = static_cast<float>(writeIndex) - smoothedDelaySamples;
        while (readPosition < 0.0f)
            readPosition += static_cast<float>(length);
        const auto readIndex0 = static_cast<int>(readPosition) % length;
        const auto readIndex1 = (readIndex0 + 1) % length;
        const auto fraction = readPosition - std::floor(readPosition);
        const auto delayedL = delayL[static_cast<size_t>(readIndex0)]
            + (delayL[static_cast<size_t>(readIndex1)] - delayL[static_cast<size_t>(readIndex0)]) * fraction;
        const auto delayedR = delayR[static_cast<size_t>(readIndex0)]
            + (delayR[static_cast<size_t>(readIndex1)] - delayR[static_cast<size_t>(readIndex0)]) * fraction;
        const auto drivenL = std::tanh((inL + delayedR * 0.18f) * driveAmount) / driveScale;
        const auto drivenR = std::tanh((inR + delayedL * 0.18f) * driveAmount) / driveScale;
        delayL[static_cast<size_t>(writeIndex)] = std::clamp(drivenL + delayedL * feedback, -1.5f, 1.5f);
        delayR[static_cast<size_t>(writeIndex)] = std::clamp(drivenR + delayedR * feedback, -1.5f, 1.5f);
        writeIndex = (writeIndex + 1) % length;

        left[i] = delayedL;
        if (channels > 1)
            right[i] = delayedR;
    }

    reverb.processStereo(left, right, numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        auto wetL = left[i];
        auto wetR = channels > 1 ? right[i] : wetL;
        const auto mid = (wetL + wetR) * 0.5f;
        const auto side = (wetL - wetR) * 0.5f * stereoWidth;
        wetL = mid + side;
        wetR = mid - side;
        // Not clamped: the chain is floating point, and a hard limit here
        // would square off a hot track before its fader could bring it down.
        left[i] = (dryL[static_cast<size_t>(i)] * (1.0f - wetMix) + wetL * wetMix) * output;
        if (channels > 1)
            right[i] = (dryR[static_cast<size_t>(i)] * (1.0f - wetMix) + wetR * wetMix) * output;
    }
}
}
