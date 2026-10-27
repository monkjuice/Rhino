#pragma once
#include "sdk/NativeDevice.h"
#include "dsp/AmpEngine.h"

namespace rhino
{
// Amp, after Live's: seven guitar amplifiers behind one set of knobs. The DSP
// is AmpEngine; this declares its controls.
class AmpDevice final : public NativeAudioEffect
{
public:
    inline static const char* xmlTypeName = "rhino.amp.v1";
    explicit AmpDevice(te::PluginCreationInfo info) : NativeAudioEffect(std::move(info), xmlTypeName) {}

private:
    void prepare(double newRate, int maximumBlockSize) override;
    void clear() override;
    void process(RenderBlock&) override;
    int latencySamples() const override { return engine.latencySamples(); }
    AmpEngine::Settings currentSettings() const;

    // An amp's knobs read 0 to 10.
    static juce::String knobText(float value) { return juce::String(value, 1); }

    // Automation addresses these by position, so append, never reorder. A
    // generated face fills its columns two controls tall: the two choosers,
    // then Gain over Volume, then the tone controls in a group of their own.
    Param type = param("type", "Amp Type")
                     .choices(juce::StringArray(AmpEngine::modelNames, AmpEngine::modelCount));
    Param output = param("output", "Output").choices({ "Mono", "Dual" });
    Param gain = param("gain", "Gain").range(0.0f, 10.0f).defaultValue(5.0f).format(knobText);
    Param volume = param("volume", "Volume").range(0.0f, 10.0f).defaultValue(5.0f).format(knobText);
    Param dryWet = param("dryWet", "Dry/Wet").range(0.0f, 1.0f).defaultValue(1.0f).unit(ParamUnit::percent);
    Param bass = param("bass", "Bass").range(0.0f, 10.0f).defaultValue(5.0f).format(knobText).section("Tone");
    Param middle = param("middle", "Middle").range(0.0f, 10.0f).defaultValue(5.0f).format(knobText).section("Tone");
    Param treble = param("treble", "Treble").range(0.0f, 10.0f).defaultValue(5.0f).format(knobText).section("Tone");
    Param presence = param("presence", "Presence").range(0.0f, 10.0f).defaultValue(5.0f).format(knobText)
                         .section("Tone");

    AmpEngine engine;
};
}
