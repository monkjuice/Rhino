#pragma once
#include "DeviceEditorPanel.h"
#include <functional>

namespace rhino
{
// The Drum Rack in a window of its own: the very face the rack shows, laid out
// stacked (DeviceEditorPanel::drumStackedHeight), the map and the sixteen pads
// across the top and the selected pad's sample editor the whole width below.
// The button in a Drum Rack's name bar opens it, and so does Edit; the rack
// owns it (DeviceRack::openDrumWindow).
//
// The window keeps its rack's id, never its place in a chain, and finds the
// rack again after every change, so a device added in front of it, a track
// moved above it or an undo leaves it on the same rack. When the rack leaves
// the edit -- deleted, or its document closed -- the window goes with it.
class DrumRackView final : public juce::Component,
                           public juce::DragAndDropTarget,
                           public juce::FileDragAndDropTarget,
                           private juce::Timer
{
public:
    DrumRackView(Session&, te::EditItemID rack);
    // Finds the rack again and shows it as it now stands; false once it has
    // left the edit.
    bool follow();
    // A knob being dragged: only that device's readings moved.
    void refreshTouched();
    // The engine moves automated knobs and tells nobody, so the window reads
    // the selected pad's back while the transport rolls, as the rack does
    // for its faces, whether or not the rack is showing.
    void followAutomation();
    te::EditItemID device() const { return rack; }
    // The rack and the track it is on, for the window's title bar.
    juce::String title() const;
    void paint(juce::Graphics&) override;
    void resized() override;
    // Sounds and kits from the browser, and sound files from the desktop,
    // land on the pads as they do in the rack.
    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDragEnter(const SourceDetails&) override;
    void itemDragMove(const SourceDetails&) override;
    void itemDragExit(const SourceDetails&) override;
    void itemDropped(const SourceDetails&) override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void fileDragEnter(const juce::StringArray&, int x, int y) override;
    void fileDragMove(const juce::StringArray&, int x, int y) override;
    void fileDragExit(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int x, int y) override;
    std::function<void(juce::String)> status;
    // A kit was saved from the name bar, so the browser's list is out of date.
    std::function<void()> presetsChanged;

private:
    friend void runDrumRackWindowTest();
    void timerCallback() override;
    // The pad under a point of this view, or -1 between pads.
    int padAt(juce::Point<int>) const;
    Session& session;
    te::EditItemID rack;
    int track = -1;
    bool wasPlaying = false;
    Session::DeviceSlot shown;
    juce::String trackName;
    DeviceEditorPanel face;
};

class DrumRackWindow final : public juce::DocumentWindow
{
public:
    // Small enough to sit beside the main window, and still tall enough that
    // the face stands stacked.
    static constexpr int minimumWidth = 720, minimumHeight = DeviceEditorPanel::drumStackedHeight + 20;

    DrumRackWindow(Session&, te::EditItemID rack);
    DrumRackView& view();
    // Maximised on the display a component is on. Restored, it is most of
    // that display, centred.
    void show(juce::Component& over);
    // Closing hides it and asks the owner to destroy it.
    void closeButtonPressed() override;
    // The shell's shortcuts answer here as they do in the main window: Space
    // plays, Ctrl+Z undoes, Ctrl+S saves.
    bool keyPressed(const juce::KeyPress&) override;
    std::function<void()> onClose;
    std::function<bool(const juce::KeyPress&)> shortcut;
};
}
