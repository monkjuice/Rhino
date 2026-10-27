#pragma once
#include "sdk/NativeDevice.h"
#include "dsp/ErosionEngine.h"
#include <optional>
#include <vector>

namespace rhino
{
// Erosion, after Live 12.4's: a short delay modulated at audio rate by a sine
// and by band-passed noise. The DSP is ErosionEngine; this declares its
// controls and pictures the modulator.
class ErosionDevice final : public NativeAudioEffect
{
public:
    inline static const char* xmlTypeName = "rhino.erosion.v1";
    explicit ErosionDevice(te::PluginCreationInfo info) : NativeAudioEffect(std::move(info), xmlTypeName) {}

    void describe(DeviceDisplay&) override;

private:
    void prepare(double newRate, int maximumBlockSize) override;
    void clear() override;
    void process(RenderBlock&) override;
    int latencySamples() const override { return engine.latencySamples(); }
    ErosionEngine::Settings currentSettings() const;

    // Automation addresses these by position, so append, never reorder.
    Param frequency = param("frequency", "Freq").range(30.0f, 18000.0f).defaultValue(3000.0f).skewAround(1500.0f)
                          .unit(ParamUnit::hertz);
    Param amount = param("amount", "Amount").range(0.0f, 1.0f).defaultValue(0.5f).unit(ParamUnit::percent);
    Param noiseBlend = param("noiseBlend", "Noise Blend").range(0.0f, 1.0f).defaultValue(0.5f)
                           .unit(ParamUnit::percent);
    Param filterWidth = param("filterWidth", "Filter Width").range(0.0f, 1.0f).defaultValue(0.5f)
                            .unit(ParamUnit::percent);
    Param stereoWidth = param("stereoWidth", "Stereo Width").range(0.0f, 1.0f).defaultValue(0.0f)
                            .unit(ParamUnit::percent);

    ErosionEngine engine;
    // The rate the device was prepared at, which its picture is drawn at.
    double pictureRate = 48000.0;
    std::optional<ErosionEngine::Settings> pictured;
    std::vector<float> picture;
};
}
