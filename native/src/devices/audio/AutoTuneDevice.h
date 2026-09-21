#pragma once
#include "AutoTuneEngine.h"
#include <tracktion_engine/tracktion_engine.h>

namespace rhino
{
namespace te = tracktion::engine;

// Rhino Tune: vocal pitch correction.
//
// The device is a thin shell.  Everything that decides how it sounds lives in
// AutoTuneEngine, in RhinoCore, where a test can drive it with a synthesised
// vowel and measure what comes back without an Edit, a graph or a render.
//
// The split between parameters and properties is deliberate.  The thirteen
// things a mix engineer rides during a take -- correction, retune, formants,
// vibrato, the blend -- are automatable parameters.  The scale, the key, the
// tracking range and the two switches are properties: they are choosers, they
// are set once for a song, and a knob is the wrong shape for them.
class AutoTuneDevice final : public te::Plugin
{
public:
    inline static const char* xmlTypeName = "rhino.autotune.v1";
    static const char* getPluginName() { return "Rhino Tune"; }

    explicit AutoTuneDevice(te::PluginCreationInfo);
    ~AutoTuneDevice() override;

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
    double getLatencySeconds() override { return engine.latencySeconds(); }

    // ---- what the editor reads and writes ---------------------------------
    AutoTuneEngine::Readout readout() const;

    PitchClassMask scaleMask() const;
    void setScaleMask(const PitchClassMask&);
    int scaleRoot() const { return juce::jlimit(0, 11, root.get()); }
    MusicalScale scale() const;
    // Writes root, scale and the note mask together, which is what a Root or
    // Scale chooser does.  Toggling one key calls setScaleMask instead, and
    // the mask then no longer matches the named scale -- which is how the
    // editor knows to show it as Custom.
    void applyScale(int root, MusicalScale);
    bool maskMatchesNamedScale() const;

    PitchTracker::Range trackingRange() const;
    void setTrackingRange(PitchTracker::Range);
    bool liveMode() const { return live.get(); }
    void setLiveMode(bool on);
    bool naturalVibrato() const { return natural.get(); }
    void setNaturalVibrato(bool on);
    int scaleDegreeShift() const { return juce::jlimit(-12, 12, degrees.get()); }
    void setScaleDegreeShift(int);

private:
    void writeProperty(const juce::Identifier&, const juce::var&);

    juce::CachedValue<float> strength, retune, flex, human, pitch, fine, formant, follow;
    juce::CachedValue<float> vibrato, vibratoRate, vibratoFade, mix, inputGain;
    juce::CachedValue<int> root, scaleIndex, notes, degrees, range;
    juce::CachedValue<bool> live, natural;

    te::AutomatableParameter::Ptr strengthParam, retuneParam, flexParam, humanParam;
    te::AutomatableParameter::Ptr pitchParam, fineParam, formantParam, followParam;
    te::AutomatableParameter::Ptr vibratoParam, vibratoRateParam, vibratoFadeParam;
    te::AutomatableParameter::Ptr mixParam, inputGainParam;

    AutoTuneEngine engine;
};
}
