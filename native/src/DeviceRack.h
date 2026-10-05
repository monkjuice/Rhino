#pragma once
#include "DeviceEditorPanel.h"
#include <algorithm>
#include <utility>
#include <vector>

namespace rhino
{
class DeviceRack final : public juce::Component,
                         public juce::DragAndDropTarget,
                         private juce::ChangeListener,
                         private juce::Timer,
                         private Session::Listener
{
public:
    // Where the chain starts below the rack's own header row, the scrollbar a
    // chain wider than the rack grows, and the margin under the chain.
    static constexpr int chainTop = 29, chainScrollBarThickness = 8, chainBottomMargin = 6;
    // The shortest rack that shows a whole device face, even with the chain's
    // scrollbar showing. The shell keeps the Device View at least this tall.
    static constexpr int minimumHeight = chainTop + DeviceEditorPanel::standardHeight
                                       + chainScrollBarThickness + chainBottomMargin;

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
    friend int runUiProfile();
    class FloatingDeviceWindow;
    class DropMarker;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void editWillChange() override;
    void editDidChange() override;
    void timerCallback() override;
    // The engine plays the lanes and tells nobody, so a knob a lane is moving
    // is read back here instead: only the faces with such a knob are
    // refreshed.
    void followAutomation();
    bool wasPlaying = false;
    void visibilityChanged() override;
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
    void refreshTouchedDevice();

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
    // A change arrived while the shell had this pane switched off; caught up
    // when it is shown. See UiVisibility.h.
    bool staleWhileHidden = false;
};
}
