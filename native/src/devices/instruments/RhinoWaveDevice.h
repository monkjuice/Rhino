#pragma once
#include <tracktion_engine/tracktion_engine.h>
#include <array>

namespace rhino
{
namespace te = tracktion::engine;

class RhinoWaveDevice final : public te::Plugin
{
public:
    inline static const char* xmlTypeName = "rhino.wave.v1";
    static const char* getPluginName() { return "Rhino Wave"; }
    explicit RhinoWaveDevice(te::PluginCreationInfo);
    ~RhinoWaveDevice() override;
    juce::String getName() const override { return getPluginName(); }
    juce::String getPluginType() override { return xmlTypeName; }
    juce::String getVendor() override { return "Rhino"; }
    juce::String getSelectableDescription() override { return getName(); }
    BusLayout getBusses() const override { return BusLayout::singleStereoInOut(); }
    bool isSynth() override { return true; }
    bool takesMidiInput() override { return true; }
    bool producesAudioWhenNoAudioInput() override { return true; }
    void initialise(const te::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void reset() override;
    void midiPanic() override;
    void applyToBuffer(const te::PluginRenderContext&) override;
    void restorePluginStateFromValueTree(const juce::ValueTree&) override;

private:
    struct Voice
    {
        bool active = false, released = false;
        int note = 0;
        float velocity = 0.0f, phase = 0.0f, osc2Phase = 0.0f, subPhase = 0.0f, motionPhase = 0.0f, lfoPhase = 0.0f;
        float envelope = 0.0f, releaseStart = 0.0f, filterLow = 0.0f, filterBand = 0.0f;
    };

    void trigger(int note, float velocity);
    void release(int note);
    float renderVoice(Voice&);
    float wave(float phase, float motionOffset, float frequency) const;
    void syncSmoothedParameters(bool immediate);
    void smoothParameters();

    juce::CachedValue<float> position, shape, motion, cutoff, filterEnv, driveDb, sub, resonance, attack, decay, sustain, releaseTime;
    juce::CachedValue<float> unison, detune, width, outputDb, lfoRate, lfoPosition, lfoCutoff, lfoPitch, lfoMotion;
    juce::CachedValue<float> osc2Level, osc2Tune;
    te::AutomatableParameter::Ptr positionParam, shapeParam, motionParam, cutoffParam, filterEnvParam, driveParam, subParam, resonanceParam;
    te::AutomatableParameter::Ptr attackParam, decayParam, sustainParam, releaseParam;
    te::AutomatableParameter::Ptr unisonParam, detuneParam, widthParam, outputParam, lfoRateParam, lfoPositionParam, lfoCutoffParam, lfoPitchParam, lfoMotionParam;
    te::AutomatableParameter::Ptr osc2LevelParam, osc2TuneParam;
    std::array<Voice, 12> voices;
    double sampleRate = 48000.0;
    size_t nextVoice = 0;
    float currentPosition = 0.0f, currentShape = 0.0f, currentMotion = 0.0f, currentCutoff = 0.0f, currentFilterEnv = 0.0f;
    float currentDrive = 1.0f, currentSub = 0.0f, currentResonance = 0.0f, currentDetune = 0.0f, currentWidth = 0.0f;
    float currentOutput = 1.0f, currentOsc2Level = 0.0f, currentOsc2Tune = 0.0f;
    float currentLfoRate = 0.0f, currentLfoPosition = 0.0f, currentLfoCutoff = 0.0f, currentLfoPitch = 0.0f, currentLfoMotion = 0.0f;
};
}
