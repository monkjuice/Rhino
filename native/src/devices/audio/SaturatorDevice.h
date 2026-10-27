#pragma once
#include "sdk/NativeDevice.h"
#include "dsp/SaturatorEngine.h"
#include <optional>
#include <vector>

namespace rhino
{
// Saturator, after Live's: a waveshaper with eight curves, colour filters
// that choose what saturates, a post clip and a dry/wet mix. The DSP is
// SaturatorEngine; this declares its controls and draws its curve.
class SaturatorDevice final : public NativeAudioEffect
{
public:
    inline static const char* xmlTypeName = "rhino.saturator.v1";
    explicit SaturatorDevice(te::PluginCreationInfo info) : NativeAudioEffect(std::move(info), xmlTypeName) {}

    // The curve as it stands, drawn from the engine's own function.
    void describe(DeviceDisplay&) override;

private:
    void prepare(double newRate, int maximumBlockSize) override;
    void clear() override;
    void process(RenderBlock&) override;
    int latencySamples() const override { return engine.latencySamples(); }
    SaturatorEngine::Settings currentSettings() const;

    // Automation addresses these by position, so append, never reorder. The
    // order is also the face's: a generated face fills its columns two
    // controls tall, so the two choosers stand together in the first.
    Param curve = param("curve", "Curve").choices(juce::StringArray(saturator::curveNames, saturator::curveCount));
    Param postClip = param("postClip", "Post Clip").choices(juce::StringArray(saturator::postClipNames, 3));
    Param drive = param("drive", "Drive").range(-36.0f, 36.0f).defaultValue(0.0f).unit(ParamUnit::decibels);
    Param output = param("output", "Output").range(-36.0f, 0.0f).defaultValue(0.0f).unit(ParamUnit::decibels);
    // Bass Shaper's threshold; only that curve reads it.
    Param bassThreshold = param("bassThreshold", "Threshold").range(-50.0f, 0.0f)
                              .defaultValue(-50.0f).unit(ParamUnit::decibels);
    Param dryWet = param("dryWet", "Dry/Wet").range(0.0f, 1.0f).defaultValue(1.0f).unit(ParamUnit::percent);
    // Live's expanded view: the colour filters, and the Waveshaper curve's
    // own controls, one at a time in one place.
    Param colour = param("colour", "Colour").toggle(false).section("Colour", "Expanded");
    Param colourLow = param("colourLow", "Colour Amt Lo").range(-1.0f, 1.0f).defaultValue(0.0f)
                          .unit(ParamUnit::percent).section("Colour", "Expanded");
    Param colourHigh = param("colourHigh", "Colour Amt Hi").range(-1.0f, 1.0f).defaultValue(0.0f)
                           .unit(ParamUnit::percent).section("Colour", "Expanded");
    Param colourFrequency = param("colourFrequency", "Colour Frequency").range(50.0f, 15000.0f)
                                .defaultValue(1000.0f).skewAround(1000.0f).unit(ParamUnit::hertz)
                                .section("Colour", "Expanded");
    Param colourWidth = param("colourWidth", "Colour Width").range(0.0f, 1.0f).defaultValue(0.3f)
                            .unit(ParamUnit::percent).section("Colour", "Expanded");
    Param wsDrive = param("wsDrive", "Waveshaper Drive").range(0.0f, 1.0f).defaultValue(0.5f)
                        .unit(ParamUnit::percent).section("Waveshaper", "Expanded");
    Param wsCurve = param("wsCurve", "Waveshaper Curve").range(0.0f, 1.0f).defaultValue(0.0f)
                        .unit(ParamUnit::percent).section("Waveshaper", "Expanded");
    Param wsDepth = param("wsDepth", "Waveshaper Depth").range(0.0f, 1.0f).defaultValue(0.0f)
                        .unit(ParamUnit::percent).section("Waveshaper", "Expanded");
    Param wsLinear = param("wsLinear", "Waveshaper Linear").range(0.0f, 1.0f).defaultValue(0.5f)
                         .unit(ParamUnit::percent).section("Waveshaper", "Expanded");
    Param wsDamp = param("wsDamp", "Waveshaper Damp").range(0.0f, 1.0f).defaultValue(0.0f)
                       .unit(ParamUnit::percent).section("Waveshaper", "Expanded");
    Param wsPeriod = param("wsPeriod", "Waveshaper Period").range(0.0f, 1.0f).defaultValue(0.0f)
                         .unit(ParamUnit::percent).section("Waveshaper", "Expanded");

    SaturatorEngine engine;
    // What the face was last shown, kept while the controls stand still: a
    // dragged knob asks for the picture every frame.
    std::optional<SaturatorEngine::Settings> pictured;
    std::vector<float> picture;
};
}
