#pragma once
#include "Session.h"
#include "ArrangementGrid.h"
#include <map>
#include <memory>

namespace rhino
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
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int x, int y) override;
    bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails&) override;
    void itemDropped(const juce::DragAndDropTarget::SourceDetails&) override;
    void fit();
    void selectTrack(int track);
    int selectedTrackIndex() const { return selectedTrack; }
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
    };
    enum class LoopGesture { none, create, move, trimStart, trimEnd };
    // What the last click selected, and therefore what Delete acts on. A track
    // is always highlighted as the working row, so "a track is selected" cannot
    // be inferred from selectedTrack - it has to be recorded.
    enum class Focus { none, clip, track, automation, group };
    // Automation is edited by dragging a point, or by lifting a lane that has
    // never been drawn off its resting line, which is what makes it active.
    enum class AutomationGesture { none, movePoint, moveLine };
    // Lanes stack: every track owns one row, plus one row for each automation
    // the user has sent to its own lane, plus one header row for each group.
    // Ghost rows repeat the track's clips dimmed, so an automation stays
    // visually anchored to what it drives. A collapsed group gives its members
    // zero height rather than dropping their rows, so every track still has a
    // row to be addressed and drawn through.
    struct LaneRow
    {
        int track = 0;
        int automation = -1; // -1 is the track's own row
        int group = -1;      // >= 0 when the row is a group's header
        float top = 0.0f;
        float height = 0.0f;
    };
    struct AutomationHit
    {
        int row = -1;
        int automation = -1;
        int point = -1; // -1 is the line itself rather than one of its points
        bool valid() const { return row >= 0; }
    };
    void sync();
    void syncTrackControls();
    void configureMasterControls();
    void updateScroll();
    void zoom(double factor, double anchor);
    void cancelDrag();
    bool isSelected(te::EditItemID) const;
    void setSelection(std::vector<te::EditItemID>, te::EditItemID primary = {});
    void copySelection();
    void pasteSelection();
    void deleteSelection();
    void splitSelectedAtPlayhead();
    void duplicateSelected();
    void nudgeSelected(int direction, bool byBar);
    juce::Result applyBrowserDrop(const juce::String& description, int track, double startSeconds = 0.0, bool insertPreset = false);
    void updatePlayhead();
    void showGridMenu();
    void showClipMenu(te::EditItemID);
    void updateGridControl();
    GridDivision resolvedGridDivision() const;
    double resolvedGridBeats() const;
    int trackAt(float y) const;
    double snapUnitSeconds() const;
    float xFor(double seconds) const;
    double timeAt(float x) const;
    double snapped(double seconds, bool bypass) const;
    double snappedClipMoveStart(double desiredStart, double length, int targetTrack, bool bypass) const;
    LoopGesture loopGestureAt(juce::Point<float>) const;
    // ArrangementAutomation.cpp
    void buildRows();
    void layoutRows();
    juce::Rectangle<float> rowBounds(int row) const;
    int rowAt(float y) const;
    const Session::TrackAutomation* automationFor(const LaneRow&) const;
    juce::Rectangle<float> automationArea(int row) const;
    bool isFocusedAutomation(Session::DeviceTarget) const;
    float automationYFor(int row, const Session::TrackAutomation&, float value) const;
    float automationValueForY(int row, const Session::TrackAutomation&, float y) const;
    std::vector<Session::AutomationPoint> defaultAutomationPoints(const Session::TrackAutomation&) const;
    AutomationHit automationHitAt(juce::Point<float>) const;
    bool beginAutomationGesture(const juce::MouseEvent&);
    void dragAutomationGesture(const juce::MouseEvent&);
    void endAutomationGesture(const juce::MouseEvent&);
    void paintAutomationRow(juce::Graphics&, int row);
    void paintGhostRow(juce::Graphics&, int row);
    void showAutomationMenu(Session::DeviceTarget);
    // ArrangementGestures.cpp
    int cardResizeEdgeAt(juce::Point<float>) const;
    int cardAt(juce::Point<float>) const;
    bool beginCardGesture(const juce::MouseEvent&);
    void dragCardGesture(const juce::MouseEvent&);
    void endCardGesture();
    void showTrackMenu(int track);
    void renameTrack(int track);
    // ArrangementGroups.cpp
    const Session::TrackGroup* groupById(int groupId) const;
    const Session::TrackGroup* groupStartingAt(int track) const;
    const Session::TrackGroup* groupContaining(int track) const;
    bool isTrackHidden(int track) const;
    bool isTrackSelected(int track) const;
    void selectTrackRange(int from, int to);
    void toggleTrackSelection(int track);
    void selectGroup(int groupId);
    int groupRowAt(juce::Point<float>) const;
    juce::Rectangle<float> groupDisclosureBounds(juce::Rectangle<float> row) const;
    juce::Rectangle<float> groupButtonBounds(juce::Rectangle<float> row, int index) const;
    bool beginGroupGesture(const juce::MouseEvent&);
    void paintGroupRow(juce::Graphics&, int row);
    void paintGroupSpine(juce::Graphics&, int track, juce::Rectangle<float> row);
    void showGroupMenu(int groupId);
    void renameGroup(int groupId);
    void groupSelectedTracks();
    void ungroupSelection();
    juce::String controlDescription(juce::Component*) const;
    juce::Rectangle<float> lane(int track) const;
    // The master row is pinned under the scrolling lanes and never scrolls.
    juce::Rectangle<float> masterLane() const;
    bool isMasterSelected() const;
    float laneHeight() const;
    // A track keeps its own row height once its card has been dragged; until
    // then it follows the fitted height, and a resize in progress previews
    // ahead of the session so the drag does not rewrite the edit per pixel.
    float laneHeightFor(int track) const;
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
    std::vector<std::vector<Session::TrackAutomation>> trackLanes;
    std::vector<LaneRow> rows;
    std::vector<Session::TrackGroup> groups;
    std::vector<int> trackRowIndex;
    float rowsHeight = 0.0f;
    juce::TextButton duplicateButton, addTrack, snap, automationButton, gridControl;
    juce::ComboBox snapSize;
    // The header controls live in a container spanning the scrolling lane
    // viewport, so JUCE clips them at its edges: a row dragged past the bottom
    // slides under the pinned main row instead of being drawn over it, and one
    // scrolled off the top disappears under the ruler. The container itself is
    // transparent to the mouse, so a click on empty header space still reaches
    // the arrangement and selects the track.
    juce::Component laneHeaders;
    std::vector<std::unique_ptr<juce::TextButton>> mute, solo;
    // The same mixer values the session view shows, laid out horizontally.
    std::vector<std::unique_ptr<juce::Slider>> volume, pan;
    juce::Slider masterVolume, masterPan;
    juce::ScrollBar scroll {false}, trackScrollBar {true};
    juce::VBlankAttachment vblank;
    double viewStart = 0.0, viewSpan = 8.0, songEnd = 2.0, trackScroll = 0.0;
    GridSettings gridSettings;
    te::EditItemID selected;
    std::vector<te::EditItemID> selectedClips, clipboard;
    int selectedTrack = 0;
    // Grouping acts on several cards at once, so the working track is joined by
    // the set a shift-click has gathered. It always holds selectedTrack.
    std::vector<int> selectedTracks {0};
    int trackSelectionAnchor = 0, selectedGroup = -1;
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
    AutomationGesture automationGesture = AutomationGesture::none;
    Session::DeviceTarget automationTarget;
    Session::DeviceTarget focusedAutomation;
    Focus focus = Focus::none;
    int automationRow = -1, automationPoint = -1;
    std::vector<Session::AutomationPoint> automationPoints;
    float playhead = -1.0f;
    // Card gestures. The bottom edge resizes the row it belongs to; the body
    // of the card carries the track to a new place in the stack. Only one of
    // them can be running, and both preview before they touch the session.
    int resizingTrack = -1, movingTrack = -1, moveDestination = -1;
    float resizePreview = 0.0f, resizeAnchor = 0.0f, resizeStartHeight = 0.0f, moveAnchor = 0.0f;
    bool moveStarted = false;
    static constexpr float headerWidth = 228.0f, rulerTop = 32.0f, lanesTop = 56.0f, masterLaneHeight = 26.0f;
    static constexpr float automationRowHeight = 44.0f;
    // A group header is one line: its disclosure, its name and its two buttons.
    // Members carry a spine in the group colour down the left of their cards,
    // which is what reads as nesting without indenting the controls.
    static constexpr float groupRowHeight = 24.0f, groupSpineLeft = 3.0f, groupSpineWidth = 5.0f;
    // A card is name plus one control line at its shortest; the mixer line is
    // the next thing that fits, and past that a row only gets roomier.
    static constexpr float minimumLaneHeight = 32.0f, mixerLaneHeight = 54.0f, maximumLaneHeight = 260.0f;
    // A card is two columns: the controls, then the name on its colour. The
    // divider between them is the same grey the row separators use. Both lines
    // of controls share one left edge and one width, so the buttons sit
    // squarely over the faders.
    static constexpr float cardControlsWidth = 116.0f, cardDividerWidth = 4.0f;
    static constexpr int cardControlsTop = 7, cardControlLeft = 10, cardControlWidth = 48, cardControlGap = 4;
};
}
