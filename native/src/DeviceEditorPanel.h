#pragma once
#include "Session.h"
#include "SpectrumAnalyser.h"
#include <memory>
#include <optional>
#include <vector>

namespace rhino
{
// A device panel dragged by its name bar says where it came from, so the rack
// can tell a chain reorder from a browser drop. The format lives beside the
// panel because the panel is what writes it; the rack is its only reader.
inline juce::String deviceChainDragDescription(int track, int pluginIndex)
{
    return "rhino-device:" + juce::String(track) + ":" + juce::String(pluginIndex);
}

inline std::optional<int> deviceChainDragSlot(const juce::String& description, int track)
{
    if (!description.startsWith("rhino-device:"))
        return std::nullopt;
    const auto body = description.fromFirstOccurrenceOf(":", false, false);
    if (body.upToFirstOccurrenceOf(":", false, false).getIntValue() != track)
        return std::nullopt;
    return body.fromFirstOccurrenceOf(":", false, false).getIntValue();
}

// Hosts Rhino's native device faces. Unknown and third-party devices retain a
// generic parameter surface; plug-in-owned editors remain available via Edit.
class DeviceEditorPanel final : public juce::Component,
                                public juce::SettableTooltipClient,
                                private juce::Timer
{
public:
    static constexpr int standardHeight = 176;
    // The name bar: what the panel is dragged by, and the one strip of it that
    // belongs to no face.
    static constexpr int headerHeight = 23;

    explicit DeviceEditorPanel(Session&);
    void setTarget(int track, const Session::DeviceSlot&, bool selected);
    // A lane is moving one of this face's knobs, as of the last setTarget.
    bool followsAutomation() const;
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
    friend void runPatternDeviceRackTest();
    enum class Face { Generic, Generated, Arp, AutoTune, Eq, Vocoder };
    void ensureControls();
    void styleControls();
    void layoutGeneric();
    // A native device without a face of its own gets one generated from what
    // its controls declare: grouped by section, a knob for a continuous
    // control, a chooser for a choice and a switch for a toggle. Its layout
    // and drawing live in DeviceEditorPanelGenerated.cpp.
    void ensureGeneratedControls();
    void styleGeneratedControls();
    void layoutGenerated();
    void paintGenerated(juce::Graphics&);
    int generatedWidth() const;
    int generatedParameterForComponent(const juce::Component*) const;
    bool isKnob(int parameter) const;
    juce::Colour faceAccent() const;
    // Sets a parameter as one undo step, the way a click on a chooser or a
    // switch does, and Reset to default.
    void writeParameter(int parameter, float value);
    // Rhino Arp uses the same automatable controls as the generic face, but
    // groups them around a diagram of the generated motion. Its layout and
    // drawing live in DeviceEditorPanelArp.cpp.
    void layoutArp();
    void paintArp(juce::Graphics&);
    void ensureArpControls();
    void styleArpControls();
    juce::ComboBox* arpChoiceFor(int parameter) const;
    int arpParameterForComponent(const juce::Component*) const;
    void setArpButtonParameter(int parameter, float value);
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
    // Set by a press on the name bar and spent by the first drag far enough to
    // mean it. A press anywhere else never arms it, so a knob is never a drag.
    bool headerPressed = false;
    Face face = Face::Generic;
    juce::String deviceName;
    std::vector<Session::DeviceParameter> parameters;
    juce::OwnedArray<juce::Label> parameterLabels, parameterValues;
    juce::OwnedArray<juce::Slider> parameterSliders;
    juce::OwnedArray<juce::TextButton> parameterAutomation;
    // Arp is deliberately not a generic knob grid: musical choices read as
    // choices, and Hold is one direct on/off action. These controls still
    // write the device's ordinary automatable parameters.
    juce::OwnedArray<juce::ComboBox> arpChoiceControls;
    std::vector<int> arpChoiceParameters;
    juce::TextButton arpHold {"Hold"};
    juce::TextButton arpRateBeats {"Beat"}, arpRateMilliseconds {"ms"};
    bool arpControlsCreated = false;
    juce::Label title;
    juce::TextButton power;
    juce::Rectangle<int> contentArea, visualArea;
    juce::String deviceId;

    // ---- Generated face -----------------------------------------------------
    // One chooser and one switch per parameter, index for index, each shown
    // only where the parameter is that kind of control.
    juce::OwnedArray<juce::ComboBox> generatedChoices;
    juce::OwnedArray<juce::TextButton> generatedToggles;
    struct GeneratedSection
    {
        juce::String title;
        juce::Rectangle<int> area;
    };
    std::vector<GeneratedSection> generatedSections;
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
