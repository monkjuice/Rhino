#pragma once
#include "sdk/NativeDevice.h"
#include "dsp/MultibandDynamicsEngine.h"
#include <array>
#include <atomic>

namespace rhino
{
// Multiband Dynamics, after Live's: a compressor and expander on each of
// three bands, above and below two thresholds. The DSP is
// MultibandDynamicsEngine; this declares its controls and says how loud each
// band is for the face's meters.
class MultibandDynamicsDevice final : public NativeAudioEffect
{
public:
    inline static const char* xmlTypeName = "rhino.multiband-dynamics.v1";
    explicit MultibandDynamicsDevice(te::PluginCreationInfo info) : NativeAudioEffect(std::move(info), xmlTypeName) {}

    // Where each control stands in the declaration order, for the face that
    // draws them; tests/MultibandDynamicsTest.cpp holds these to the ids.
    static constexpr int lowBandParameter = 0, lowCrossoverParameter = 1, highBandParameter = 2,
                         highCrossoverParameter = 3, softKneeParameter = 4, rmsParameter = 5,
                         outputParameter = 6, timeParameter = 7, amountParameter = 8;
    // Each band's own controls follow, low band first, in this order.
    enum class BandControl
    {
        active, solo, input, output, belowThreshold, belowRatio, aboveThreshold, aboveRatio, attack, release
    };
    static constexpr int bandControlCount = 10;
    static constexpr int bandParameter(int band, BandControl control)
    {
        return amountParameter + 1 + band * bandControlCount + static_cast<int>(control);
    }

    // The loudest each band has been since this was last asked, as the
    // engine's levels() reads it. Any thread; the face is its one reader.
    MultibandDynamicsEngine::Levels takeBandLevels(int band);

private:
    void prepare(double newRate, int maximumBlockSize) override;
    void clear() override;
    void process(RenderBlock&) override;
    MultibandDynamicsEngine::Settings currentSettings() const;

    struct BandControls
    {
        Param active, solo, input, output, belowThreshold, belowRatio, aboveThreshold, aboveRatio, attack, release;
    };
    // One band's controls, a section named for it.
    BandControls bandControls(const juce::String& band, float attackMs, float releaseMs);

    // Automation and the face address these by position (the constants
    // above), so append, never reorder.
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
    // Raised by the audio thread to the loudest it has played, and taken back
    // to none by the face.
    std::array<std::atomic<float>, MultibandDynamicsEngine::bandCount> inputLevels {}, outputLevels {};
};
}
