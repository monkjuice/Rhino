#pragma once
#include "sdk/NativeDevice.h"
#include "dsp/TunerEngine.h"

namespace rhino
{
// Tuner, after Live's: a measuring tool, not an effect. It reads the pitch of
// what passes and leaves the audio exactly as it found it. The measuring is
// TunerEngine; the face that shows it is DeviceEditorPanelTuner.cpp.
//
// Its view is declared as controls too, so it is saved with the device and a
// click on the face is an undo step like any other.
class TunerDevice final : public NativeAudioEffect
{
public:
    inline static const char* xmlTypeName = "rhino.tuner.v1";
    explicit TunerDevice(te::PluginCreationInfo info) : NativeAudioEffect(std::move(info), xmlTypeName) {}

    // A meter passes out exactly what it was handed, mono as mono, as the
    // Utility channel strip does.
    BusLayout getBusses() const override { return BusLayout::singlePassThrough(); }
    int getNumOutputChannelsGivenInputs(int numInputChannels) override { return numInputChannels; }

    // The controls by position, for the face.
    static constexpr int referenceParameter = 0;
    static constexpr int viewParameter = 1;
    static constexpr int modeParameter = 2;
    static constexpr int unitsParameter = 3;

    enum class View { classic, histogram };
    enum class Mode { target, strobe };

    // Any thread.
    TunerEngine::Reading reading() const { return engine.reading(); }
    int history(float* frequencies, int count) const { return engine.history(frequencies, count); }
    double analysesPerSecond() const { return engine.analysesPerSecond(); }
    float referenceHz() const { return reference.value(); }
    View view() const { return viewChoice.index() == 1 ? View::histogram : View::classic; }
    Mode mode() const { return modeChoice.index() == 1 ? Mode::strobe : Mode::target; }
    bool showsCents() const { return readout.index() == 1; }

private:
    void prepare(double newRate, int maximumBlockSize) override;
    void clear() override;
    void process(RenderBlock&) override;

    // Automation addresses these by position, so append, never reorder.
    Param reference = param("reference", "Reference").range(410.0f, 480.0f, 1.0f).defaultValue(440.0f)
                          .unit(ParamUnit::hertz);
    Param viewChoice = param("view", "View").choices({ "Classic", "Histogram" });
    Param modeChoice = param("mode", "Classic Mode").choices({ "Target", "Strobe" });
    Param readout = param("units", "Readout").choices({ "Hertz", "Cents" });

    TunerEngine engine;
};
}
