#pragma once
#include "sdk/NativeDevice.h"
#include "dsp/MultibandDynamicsEngine.h"
#include <array>
#include <optional>
#include <vector>

namespace rhino
{
// Multiband Dynamics, after Live's: a compressor and expander on each of
// three bands, above and below two thresholds. The DSP is
// MultibandDynamicsEngine; this declares its controls and draws each band's
// curve.
class MultibandDynamicsDevice final : public NativeAudioEffect
{
public:
    inline static const char* xmlTypeName = "rhino.multiband-dynamics.v1";
    explicit MultibandDynamicsDevice(te::PluginCreationInfo info) : NativeAudioEffect(std::move(info), xmlTypeName) {}

    // Each band's curve as it stands, drawn from the engine's own function.
    void describe(DeviceDisplay&) override;

private:
    void prepare(double newRate, int maximumBlockSize) override;
    void clear() override;
    void process(RenderBlock&) override;
    MultibandDynamicsEngine::Settings currentSettings() const;

    struct BandControls
    {
        Param active, solo, input, output, belowThreshold, belowRatio, aboveThreshold, aboveRatio, attack, release;
    };
    // One band's controls, a section of the "Bands" tab group named for it.
    BandControls bandControls(const juce::String& band, float attackMs, float releaseMs);

    // Automation addresses these by position, so append, never reorder. A
    // generated face fills its columns two controls tall, so each band
    // switch stands over the crossover it moves.
    Param lowBand = param("lowBand", "Low Band").toggle(true);
    Param lowCrossover = param("lowCrossover", "Low Crossover").range(30.0f, 3000.0f).defaultValue(120.0f)
                             .skewAround(300.0f).unit(ParamUnit::hertz);
    Param highBand = param("highBand", "High Band").toggle(true);
    Param highCrossover = param("highCrossover", "High Crossover").range(300.0f, 15000.0f).defaultValue(2500.0f)
                              .skewAround(2500.0f).unit(ParamUnit::hertz);
    Param softKnee = param("softKnee", "Soft Knee").toggle(true);
    Param rms = param("rms", "RMS").toggle(true);
    Param output = param("output", "Output").range(-12.0f, 12.0f).defaultValue(0.0f).unit(ParamUnit::decibels);
    Param time = param("time", "Time").range(0.1f, 10.0f).defaultValue(1.0f).skewAround(1.0f).unit(ParamUnit::percent);
    Param amount = param("amount", "Amount").range(0.0f, 1.0f).defaultValue(1.0f).unit(ParamUnit::percent);
    // Live's attack and release for each band.
    std::array<BandControls, MultibandDynamicsEngine::bandCount> bands { bandControls("Low", 50.0f, 300.0f),
                                                                          bandControls("Mid", 10.0f, 200.0f),
                                                                          bandControls("High", 5.0f, 100.0f) };

    MultibandDynamicsEngine engine;
    // What the face was last shown, kept while the controls stand still: a
    // dragged knob asks for the picture every frame.
    std::optional<MultibandDynamicsEngine::Settings> pictured;
    std::vector<DeviceDisplay::Trace> picture;
};
}
