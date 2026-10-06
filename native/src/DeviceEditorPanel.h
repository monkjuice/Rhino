#pragma once
#include "Session.h"
#include "SpectrumAnalyser.h"
#include <array>
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
    // A Drum Rack's face takes sounds dropped on its pads; the rack asks it
    // which pad a point falls on, and has it light the pad a drag would land
    // on (-1 for none).
    bool showsDrumRack() const { return face == Face::DrumRack; }
    int drumPadAt(juce::Point<int>) const;
    void showDrumDropTarget(int pad);
    int devicePluginIndex() const { return pluginSlot; }
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    std::function<void(juce::String)> status;
    std::function<void()> selected;
    // A preset was saved, so a list of them is out of date.
    std::function<void()> presetsChanged;

private:
    friend void runPatternDeviceRackTest();
    friend void runDrumRackFaceTest();
    enum class Face { Generic, Generated, Arp, AutoTune, Eq, Vocoder, DrumRack };
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
    // Sections of one tab group take turns in one place. Which one shows is
    // remembered against the device, not the panel, because the rack builds
    // its panels afresh whenever the chain or the track changes.
    juce::String selectedTab(const juce::String& tabGroup) const;
    void selectTab(const juce::String& tabGroup, const juce::String& section);
    bool onHiddenTab(int parameter) const;
    bool handleGeneratedClick(const juce::MouseEvent&);
    juce::String getTooltip() override;
    // What a device describes beside its controls (DeviceDisplay): a diagram
    // of blocks and a set of traces. It stands after the face's first group.
    // Laid out and drawn in DeviceEditorPanelDisplay.cpp.
    int displayWidth() const;
    void layoutDisplay(juce::Rectangle<int>, int titleHeight);
    void paintDisplay(juce::Graphics&);
    int displayBlockAt(juce::Point<float>) const;
    juce::String roleOfSection(const juce::String& section) const;
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
    // The Drum Rack's face is DeviceEditorPanelDrums.cpp: sixteen pads, each
    // with its mute, play and solo, beside the selected pad's sound and its
    // six knobs. The knobs follow whichever pad is selected, as the EQ's
    // follow its band, and everything else is drawn and hit-tested by
    // rectangle, so selecting a pad rebuilds nothing.
    static constexpr int drumFaceWidth = 624;
    void layoutDrums();
    void paintDrums(juce::Graphics&);
    // The pads are drawn again only when something on one of them changed: a
    // knob turning changes the selected pad's side and nothing else.
    void repaintDrums();
    void ensureDrumControls();
    void styleDrumControls();
    bool handleDrumMouseDown(const juce::MouseEvent&);
    juce::String drumTooltip(juce::Point<int>) const;
    void showDrumPadMenu(int pad);
    void showDrumChokeMenu(int pad);
    void chooseDrumSample(int pad);
    void askToRenameDrumPad(int pad);
    void askToSaveDrumPreset(int pad);
    // The name bar's menu on a Drum Rack: its kits to load, and Save kit.
    void showDrumKitMenu();
    void tickDrums();
    // Only the meter, the detected note and the target key repaint; the rest
    // of the face is static and stays out of the frame path.
    void timerCallback() override;
    void showParameterMenu(int index);
    int visibleParameterCount() const;
    // The name bar's right-click menu: this device's presets to load, and
    // Save preset. In DeviceEditorPanelPresets.cpp.
    void showPresetMenu();
    void askToSavePreset();
    void savePreset(const juce::File&);
    void askToSaveFile(const juce::String& noun, const juce::String& prompt, const juce::String& suggestedName,
                       const juce::File& folder, const juce::String& extension,
                       std::function<void(const juce::File&)> save);

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
        juce::String title;         // for a tab group, the section showing
        juce::Rectangle<int> area;
        // A tab group's name and its tabs, each a section and where it is.
        juce::String tabGroup;
        std::vector<std::pair<juce::String, juce::Rectangle<int>>> tabs;
    };
    std::vector<GeneratedSection> generatedSections;
    // The device's own engine id, which a tab choice is remembered under.
    juce::String deviceKey;
    DeviceDisplay display;
    juce::Rectangle<int> displayArea, diagramArea, traceArea, captionArea;
    std::vector<juce::Rectangle<float>> displayBlocks;
    std::vector<juce::Path> displayTraces;
    float displayBusY = 0.0f;
    int displaySelected = -1;   // the block whose section is showing
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

    // ---- Drum Rack --------------------------------------------------------
    // Six knobs, not ninety-six: they stand for the selected pad's controls.
    juce::OwnedArray<juce::Slider> drumSliders;
    // How often each pad had been struck when the face last looked, and how
    // brightly each is still lit from it.
    std::array<std::uint32_t, 16> drumStrikesSeen {};
    std::array<float, 16> drumFlash {};
    // The device those counts were read from: a face retargeted at another
    // rack starts from that rack's counts rather than flashing every pad.
    juce::String drumStrikesDevice;
    // Everything the pads were last drawn from, written as one string.
    juce::String drumPadsDrawn;
    int drumDropTarget = -1;
    std::unique_ptr<juce::FileChooser> drumFileChooser;
};
}
