#pragma once
#include "sdk/NativeDevice.h"
#include "dsp/ReduxEngine.h"
#include <optional>
#include <vector>

namespace rhino
{
// Redux, after Live's: a downsampler and a bit reducer. The DSP is
// ReduxEngine; this declares its controls and pictures what it does to a sine.
class ReduxDevice final : public NativeAudioEffect
{
public:
    inline static const char* xmlTypeName = "rhino.redux.v1";
    explicit ReduxDevice(te::PluginCreationInfo info) : NativeAudioEffect(std::move(info), xmlTypeName) {}

    void describe(DeviceDisplay&) override;

private:
    void prepare(double newRate, int maximumBlockSize) override;
    void clear() override;
    void process(RenderBlock&) override;
    ReduxEngine::Settings currentSettings() const;

    // Automation addresses these by position, so append, never reorder.
    Param rate = param("rate", "Rate").range(50.0f, 40000.0f).defaultValue(11025.0f).skewAround(4000.0f)
                     .unit(ParamUnit::hertz).section("Downsample");
    Param jitter = param("jitter", "Jitter").range(0.0f, 1.0f).defaultValue(0.0f)
                       .unit(ParamUnit::percent).section("Downsample");
    Param preFilter = param("preFilter", "Pre Filter").toggle(true).section("Downsample");
    Param postFilter = param("postFilter", "Post Filter").toggle(false).section("Downsample");
    // Octaves either side of half the rate.
    Param postOctave = param("postOctave", "Post Octave").range(-3.0f, 3.0f).defaultValue(0.0f)
                           .format([] (float value) { return (value > 0.0f ? "+" : "") + juce::String(value, 1) + " oct"; })
                           .section("Downsample");
    Param bits = param("bits", "Bits").range(1.0f, 16.0f).defaultValue(8.0f)
                     .format([] (float value) { return juce::String(value, 2) + " bits"; })
                     .section("Bit Reduction");
    Param shape = param("shape", "Shape").range(0.0f, 1.0f).defaultValue(0.0f)
                      .unit(ParamUnit::percent).section("Bit Reduction");
    Param dcShift = param("dcShift", "DC Shift").toggle(false).section("Bit Reduction");
    Param dryWet = param("dryWet", "Dry/Wet").range(0.0f, 1.0f).defaultValue(1.0f)
                       .unit(ParamUnit::percent).section("Mix");

    ReduxEngine engine;
    // The rate the device was prepared at, which its picture is drawn at.
    double pictureRate = 48000.0;
    std::optional<ReduxEngine::Settings> pictured;
    std::vector<float> pictureIn, pictureOut;
};
}
