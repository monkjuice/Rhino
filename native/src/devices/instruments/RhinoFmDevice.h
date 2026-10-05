#pragma once
#include "FmEngine.h"
#include "sdk/NativeDevice.h"
#include <array>

namespace rhino
{
// A four-operator FM synth, written on the device SDK. How it sounds is
// FmEngine's (core/FmEngine.h); this turns the controls into the engine's
// settings and the MIDI into its calls. Its defaults are an electric piano: a
// pair of stacks, one carrying a 14:1 tine that dies away in a fifth of a
// second.
class RhinoFmDevice final : public NativeInstrument
{
public:
    inline static const char* xmlTypeName = "rhino.fm.v1";
    explicit RhinoFmDevice(te::PluginCreationInfo info) : NativeInstrument(std::move(info), xmlTypeName) {}

private:
    struct OperatorControls
    {
        Param ratio, detune, level, attack, decay, sustain, release;
    };
    struct OperatorDefaults
    {
        float ratio, detune, level, attack, decay, sustain, release;
    };
    static juce::StringArray algorithmNames();
    OperatorControls operatorControls(int number, OperatorDefaults);

    void prepare(double rate, int maximumBlockSize) override;
    void clear() override;
    void process(RenderBlock&) override;
    void noteOn(int note, float velocity, int channel) override;
    void noteOff(int note, float velocity, int channel) override;
    void allNotesOff() override;
    void controller(int number, int value, int channel) override;
    void pitchWheel(int value, int channel) override;

    // Automation addresses these by position: append, never reorder.
    Param algorithm = param("algorithm", "Algorithm").choices(algorithmNames(), 4).section("Voice");
    Param feedback = param("feedback", "Feedback").range(0.0f, 1.0f).unit(ParamUnit::percent).section("Voice");
    Param depth = param("depth", "Depth").range(0.0f, 2.0f).defaultValue(1.0f).unit(ParamUnit::percent)
                      .section("Voice");
    Param velocity = param("velocity", "Velocity").range(0.0f, 1.0f).defaultValue(0.7f).unit(ParamUnit::percent)
                         .section("Voice");
    Param mono = param("mono", "Mono").toggle().section("Voice");
    Param glide = param("glide", "Glide").range(0.0f, 2.0f).skewAround(0.2f).unit(ParamUnit::seconds)
                      .section("Voice");
    Param outputDb = param("outputDb", "Output").range(-36.0f, 6.0f).defaultValue(0.0f).unit(ParamUnit::decibels)
                         .section("Voice");
    std::array<OperatorControls, FmEngine::operators> ops {
        operatorControls(1, { 1.0f, 0.0f, 0.85f, 0.002f, 2.5f, 0.0f, 0.5f }),
        operatorControls(2, { 1.0f, 0.0f, 0.42f, 0.002f, 1.2f, 0.15f, 0.5f }),
        operatorControls(3, { 1.0f, 6.0f, 0.6f, 0.002f, 1.6f, 0.0f, 0.4f }),
        operatorControls(4, { 14.0f, 0.0f, 0.22f, 0.001f, 0.18f, 0.0f, 0.2f }),
    };

    FmEngine engine;
    FmEngine::Settings settings;
};
}
