#pragma once
#include "DeviceEditorPanel.h"
#include <algorithm>
#include <utility>
#include <vector>

namespace rhino
{
class DrumRackWindow;

class DeviceRack final : public juce::Component,
                         public juce::DragAndDropTarget,
                         public juce::FileDragAndDropTarget,
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
    // Sound files dragged in from the desktop land on a Drum Rack's pads.
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void fileDragEnter(const juce::StringArray&, int x, int y) override;
    void fileDragMove(const juce::StringArray&, int x, int y) override;
    void fileDragExit(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int x, int y) override;
    std::function<void(juce::String)> status;
    // A device's preset was saved from its panel.
    std::function<void()> presetsChanged;
    // The Drum Rack's window hears what the main window hears: the shell's
    // shortcuts, and the typing keyboard, which listens on the window it is
    // handed. The shell wires both.
    std::function<bool(const juce::KeyPress&)> shortcut;
    std::function<void(juce::Component&)> listenForKeys;

private:
    friend void runPatternDeviceRackTest();
    friend void runDrumRackFaceTest();
    friend void runDrumRackWindowTest();
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
    // The Drum Rack in a slot of the selected track, in its own window. One
    // is open at a time: the rack it shows comes to the front again, and
    // another rack takes its place.
    void openDrumWindow(int pluginIndex);
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
    // The Drum Rack panel under a point in the rack, and the pad under it
    // there, or -1 between pads.
    DeviceEditorPanel* drumPanelAt(juce::Point<int> rackPosition, int& pad) const;
    bool chainHasDrumRack() const;
    // Lights the pad a sound would land on, and no other.
    void showDrumDropTarget(juce::Point<int> rackPosition);
    void clearDrumDropTargets();
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
    std::unique_ptr<DrumRackWindow> drumWindow;
    // A change arrived while the shell had this pane switched off; caught up
    // when it is shown. See UiVisibility.h.
    bool staleWhileHidden = false;
};
}
