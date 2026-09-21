#pragma once
#include "Session.h"
#include "SpectrumAnalyser.h"
#include <memory>
#include <vector>

namespace rhino
{
// Hosts Rhino's native device faces. Unknown and third-party devices retain a
// generic parameter surface; plug-in-owned editors remain available via Edit.
class DeviceEditorPanel final : public juce::Component,
                                private juce::Timer
{
public:
    static constexpr int standardHeight = 176;

    explicit DeviceEditorPanel(Session&);
    void setTarget(int track, const Session::DeviceSlot&, bool selected);
    int preferredWidth() const;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    std::function<void(juce::String)> status;
    std::function<void()> selected;

private:
    enum class Face { Generic, RhinoSpace, AutoTune, Eq, Vocoder };
    void ensureControls();
    void styleControls();
    void layoutGeneric();
    void layoutRhinoSpace();
    // Rhino Tune's face is its own translation unit, DeviceEditorPanelAutoTune.cpp:
    // it is a meter, a keyboard and two choosers on top of the knobs, and it is
    // the only face that needs the device itself rather than its parameters.
    // Nothing about it belongs in this file's layout arithmetic.
    void layoutAutoTune();
    void paintAutoTune(juce::Graphics&);
    bool handleAutoTuneClick(const juce::MouseEvent&);
    // Rhino EQ's face is DeviceEditorPanelEq.cpp, for the same reason: a
    // curve over a live spectrum, eight handles sitting on it, and three
    // knobs that follow whichever band is in hand. None of that is parameter
    // grid arithmetic either.
    void layoutEq();
    void paintEq(juce::Graphics&);
    void ensureEqControls();
    void styleEqControls();
    bool handleEqMouseDown(const juce::MouseEvent&);
    bool handleEqDrag(const juce::MouseEvent&);
    bool handleEqDoubleClick(const juce::MouseEvent&);
    bool handleEqWheel(const juce::MouseEvent&, const juce::MouseWheelDetails&);
    void showEqTypeMenu(int band);
    void rebuildEqCurve();
    void tickEqSpectrum();
    // Rhino Vocoder's face is DeviceEditorPanelVocoder.cpp: a chooser that
    // decides where the carrier comes from, a bar per band of the filter bank,
    // and two lamps that say whether either signal is arriving. The chooser is
    // the one control on any face that edits the *routing* rather than a
    // parameter, which is why it is drawn rather than made a knob.
    void layoutVocoder();
    void paintVocoder(juce::Graphics&);
    bool handleVocoderClick(const juce::MouseEvent&);
    void tickVocoder();
    // Only the meter, the detected note and the target key repaint; the rest
    // of the face is static and stays out of the frame path.
    void timerCallback() override;
    void showParameterMenu(int index);
    int visibleParameterCount() const;

    Session& session;
    int track = -1, pluginSlot = -1;
    bool syncing = false, isSelected = false;
    Face face = Face::Generic;
    juce::String deviceName;
    std::vector<Session::DeviceParameter> parameters;
    juce::OwnedArray<juce::Label> parameterLabels, parameterValues;
    juce::OwnedArray<juce::Slider> parameterSliders;
    juce::OwnedArray<juce::TextButton> parameterAutomation;
    juce::Label title;
    juce::TextButton power;
    juce::Rectangle<int> contentArea, visualArea;
    // What the meter last drew, so an idle device asks for no frames at all.
    float lastDrawnCents = 0.0f, lastDrawnNote = 0.0f;
    int lastDrawnBand = -2;
    // The vocoder's display asks for a frame only when the bank has moved:
    // the sum of the band envelopes stands in for all forty of them, and the
    // two lamps are a pair of bits.
    float lastDrawnBandSum = -1.0f;
    int lastDrawnLamps = -1;

    // ---- Rhino EQ ---------------------------------------------------------
    // The reader is heap-allocated because it carries the transform tables
    // and a 2048-point frame, and only one panel in a rack is ever an EQ.
    std::unique_ptr<SpectrumReader> spectrum;
    // Five knobs, not twenty-six: three follow the selected band and two are
    // the global pair. The generic grid cannot do that, because the parameter
    // behind a control there is fixed when the control is made.
    juce::OwnedArray<juce::Slider> eqSliders;
    // The response only moves when a parameter does, so it is built once per
    // change and the frame path just strokes it.
    juce::Path eqCurve, eqBandCurve;
    juce::Rectangle<int> eqDisplay;
    int eqDragBand = -1;
    bool eqDragging = false;
};
}
