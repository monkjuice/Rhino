#pragma once
#include "EqEngine.h"
#include <tracktion_engine/tracktion_engine.h>

namespace rhino
{
namespace te = tracktion::engine;

// Rhino EQ: eight bands and the spectrum they are being drawn over.
//
// The device is a shell, as Rhino Tune is. The filters, the smoothing and the
// analysis tap live in EqEngine, in RhinoCore, so a test can sweep a sine
// through them and measure what comes out without an Edit or a graph.
//
// Parameters and properties split the way they do everywhere else here: the
// twenty-four numbers an engineer rides -- three per band -- plus output and
// scale are automatable parameters, while the eight on switches and the eight
// filter-type choosers are properties. A knob is the wrong shape for a
// chooser with eight entries, and a band being on is not a ramp.
class RhinoEqDevice final : public te::Plugin
{
public:
    inline static const char* xmlTypeName = "rhino.eq.v1";
    static const char* getPluginName() { return "Rhino EQ"; }

    static constexpr int bandCount = EqEngine::bandCount;

    explicit RhinoEqDevice(te::PluginCreationInfo);
    ~RhinoEqDevice() override;

    juce::String getName() const override { return getPluginName(); }
    juce::String getPluginType() override { return xmlTypeName; }
    juce::String getVendor() override { return "Rhino"; }
    juce::String getSelectableDescription() override { return getName(); }
    BusLayout getBusses() const override { return BusLayout::singleStereoInOut(); }
    void initialise(const te::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void reset() override;
    void applyToBuffer(const te::PluginRenderContext&) override;
    void restorePluginStateFromValueTree(const juce::ValueTree&) override;

    // ---- where a parameter lives, so the face can reach one by band -------
    // The order is band 1 frequency, gain, Q, band 2, and so on, which is the
    // order the generic fallback panel would show them in.
    static constexpr int frequencyParameter(int band) { return band * 3; }
    static constexpr int gainParameter(int band) { return band * 3 + 1; }
    static constexpr int qParameter(int band) { return band * 3 + 2; }
    static constexpr int outputGainParameter = bandCount * 3;
    static constexpr int scaleParameter = bandCount * 3 + 1;
    static constexpr int parameterCount = bandCount * 3 + 2;

    // ---- what the face reads and writes ----------------------------------
    // The settings the audio is running on right now, Scale not yet applied:
    // EqEngine::responseDbAt applies it, so the curve and the samples cannot
    // disagree about what Scale did.
    EqEngine::Settings currentSettings() const;
    const SpectrumTap& spectrumTap() const { return engine.tap(); }
    double spectrumRate() const { return engine.rate(); }

    bool bandEnabled(int band) const;
    void setBandEnabled(int band, bool);
    EqFilterType bandType(int band) const;
    void setBandType(int band, EqFilterType);

    EqEngine::AnalyserMode analyserMode() const;
    void setAnalyserMode(EqEngine::AnalyserMode);

    // Which band the face has in hand. It is view state, but it belongs to
    // the device rather than the panel: the rack destroys and rebuilds every
    // panel whenever anything about a track changes, and a selection that
    // resets to band one on an unrelated edit is maddening.
    int selectedBand() const;
    void setSelectedBand(int);

private:
    void writeProperty(const juce::Identifier&, const juce::var&);

    juce::CachedValue<float> frequency[bandCount], gain[bandCount], q[bandCount];
    juce::CachedValue<float> outputGainDb, scalePercent;
    juce::CachedValue<bool> enabled[bandCount];
    juce::CachedValue<int> type[bandCount];
    juce::CachedValue<int> analyser, selected;

    te::AutomatableParameter::Ptr frequencyParam[bandCount], gainParam[bandCount], qParam[bandCount];
    te::AutomatableParameter::Ptr outputGainParam, scaleParam;

    EqEngine engine;
};
}
