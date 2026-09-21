#pragma once
#include "Session.h"
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
    std::function<void(juce::String)> status;
    std::function<void()> selected;

private:
    enum class Face { Generic, RhinoSpace, AutoTune };
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
};
}
