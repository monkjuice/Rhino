#pragma once
#include "Session.h"
#include "ArrangementGrid.h"
#include <map>
#include <memory>

namespace theta
{
class Arrangement final : public juce::Component,
                          public juce::FileDragAndDropTarget,
                          public juce::DragAndDropTarget,
                          private juce::ChangeListener,
                          private juce::ScrollBar::Listener,
                          private Session::Listener
{
public:
    explicit Arrangement(Session&);
    ~Arrangement() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int x, int y) override;
    bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails&) override;
    void itemDropped(const juce::DragAndDropTarget::SourceDetails&) override;
    void fit();
    std::function<void(juce::String)> status;
    std::function<void(int)> trackSelected;
private:
    friend int runArrangementTest();
    struct Waveform;
    struct MidiNoteView
    {
        double start = 0.0, end = 0.0;
        int pitch = 0;
    };
    struct ClipView
    {
        te::EditItemID id;
        juce::String name;
        ClipGeometry position;
        Waveform* waveform = nullptr;
        std::vector<MidiNoteView> midiNotes;
        double speed = 1.0;
        double sourceDuration = 0.0;
        int track = 0;
        juce::Colour colour;
        int clipPlugins = 0;
        std::vector<Session::ClipAutomation> automations;
    };
    enum class LoopGesture { none, create, move, trimStart, trimEnd };
    void sync();
    void syncTrackControls();
    void updateScroll();
    void zoom(double factor, double anchor);
    void cancelDrag();
    bool isSelected(te::EditItemID) const;
    void setSelection(std::vector<te::EditItemID>, te::EditItemID primary = {});
    void copySelection();
    void pasteSelection();
    void deleteSelection();
    void selectTrack(int track);
    void splitSelectedAtPlayhead();
    void duplicateSelected();
    void nudgeSelected(int direction, bool byBar);
    juce::Result applyBrowserDrop(const juce::String& description, int track, double startSeconds = 0.0, bool insertPreset = false);
    void updatePlayhead();
    void showGridMenu();
    void updateGridControl();
    GridDivision resolvedGridDivision() const;
    double resolvedGridBeats() const;
    int trackAt(float y) const;
    double snapUnitSeconds() const;
    float xFor(double seconds) const;
    double timeAt(float x) const;
    double snapped(double seconds, bool bypass) const;
    double snappedClipMoveStart(double desiredStart, double length, int targetTrack, bool bypass) const;
    juce::Rectangle<float> automationBounds(const ClipView&) const;
    float automationValueForY(const ClipView&, float y, Session::DeviceTarget) const;
    int activeAutomationIndex(const ClipView&) const;
    int displayedAutomationIndex(const ClipView&) const;
    int automationLaneAt(const ClipView&, juce::Point<float>) const;
    LoopGesture loopGestureAt(juce::Point<float>) const;
    juce::Rectangle<float> lane(int track) const;
    float laneHeight() const;
    float laneContentHeight() const;
    ClipGeometry displayedPosition(const ClipView&) const;
    int displayedTrack(const ClipView&) const;
    juce::Rectangle<float> bounds(const ClipView&) const;
    int hit(juce::Point<float>) const;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void scrollBarMoved(juce::ScrollBar*, double) override;
    void editWillChange() override;
    void editDidChange() override;
    Session& session;
    juce::AudioFormatManager formats;
    juce::AudioThumbnailCache thumbnailCache {32};
    std::map<juce::String, std::unique_ptr<Waveform>> waveforms;
    std::vector<ClipView> clips;
    juce::TextButton duplicateButton, addTrack, snap, automationButton, gridControl;
    juce::ComboBox snapSize;
    std::vector<std::unique_ptr<juce::TextButton>> mute, solo;
    juce::ScrollBar scroll {false}, trackScrollBar {true};
    juce::VBlankAttachment vblank;
    double viewStart = 0.0, viewSpan = 8.0, songEnd = 2.0, trackScroll = 0.0;
    GridSettings gridSettings;
    te::EditItemID selected;
    std::vector<te::EditItemID> selectedClips, clipboard;
    int selectedTrack = 0;
    bool dragging = false;
    bool marqueeSelecting = false;
    juce::Rectangle<float> marqueeBounds;
    juce::Point<float> marqueeAnchor;
    double pasteTime = 0.0;
    ClipGesture gesture = ClipGesture::move;
    ClipGeometry original, preview;
    int originalTrack = 0, previewTrack = 0;
    double dragTime = 0.0, sourceDuration = 0.0;
    LoopGesture loopGesture = LoopGesture::none;
    double loopAnchor = 0.0, loopOriginalStart = 0.0, loopOriginalEnd = 0.0, loopPreviewStart = 0.0, loopPreviewEnd = 0.0;
    bool automationDragging = false;
    Session::DeviceTarget automationTarget;
    te::EditItemID activeAutomationClip;
    Session::DeviceTarget activeAutomationTarget;
    double automationStartTime = 0.0, automationEndTime = 0.0;
    float automationStartValue = 0.0f, automationEndValue = 0.0f;
    float playhead = -1.0f;
    static constexpr float headerWidth = 148.0f, rulerTop = 32.0f, lanesTop = 56.0f;
};
}
