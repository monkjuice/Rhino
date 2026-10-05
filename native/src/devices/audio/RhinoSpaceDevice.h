#pragma once
#include "sdk/NativeDevice.h"
#include <vector>

namespace rhino
{
// A floating multi-effect: a driven, cross-fed echo into a reverb, widened.
class RhinoSpaceDevice final : public NativeAudioEffect
{
public:
    inline static const char* xmlTypeName = "rhino.space.v1";
    explicit RhinoSpaceDevice(te::PluginCreationInfo info) : NativeAudioEffect(std::move(info), xmlTypeName) {}

private:
    void prepare(double newRate, int maximumBlockSize) override;
    void clear() override;
    void process(RenderBlock&) override;
    double tailSeconds() const override;
    // The echo's length in samples for a room size, inside the delay line.
    float delayFor(float room) const;

    // Automation addresses these by position, so append, never reorder.
    Param mix = param("mix", "Mix").range(0.0f, 1.0f).defaultValue(0.35f).unit(ParamUnit::percent);
    Param size = param("size", "Size").range(0.0f, 1.0f).defaultValue(0.55f).unit(ParamUnit::percent);
    Param smear = param("smear", "Smear").range(0.0f, 0.95f).defaultValue(0.32f).unit(ParamUnit::percent);
    Param drive = param("drive", "Drive").range(0.0f, 1.0f).defaultValue(0.12f).unit(ParamUnit::percent);
    Param width = param("width", "Width").range(0.0f, 2.0f).defaultValue(1.15f).unit(ParamUnit::ratio);
    Param outputDb = param("outputDb", "Output").range(-24.0f, 12.0f).defaultValue(0.0f).unit(ParamUnit::decibels);

    juce::Reverb reverb;
    std::vector<float> delayL, delayR, dryL, dryR;
    double rate = 48000.0;
    int writeIndex = 0;
    float smoothedDelaySamples = 1.0f;
};
}
