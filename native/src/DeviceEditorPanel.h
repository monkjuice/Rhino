#pragma once
#include "Session.h"
#include <vector>

namespace theta
{
// Hosts Theta's native device faces. Unknown and third-party devices retain a
// generic parameter surface; plug-in-owned editors remain available via Edit.
class DeviceEditorPanel final : public juce::Component
{
public:
    explicit DeviceEditorPanel(Session&);
    void setTarget(int track, const Session::DeviceSlot&, bool selected);
    int preferredWidth() const;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    std::function<void(juce::String)> status;
    std::function<void()> selected;

private:
    enum class Face { Generic, ThetaSpace };
    void ensureControls();
    void styleControls();
    void layoutGeneric();
    void layoutThetaSpace();
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
};
}
