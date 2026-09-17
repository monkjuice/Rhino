#pragma once
#include "DeviceEditorPanel.h"
#include <algorithm>
#include <utility>
#include <vector>

namespace rhino
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
    friend void runPatternDeviceRackTest();
    class FloatingDeviceWindow;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void openSelectedDevice();
    void showAddMenu();
    void selectDevice(int device);
    int selectedPluginIndex() const;
    void rebuildDevicePanels();
    void sync();

    Session& session;
    int selectedTrack = 0, selectedDevice = 0;
    std::vector<Session::DeviceSlot> slots;
    juce::Label title, context, outputLabel;
    juce::TextButton open {"Open"}, remove {"Delete"}, add {"+"};
    juce::Viewport chainViewport;
    juce::Component chainContent;
    juce::OwnedArray<DeviceEditorPanel> devicePanels;
    std::unique_ptr<FloatingDeviceWindow> floatingWindow;
};
}
