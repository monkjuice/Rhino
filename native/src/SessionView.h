#pragma once
#include "Session.h"
#include <functional>

namespace rhino
{
// The clip launcher: tracks are columns, scenes are rows, and every cell is a
// clip slot. Cells are painted rather than built from child components so the
// grid costs one repaint per launch-state change rather than N components.
//
// Defined across SessionView.cpp, SessionViewPainter.cpp and
// SessionViewGestures.cpp.
class SessionView final : public juce::Component,
                          public juce::FileDragAndDropTarget,
                          public juce::DragAndDropTarget,
                          private juce::ChangeListener,
                          private te::SceneWatcher::Listener,
                          private juce::ScrollBar::Listener,
                          private Session::Listener
{
public:
    explicit SessionView(Session&);
    ~SessionView() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int x, int y) override;
    bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails&) override;
    void itemDragMove(const juce::DragAndDropTarget::SourceDetails&) override;
    void itemDragExit(const juce::DragAndDropTarget::SourceDetails&) override;
    void itemDropped(const juce::DragAndDropTarget::SourceDetails&) override;
    void selectTrack(int track);
    int selectedTrackIndex() const { return selectedTrack; }
    void setMixerVisible(bool);
    bool isMixerVisible() const { return mixerOpen; }
    std::function<void(juce::String)> status;
    std::function<void(int)> trackSelected;

private:
    friend int runArrangementTest();
    enum class Region { none, slot, sceneLaunch, trackStop, trackHeader };
    struct Hit
    {
        Region region = Region::none;
        int track = -1;
        int scene = -1;
        bool operator==(const Hit& other) const
        {
            return region == other.region && track == other.track && scene == other.scene;
        }
    };
    Hit hitTest(juce::Point<float>) const;
    juce::Rectangle<float> slotBounds(int track, int scene) const;
    juce::Rectangle<float> trackHeaderBounds(int track) const;
    juce::Rectangle<float> sceneLaunchBounds(int scene) const;
    juce::Rectangle<float> trackStopBounds(int track) const;
    juce::Rectangle<float> sceneColumnBounds() const;
    float columnX(int track) const;
    float rowY(int scene) const;
    float gridBottom() const;
    float stopRowTop() const;
    float sceneColumnX() const;
    void launchSlot(int track, int scene);
    void showSlotMenu(int track, int scene);
    void applyBrowserDrop(const juce::String& description, int track, int scene);
    void refreshControls();
    void syncTrackControls();
    void layOutTrackControls();
    void updateScroll();
    float mixerTop() const;
    float mixerHeight() const;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void slotUpdated(int audioTrackIndex, int slotIndex) override;
    void scrollBarMoved(juce::ScrollBar*, double) override;
    void editWillChange() override;
    void editDidChange() override;
    Session& session;
    juce::TextButton stopAllButton, addSceneButton, addTrackButton, mixerButton;
    juce::ComboBox quantiseBox;
    juce::ScrollBar sceneScrollBar {true};
    // The mixer strip under each column. These are the same values the
    // arrangement's track headers edit; neither view owns them.
    std::vector<std::unique_ptr<juce::TextButton>> mute, solo;
    std::vector<std::unique_ptr<juce::Slider>> volume, pan;
    juce::Slider mainVolume;
    juce::Label mainLabel;
    Hit hovered, dropTarget;
    double sceneScroll = 0.0, trackScroll = 0.0;
    int selectedTrack = 0;
    bool updatingQuantisation = false;
    bool mixerOpen = true;
    static constexpr float toolbarHeight = 30.0f, trackHeaderHeight = 32.0f;
    static constexpr float slotHeight = 26.0f, columnWidth = 116.0f;
    static constexpr float sceneColumnWidth = 128.0f, stopRowHeight = 24.0f;
    static constexpr float scrollBarWidth = 12.0f, mixerStripHeight = 74.0f;
};
}
