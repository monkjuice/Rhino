#pragma once
#include "Session.h"
#include <algorithm>
#include <vector>

namespace theta
{
class DeviceRack final : public juce::Component,
                         public juce::DragAndDropTarget,
                         private juce::ChangeListener
{
public:
    explicit DeviceRack(Session&);
    ~DeviceRack() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void selectTrack(int track);
    bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails&) override;
    void itemDropped(const juce::DragAndDropTarget::SourceDetails&) override;
    std::function<void(juce::String)> status;

private:
    class FloatingDeviceWindow;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void openSelectedDevice();
    void showAddMenu();
    void selectDevice(int device);
    int selectedPluginIndex() const;
    void rebuildDeviceCards();
    void rebuildParameterControls();
    void sync();

    Session& session;
    int selectedTrack = 0, selectedDevice = 0;
    bool syncing = false;
    std::vector<Session::DeviceSlot> slots;
    std::vector<Session::DeviceParameter> parameters;
    juce::Label title, context, selectedDeviceLabel, outputLabel;
    juce::TextButton open {"Open"}, bypass {"Bypass"}, remove {"Delete"}, add {"+"};
    juce::Viewport chainViewport;
    juce::Component chainContent;
    juce::OwnedArray<juce::TextButton> deviceCards;
    juce::OwnedArray<juce::Label> signalArrows;
    juce::OwnedArray<juce::Label> parameterLabels, parameterValues;
    juce::OwnedArray<juce::Slider> parameterSliders;
    juce::OwnedArray<juce::TextButton> parameterAutomation;
    std::unique_ptr<FloatingDeviceWindow> floatingWindow;
};
}
