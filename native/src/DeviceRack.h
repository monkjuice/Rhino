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
    void itemDragEnter(const juce::DragAndDropTarget::SourceDetails&) override;
    void itemDragMove(const juce::DragAndDropTarget::SourceDetails&) override;
    void itemDragExit(const juce::DragAndDropTarget::SourceDetails&) override;
    void itemDropped(const juce::DragAndDropTarget::SourceDetails&) override;
    std::function<void(juce::String)> status;

private:
    friend void runPatternDeviceRackTest();
    class FloatingDeviceWindow;
    class DropMarker;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void openSelectedDevice();
    void showAddMenu();
    void selectDevice(int device);
    int selectedPluginIndex() const;
    void rebuildDevicePanels();
    // Which gap between panels a point in the rack falls in: 0 is in front of
    // the first device and devicePanels.size() is past the last.
    int dropGapFor(juce::Point<int> rackPosition) const;
    int devicePositionFor(int pluginIndex) const;
    void showDropMarker(int gap);
    void hideDropMarker();
    void sync();

    Session& session;
    int selectedTrack = 0, selectedDevice = 0;
    std::vector<Session::DeviceSlot> slots;
    juce::Label title, context, outputLabel;
    juce::TextButton open {"Open"}, remove {"Delete"}, add {"+"};
    juce::Viewport chainViewport;
    juce::Component chainContent;
    juce::OwnedArray<DeviceEditorPanel> devicePanels;
    std::unique_ptr<DropMarker> dropMarker;
    std::unique_ptr<FloatingDeviceWindow> floatingWindow;
};
}
