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
    // The clip a command would act on, or an invalid id when the selection is
    // a track, a region or nothing. The shell reads it to decide which editor
    // the lower pane should be showing.
    te::EditItemID selectedClipID() const { return selected; }
    std::function<void(juce::String)> status;
    std::function<void(int)> trackSelected;
    // Clicking a card is what asks for that track's devices, and it is reported
    // separately from trackSelected because it fires even when the working
    // track did not move: clicking the card already selected is how the Device
    // View is asked for again once it has been closed.
    std::function<void(int)> trackFocused;
    // Selecting a clip says what is being worked on; double-clicking one says
    // to open it. The arrangement reports both and decides neither: which
    // editor a clip belongs in is the shell's business, not the timeline's.
    std::function<void(te::EditItemID)> clipSelected;
    std::function<void(te::EditItemID)> clipOpened;
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
    // A clip is two rows. The strip along its top is the clip itself: clicking
    // it selects, dragging it carries. Everything below belongs to the
    // timeline, so a press there puts the selection line down and a drag
    // sweeps out a region, exactly as over an empty lane. The left and right
    // edges trim at any height, because a trim is about the clip's own bounds
    // rather than about which of its two rows the pointer is in.
    enum class ClipZone { trimStart, trimEnd, header, body };
    // What the last click selected, and therefore what Delete acts on. A track
    // is always highlighted as the working row, so "a track is selected" cannot
    // be inferred from selectedTrack - it has to be recorded.
    enum class Focus { none, clip, track, automation, region };
    // A rectangle of the timeline: a span of time across a run of tracks. It is
    // what copy, cut, paste, duplicate and delete all act on, so the same rule
    // covers a dragged-out region and a clicked clip - selecting a clip sets
    // the region to that clip's span, which is what makes Ctrl+D put the copy
    // flush against its end. A click that never drags leaves a zero-length
    // region: an insert point, and nothing else, which is where a paste lands.
    struct TimeSelection
    {
        double start = 0.0, end = 0.0;
        int firstTrack = 0, lastTrack = 0;
        bool active = false;
        double length() const { return std::max(0.0, end - start); }
        bool isRange() const { return end > start + 1.0e-7; }
        bool covers(int track) const { return active && track >= firstTrack && track <= lastTrack; }
    };
    // Automation is edited by dragging a point, or by lifting a lane that has
    // never been drawn off its resting line, which is what makes it active.
    enum class AutomationGesture { none, movePoint, moveLine };
    // Lanes stack: every track owns one row, plus one row for each automation
    // the user has sent to its own lane. Ghost rows repeat the track's clips
    // dimmed, so an automation stays visually anchored to what it drives. A
    // collapsed group gives its members zero height rather than dropping their
    // rows, so every track still has a row to be addressed and drawn through.
    struct LaneRow
    {
        int track = 0;
        int automation = -1; // -1 is the track's own row
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
    void splitSelectedAtPlayhead();
    void openCreatedClip(te::EditItemID created);
    // Records that the last click landed on a track card, and reports it.
    void focusTrack();
    // ArrangementSelection.cpp
    void setTimeSelection(double start, double end, int firstTrack, int lastTrack);
    void setInsertPoint(double seconds, int track);
    void clearTimeSelection();
    // The line the region starts at is where playback begins, so putting it
    // down moves the transport onto it and tells the session where Stop should
    // return to.
    void moveTransportToSelectionStart();
    // The region a command acts on: the dragged-out one when there is one,
    // otherwise the span of the selected clips, so both read the same way.
    TimeSelection effectiveRegion() const;
    // Where the region is drawn. While a clip drag previews, the engine has
    // not moved anything yet, so the region follows the previewed geometry the
    // clips are being drawn at. Display only: the commands read the region the
    // edit is actually in.
    TimeSelection displayedTimeSelection() const;
    // The bounding rectangle of the selected clips, taken either from the edit
    // or from what is currently being previewed.
    TimeSelection regionOfSelectedClips(bool previewed) const;
    void selectRegionContents();
    void setRegionFromSelectedClips();
    bool beginRegionGesture(const juce::MouseEvent&);
    void dragRegionGesture(const juce::MouseEvent&);
    void endRegionGesture();
    void paintTimeSelection(juce::Graphics&);
    juce::String barPositionText(double seconds) const;
    void copySelection();
    void cutSelection();
    void pasteSelection();
    void deleteSelection();
    void duplicateSelected();
    void nudgeSelected(int direction, bool byBar);
    juce::Result applyBrowserDrop(const juce::String& description, int track, double startSeconds = 0.0, bool insertPreset = false);
    void updatePlayhead();
    // A take is drawn as it is played, so the band has to be repainted as it
    // grows. The playhead's own damage is two narrow strips at its old and new
    // positions and not the span between them, so it cannot be relied on to
    // fill the band in.
    void repaintRecordingBand();
    void showGridMenu();
    void showAddTrackMenu();
    void addTrackOfType(Session::TrackType);
    void showClipMenu(te::EditItemID);
    void updateGridControl();
    GridDivision resolvedGridDivision() const;
    double resolvedGridBeats() const;
    int trackAt(float y) const;
    double snapUnitSeconds() const;
    float xFor(double seconds) const;
    double timeAt(float x) const;
    double snapped(double seconds, bool bypass) const;
    // A pointer position lands inside a grid cell, and which edge of that cell
    // it takes depends on what it is for. The selection line takes the cell it
    // is inside, so clicking anywhere in a bar puts the line on the bar; a
    // region's edges grow outwards from the anchor, so a drag covers whole
    // cells rather than stopping halfway through the one it ended in.
    double snappedDown(double seconds, bool bypass) const;
    double snappedUp(double seconds, bool bypass) const;
    static float clipHeaderHeight(float boxHeight);
    juce::Rectangle<float> clipHeaderBounds(const ClipView&) const;
    ClipZone clipZoneAt(juce::Point<float>, const ClipView&) const;
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
    // ArrangementRename.cpp
    void configureNameEditor();
    juce::Rectangle<int> trackNameBounds(int track) const;
    bool isRenaming() const;
    void layoutNameEditor();
    void startRename(int track, const juce::String& current);
    void endRename(bool keep);
    void renameTrack(int track);
    // ArrangementGroups.cpp
    const Session::TrackGroup* groupById(int groupId) const;
    const Session::TrackGroup* groupForBus(int track) const;
    const Session::TrackGroup* groupContaining(int track) const;
    bool isTrackHidden(int track) const;
    float trackIndent(int track) const;
    bool isTrackSelected(int track) const;
    // The cached arm flag, so the painter never has to ask the session.
    bool isTrackArmed(int track) const;
    void selectTrackRange(int from, int to);
    void toggleTrackSelection(int track);
    juce::Rectangle<float> busDisclosureBounds(juce::Rectangle<float> row) const;
    bool beginGroupGesture(const juce::MouseEvent&);
    void paintGroupGutter(juce::Graphics&, int track, juce::Rectangle<float> row);
    void toggleGroupCollapsed(int groupId);
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
    // Read once per sync rather than per repaint: answering it costs the
    // engine's track list, and the painter asks for every visible row.
    std::vector<bool> armedTracks;
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
    // Renaming happens in place, on the line the name is painted on. The editor
    // is a child of laneHeaders so a row scrolled half out of view crops it.
    juce::TextEditor nameEditor;
    int renamingTrack = -1;
    // Mute, solo and the record dot share the card's button line. Arming is
    // last, as it is in Live's track header, so the two that shape playback
    // stay together on the left.
    std::vector<std::unique_ptr<juce::TextButton>> mute, solo, arm;
    // The same mixer values the session view shows, laid out horizontally.
    std::vector<std::unique_ptr<juce::Slider>> volume, pan;
    juce::Slider masterVolume, masterPan;
    juce::ScrollBar scroll {false}, trackScrollBar {true};
    juce::VBlankAttachment vblank;
    double viewStart = 0.0, viewSpan = 8.0, songEnd = 2.0, trackScroll = 0.0;
    GridSettings gridSettings;
    te::EditItemID selected;
    std::vector<te::EditItemID> selectedClips;
    // A copied rectangle rather than clip ids: a cut deletes its sources, and
    // so does an undo made between the copy and the paste.
    Session::ClipRegion clipboard;
    int selectedTrack = 0;
    // Grouping acts on several cards at once, so the working track is joined by
    // the set a shift-click has gathered. It always holds selectedTrack.
    std::vector<int> selectedTracks {0};
    int trackSelectionAnchor = 0;
    bool dragging = false;
    bool marqueeSelecting = false;
    juce::Rectangle<float> marqueeBounds;
    juce::Point<float> marqueeAnchor;
    TimeSelection timeSelection;
    bool regionSelecting = false;
    // The clip selection and the region are two views of one thing and each
    // keeps the other in step; this stops the two updates chasing each other.
    bool syncingSelection = false;
    // A region read off the selected clips belongs to those clips, so it has to
    // travel with them when they are moved, trimmed or nudged. One dragged out
    // over the lanes belongs to the timeline and stays where it was put.
    bool regionFollowsClips = false;
    // Unsnapped: which cell each edge of a dragged region takes depends on
    // which side of the anchor the pointer ended up, so the anchor has to be
    // re-snapped on every move rather than fixed when the press landed.
    double regionAnchorTime = 0.0;
    int regionAnchorTrack = 0;
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
    // How far the recording band has been painted, and the note revision it
    // was painted at, so an ordinary frame repaints only the sliver the band
    // has grown by. Negative means nothing is being recorded.
    float recordingPaintedTo = -1.0f;
    juce::int64 paintedNoteRevision = -1;
    // Card gestures. The bottom edge resizes the row it belongs to; the body
    // of the card carries the track to a new place in the stack. Only one of
    // them can be running, and both preview before they touch the session.
    int resizingTrack = -1, movingTrack = -1, moveDestination = -1;
    float resizePreview = 0.0f, resizeAnchor = 0.0f, resizeStartHeight = 0.0f, moveAnchor = 0.0f;
    bool moveStarted = false;
    static constexpr float headerWidth = 228.0f, rulerTop = 32.0f, lanesTop = 56.0f, masterLaneHeight = 26.0f;
    static constexpr float automationRowHeight = 44.0f;
    // A group's members are pushed right by groupIndent, and the gap that opens
    // up is filled with the bus's colour; the bus itself keeps that column for
    // its disclosure arrow. The step in the left edge is what tells a card
    // inside a group from one below it at a glance.
    static constexpr float groupIndent = 13.0f, groupSpineLeft = 3.0f;
    // A card is name plus one control line at its shortest; the mixer line is
    // the next thing that fits, and past that a row only gets roomier.
    static constexpr float minimumLaneHeight = 32.0f, mixerLaneHeight = 54.0f, maximumLaneHeight = 260.0f;
    // A card is two columns: the controls, then the name on its colour. The
    // divider between them is the same grey the row separators use. Both lines
    // of controls share one left edge and one width, so the buttons sit
    // squarely over the faders.
    static constexpr float cardControlsWidth = 124.0f, cardDividerWidth = 4.0f;
    // The buttons are narrower than the faders under them: three of them share
    // the line the two faders split, and a single letter needs far less room
    // than a level bar does.
    static constexpr int cardControlsTop = 7, cardControlLeft = 10, cardControlWidth = 52,
                         cardControlGap = 4, cardButtonWidth = 34;
};
}
