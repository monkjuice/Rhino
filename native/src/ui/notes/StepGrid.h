#pragma once
#include "model/Session.h"
#include "VelocityLaneGeometry.h"
#include <array>
#include <bitset>
#include <vector>

namespace rhino
{
class StepGrid final : public juce::Component, public juce::TooltipClient, private juce::ChangeListener,
                       private juce::ScrollBar::Listener, private juce::Timer
{
public:
    explicit StepGrid(Session&);
    ~StepGrid() override;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void modifierKeysChanged(const juce::ModifierKeys&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void zoomIn();
    void zoomOut();
    // A span of the song, in seconds, shown in the open clip: the part of it
    // a sweep across the arrangement's lanes selected. It becomes the
    // editor's region, the notes it runs across are selected, as a marquee
    // over every pitch would select them, and the view is framed on it.
    void showTimeSpan(double startSeconds, double endSeconds);
    void setScaleHighlight(int selection);
    // Draw mode: the pointer becomes a pencil, so a press paints notes and a
    // drag paints a run of them. With it off the pointer selects, which is
    // what it does in the arrangement, and a note is put down by
    // double-clicking a cell. B toggles it, as it does in Live.
    void setDrawMode(bool shouldDraw);
    bool isDrawMode() const { return drawMode; }
    // The velocity lane under the rows, Live's velocity editor: a marker per
    // note, dragged to change its velocity. Shown unless switched off; a pane
    // too short for it folds it away until there is room again.
    void setVelocityLaneShown(bool shouldShow);
    bool isVelocityLaneShown() const { return velocityLaneShown; }
    // What the Info View says about the part of the editor under the pointer.
    juce::String getTooltip() override;
    void focusLost(juce::Component::FocusChangeType) override;
    void resized() override;
    // Where the editor says what a command did, or why it refused: the shell
    // points it at the readout, as it does the arrangement's.
    std::function<void(juce::String)> status;
private:
    friend int runArrangementTest();
    friend int runUiProfile();
    enum class Gesture { none, draw, move, resize, select, keyboard, pan,
                         velocityDrag, velocityDraw, velocitySelect, laneResize };
    // What the right button's menus offer, StepGridMenu.cpp. A chop into n
    // equal parts is chopCommand + n, and chopCommand alone chops on the grid.
    enum MenuCommand
    {
        cutCommand = 1, copyCommand, pasteCommand, duplicateCommand, deleteCommand, fillCommand,
        selectAllCommand, addNoteCommand, drawModeCommand, velocityLaneCommand, loopCommand,
        octaveUpCommand, semitoneUpCommand, semitoneDownCommand, octaveDownCommand,
        clearLoopCommand, doubleLoopCommand,
        chopCommand = 100
    };
    // The preview switch beside the loop button: the loop button's box with
    // headphones drawn on it, which StepGridPainter.cpp paints.
    struct PreviewButton final : juce::TextButton
    {
        void paintButton(juce::Graphics&, bool highlighted, bool down) override;
    };
    static constexpr bool isVelocityLaneGesture(Gesture g)
    {
        return g == Gesture::velocityDrag || g == Gesture::velocityDraw
            || g == Gesture::velocitySelect || g == Gesture::laneResize;
    }
    // The two that hold a note-velocity gesture open in the session.
    static constexpr bool holdsVelocityGesture(Gesture g)
    {
        return g == Gesture::velocityDrag || g == Gesture::velocityDraw;
    }
    // The visible pitch range is a zoom, not a constant: Session::pitches is
    // only where it starts. These bound how far the lanes may be squeezed, and
    // maxPitchRows is the stride the row-addressed caches below are built for.
    static constexpr int minPitchRows = 8, maxPitchRows = 48;
    static constexpr float minimumRowHeight = 7.0f;
    // The row height below which the keyboard stops naming octaves. The
    // velocity lane folds away rather than squeeze sixteen rows under it.
    static constexpr float octaveLabelRowHeight = 8.0f;
    struct CopiedNote { double step = 0.0; int pitch = 0; double length = 1.0; int velocity = 100; };
    // What a copy takes and a paste puts down: each note from the region's
    // start and from its lowest pitch, and how wide the region was, which is
    // what a paste occupies - so a rest at either end survives the round trip.
    struct NoteClipboard
    {
        std::vector<CopiedNote> notes;
        int basePitch = 0;
        double spanSteps = 0.0;
    };
    // The arrangement's time selection, in steps instead of seconds, which
    // copy, cut and duplicate act on. A click leaves it zero-length, and
    // selecting notes sets it to their span rounded out to whole steps, so a
    // bar of notes duplicates as a bar rather than as the distance between its
    // first and last note. Where a paste lands is insertStep's, below.
    struct StepSelection
    {
        double start = 0.0, end = 0.0;
        bool active = false;
        double length() const { return std::max(0.0, end - start); }
        bool isRange() const { return end > start + 1.0e-6; }
    };
    struct MovingNote { juce::ValueTree state; double step = 0.0; int pitch = 0; };
    struct VisibleNote
    {
        juce::ValueTree state;
        double start = 0.0, length = 1.0;
        int pitch = 0, row = 0, velocity = 100;
    };
    // Every note of the clip inside the step range, scrolled into the pitch
    // window or not: what the velocity lane draws, hit-tests and reads. A
    // note out of the window still sounds, and still has a velocity.
    struct LaneNote
    {
        juce::ValueTree state;
        double start = 0.0, length = 1.0;
        int pitch = 0, velocity = 100;
        bool inWindow = false;
        bool operator==(const LaneNote&) const = default;
    };
    juce::Rectangle<float> cell(int step, int row) const;
    // From the header to the scrollbar: the rows, and the lane under them.
    float timeAreaHeight() const;
    float timeAreaBottom() const;
    float rowAreaHeight() const;
    float rowHeight() const;
    int visiblePitchRows() const { return pitchRowCount; }
    int computePitchRowCount() const;
    bool isOverKeyboard(juce::Point<float>) const;
    void zoomPitchAt(double factor, float pointerY);
    void scrollPitchBy(int semitones);
    void paintKeyboard(juce::Graphics&);
    // What a row is called: its pad's sound on a drum clip, else its note.
    juce::String rowName(int pitch) const;
    // The ruler counts the clip in bars and beats of the song's time
    // signature; a beat is the signature's note value, so many steps of the
    // editor's resolution.
    int stepsPerBeat() const;
    int stepsPerBar() const;
    void paintRuler(juce::Graphics&, juce::Rectangle<float> dirty);
    // A click on a key: every note of that pitch in the clip, as in Live.
    // Shift adds them to the selection and Ctrl takes them out again when
    // they are all in it already.
    void selectPitch(int pitch, const juce::ModifierKeys&);
    // What a paint has to cover, as the arrangement has it: the clip, or with
    // cullRepaints off the whole panel, for a test to compare against.
    juce::Rectangle<float> repaintArea(const juce::Graphics&) const;
    bool cullRepaints = true;
    juce::Rectangle<float> footerBounds() const;
    float cellWidth() const;
    float gridRight() const;
    float gridWidth() const;
    double visibleStepSpan() const;
    void syncHorizontalScroll();
    void zoomAt(double factor, float pointerX);
    // A drag that reaches the edge of the grid pulls the view after it, so a
    // selection, a move or a stroke can run past what is on screen. Driven by
    // the gesture timer rather than by pointer movement, because a pointer
    // held still outside the panel sends no more drags.
    void autoScrollDrag();
    // Middle-button pan, the arrangement's: carries the steps and the pitch
    // rows so that whatever was under the pointer when the button went down
    // stays there.
    void panView(juce::Point<float> pointer);
    void moveDraggedNotesAt(juce::Point<float>);
    // Alt and a double-click: a note where the pointer is, off the grid.
    void addNoteOffTheGrid(double step, int pitch);
    // Pointer position to musical position and back. The marquee keeps its
    // anchor in these rather than in pixels: the view scrolls under a drag
    // that reaches the edge, and a pixel anchor would slide with it.
    double stepAtX(float x) const;
    float xForStep(double step) const;
    double pitchAtY(float y) const;
    float yForPitch(double pitch) const;
    int cellHit(juce::Point<float>) const;
    int hit(juce::Point<float>) const;
    int resizeHit(juce::Point<float>) const;
    juce::Rectangle<float> boundsFor(const VisibleNote&) const;
    const VisibleNote* noteForState(const juce::ValueTree&) const;
    std::vector<juce::ValueTree> selectedStates() const;
    bool isSelected(const juce::ValueTree&) const;
    void setSelectedStates(std::vector<juce::ValueTree>);
    // Ctrl+E, the chop: every selected note cut on the grid, and while Ctrl is
    // held the mouse moving up adds a part to each and down takes one away,
    // as Ctrl+Up and Ctrl+Down do. StepGridEditing.cpp.
    void finishSubdivision();
    bool beginSubdivision();
    bool adjustSubdivision(int delta);
    void chopByMouse(float y);
    bool beginVelocityAdjustment();
    bool adjustVelocity(int delta);
    void finishVelocityAdjustment();
    // The selected notes' shared velocity, 1-127; -1 when they differ and -2
    // when nothing is selected.
    int selectedVelocity() const;
    void apply(int index);
    void toggleSelection(int index);
    bool selectAllNotes();
    bool transposeSelection(int semitones);
    void clearSelection();
    void setStepSelection(double start, double end);
    void setStepInsertPoint(double step);
    void clearStepSelection();
    // What a command acts on: the dragged-out span when there is one, else the
    // span of the selected notes.
    StepSelection effectiveStepRegion() const;
    void setStepRegionFromSelection();
    void paintInsertLine(juce::Graphics&);
    bool copySelection();
    bool cutSelection();
    bool pasteSelection();
    bool duplicateSelection();
    bool deleteSelection();
    bool fillSelectionToClipEnd();
    bool captureRegion(const StepSelection&, NoteClipboard&) const;
    bool pasteNotes(const NoteClipboard&, double atStep, const juce::String& actionName);
    // Where a paste lands: on the insert line when there is one, else just
    // after the selected notes, never on top of them.
    double pasteStep() const;
    void setInsertStep(double step);
    // Left and Right: the selected notes along the clip, by a step or with Alt
    // by a sixteenth of one.
    bool nudgeSelection(double steps);
    // The menu's chop, in one go: on the grid with parts 0, else that many
    // equal parts a note.
    bool chopSelection(int parts);
    std::vector<Session::NoteChop> chopsOfSelection(std::vector<juce::ValueTree>& originals) const;
    void report(const juce::String&) const;
    juce::Result moveCurrentNotesBy(double stepDelta, int pitchDelta);
    juce::Result resizeCurrentNoteTo(int index);
    juce::Result resizeCurrentNoteTo(juce::Point<float>, bool freeLength);
    void updatePointer(juce::Point<float>, const juce::ModifierKeys&);
    // What mouseDrag does for the gesture under way; mouseDrag then names the
    // key the pointer or the note in hand is on.
    void dragGesture(const juce::MouseEvent&);
    // The key the keyboard names: the pitch of the note being carried or
    // resized, else of the row or the marker under the point; -1 for none.
    int pitchNamedAt(juce::Point<float>) const;
    void setHoveredPitch(int pitch);
    juce::Rectangle<int> keyHoverArea(int pitch) const;
    // The right button, StepGridMenu.cpp: what a right-click takes, the two
    // menus it can open, and what their items do.
    void openContextMenu(juce::Point<float>);
    juce::PopupMenu noteMenu() const;
    juce::PopupMenu gridMenu() const;
    juce::PopupMenu rulerMenu() const;
    void presentContextMenu(juce::PopupMenu);
    void performMenuCommand(int command);
    void beginMarquee(const juce::MouseEvent&);
    void updateMarqueeSelection();
    void toggleDrawMode();
    juce::Result loopEditedClip();
    double loopStepAt(float x, bool free) const;
    double timelineTimeForLoopStep(double step) const;
    float loopXForTimelineTime(double seconds) const;
    int pitchForIndex(int index) const;
    int indexForCell(int step, int pitch) const;
    int automaticLowestPitch() const;
    void rebuildVisibleNotes();
    float playheadXForTime(double seconds) const;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void visibilityChanged() override;
    void scrollBarMoved(juce::ScrollBar*, double) override;
    void timerCallback() override;
    void updatePlayhead();
    // The velocity lane, StepGridVelocity*.cpp.
    float velocityLaneHeight() const;
    float velocityLaneBlockHeight() const;
    juce::Rectangle<float> laneDividerBounds() const;
    juce::Rectangle<float> velocityLaneBounds() const;
    juce::Rectangle<float> velocityPlotBounds() const;
    bool isOverLaneBlock(juce::Point<float>) const;
    bool isOverLaneDivider(juce::Point<float>) const;
    float yForVelocity(float velocity) const;
    float velocityValueAtY(float y) const;
    const LaneNote* laneNoteFor(const juce::ValueTree&) const;
    juce::Rectangle<int> markerBounds(const LaneNote&) const;
    int velocityMarkerAt(juce::Point<float>) const;
    // The value and the note name the gutter shows in place of its scale while
    // a marker is hovered or dragged; both empty when the scale shows.
    std::pair<juce::String, juce::String> velocityGutterText() const;
    juce::String tooltipAt(juce::Point<float>) const;
    void setUpVelocityLane();
    void updateVelocityButton();
    void rebuildLaneNotes(const std::vector<Session::EditorNote>&, int rows);
    // Re-reads the velocities from the model during a velocity gesture, when
    // nothing announces the change and the change listener does not run.
    void refreshNoteVelocities();
    bool applyNoteVelocities(const std::vector<Session::NoteVelocity>&);
    void repaintVelocityLane();
    bool updateLanePointer(juce::Point<float>, const juce::ModifierKeys&);
    void updateHover(juce::Point<float>);
    void setHoveredNote(const juce::ValueTree&);
    bool velocityLaneMouseDown(const juce::MouseEvent&);
    void velocityLaneMouseDrag(const juce::MouseEvent&);
    void finishVelocityLaneGesture();
    void beginVelocityMarquee(const juce::MouseEvent&);
    void updateVelocityMarquee();
    void paintVelocityStrokeTo(juce::Point<float>, bool perNote);
    bool nudgeSelectedVelocities(int delta);
    void toggleSelectionOf(const juce::ValueTree&);
    void paintVelocityLane(juce::Graphics&, juce::Rectangle<float> dirty, float stepWidth, int firstStep, int lastStep);
    Session& session;
    // A change arrived while the shell had this pane switched off; caught up
    // when it is shown. See UiVisibility.h.
    bool staleWhileHidden = false;
    std::bitset<Session::steps * maxPitchRows> notes, visited, selectedNotes;
    std::array<float, Session::steps * maxPitchRows> noteLengths {}, noteStartOffsets {};
    std::vector<VisibleNote> visibleNotes;
    std::vector<juce::ValueTree> selectedNoteStates;
    NoteClipboard noteClipboard;
    StepSelection stepSelection;
    // Live's insert marker: where a paste lands, and the one line drawn for a
    // selection. A click in the lanes or on the ruler puts it down and a
    // marquee leaves it at the start of what it swept; a copy carries it to
    // the end of what was copied and a paste to the end of what was pasted,
    // so a paste never lands on its own source and pastes in a row lie end to
    // end. Kept apart from the region, which every rebuild reads again off the
    // selected notes. Negative when there is none.
    double insertStep = -1.0;
    // The clip the editor last showed. A line in one clip means nothing in
    // the next.
    te::EditItemID shownClip;
    // The beat and bar the ruler and the shading were last drawn for: a new
    // time signature moves every line and label without moving a note.
    int shownStepsPerBeat = 0, shownStepsPerBar = 0;
    std::vector<MovingNote> movingNotes;
    Gesture gesture = Gesture::none;
    bool adding = true, showingDrumLabels = false, noteMoved = false, manualPitchScroll = false, movingGroup = false, resizingFromLeft = false;
    // What the drum rows were last called. A Drum Rack names its rows after
    // its pads' sounds, which change without a single note changing.
    juce::StringArray drumRowNames;
    // The gesture clock repeats the drag while autoscrolling. Remember the
    // snapped edge so a stationary pointer does not rewrite the model at 60 Hz.
    bool hasResizeTarget = false;
    // Pointer events and the autoscroll clock must use the same snap mode.
    bool resizeFreely = false;
    bool drawMode = false;
    // A plain press inside a group of selected notes keeps the group, so the
    // drag carries all of it. If the press turns out never to have been a
    // drag, it was a click, and a click takes the one note under it - which is
    // what every list in the OS does, and can only be decided on release.
    bool collapseSelectionOnRelease = false;
    juce::ValueTree clickedNoteState;
    // Only a drag that has actually travelled may pull the view after it: a
    // press a few pixels from the right edge is a click, not a request to
    // scroll to the end of the clip.
    bool dragTravelled = false;
    int lastHit = -1;
    int moveGrabPitch = -1;
    int visibleStepCount = Session::defaultSteps;
    int lowestVisiblePitch = Session::lowestNote;
    double stepScroll = 0.0, stepZoom = 1.0, pitchZoom = 1.0;
    int pitchRowCount = Session::pitches;
    juce::Point<float> keyboardDragPosition {-1.0f, -1.0f};
    double keyboardScrollRemainder = 0.0;
    // The key a press on the keyboard landed on, which a click selects the
    // notes of and the preview sounds while the button is down.
    int keyboardPressPitch = -1;
    // A move snaps the grabbed note to the grid unless Alt is held. The pointer
    // events and the gesture clock must agree on it, as they do for a resize.
    bool moveFreely = false;
    // Where a pan began. The anchors are absolute, as the arrangement's are,
    // so a pan that runs into the end of the clip and back returns exactly.
    juce::Point<float> panAnchor;
    double panStartStep = 0.0;
    int panStartPitch = 0;
    double dragStartStep = -1.0;
    double resizingStartStep = 0.0, resizeTarget = 0.0;
    juce::ValueTree movingNoteState, resizingNoteState;
    bool subdivisionActive = false;
    // The parts the first chopped note is in, which the badge over it shows.
    int subdivisionCount = 0;
    juce::Rectangle<float> subdivisionSourceBounds;
    // The notes as they were when Ctrl+E went down, chopped again from these
    // at every new count; the pieces the last chop made, which the next one
    // replaces; and how many parts each note has gained or lost.
    std::vector<Session::NoteChop> chopSources;
    std::vector<juce::ValueTree> chopPieces;
    int chopDelta = 0;
    // Where the mouse was when it last changed the count, -1 until it moves.
    float chopMouseY = -1.0f;
    static constexpr float chopPixelsPerPart = 12.0f;
    bool velocityAdjustActive = false;
    std::vector<LaneNote> laneNotes;
    bool velocityLaneShown = true, dividerHot = false;
    float velocityLaneRequested = velocitylane::defaultHeight;
    // Where a divider drag began, and what the lane was then, so a release
    // with the lane dragged shut leaves the toggle bringing back its old size.
    float laneResizeStartY = 0.0f, laneResizeStartHeight = 0.0f, laneResizeStartRequest = 0.0f;
    juce::ValueTree hoveredNoteState;
    // The key lit and named on the keyboard, -1 for none, and how tall its
    // name stands on a row squeezed thinner than that.
    int hoveredPitch = -1;
    static constexpr float keyTagHeight = 13.0f;
    // Where the grid's menu was opened, for Add Note; -1 when it was opened
    // somewhere with no pitch, the velocity lane.
    int menuStep = -1, menuPitch = -1;
    // How a context menu reaches the screen. A scenario swaps it for one that
    // picks an item, so a right-click is tested without a menu window opening
    // on the desktop of whoever runs the suite.
    std::function<void(juce::PopupMenu, std::function<void(int)>)> presentMenu;
    // A marker drag's notes as they were at the press. Every step is measured
    // from these, never from the last step, so a group pushed into a rail
    // comes back to its own spacing.
    std::vector<Session::NoteVelocity> velocityOrigins;
    // A press arms once it has travelled; arming moves nothing, so a marker
    // never jumps by the distance the pointer had to cover first.
    bool velocityDragArmed = false;
    float velocityDragLastY = 0.0f;
    double velocityDragDelta = 0.0;
    // What a stroke in the lane may paint, fixed at its press and kept index
    // for index beside laneNotes (an invalid tree where a note is not one),
    // and where the stroke was last, in steps and velocity.
    std::vector<juce::ValueTree> velocityDrawTargets;
    double strokeLastStep = 0.0;
    float strokeLastValue = 0.0f;
    bool strokeFresh = false;
    // The lane marquee's anchor, in steps and velocity.
    double velocityAnchorStep = 0.0;
    float velocityAnchorValue = 0.0f;
    int scaleHighlight = 1;
    float verticalAutoScroll = 0.0f;
    juce::Point<float> dragPosition {-1.0f, -1.0f};
    // Where the marquee was started, in steps and in pitch rather than in
    // pixels, so the box keeps covering the same music while a drag at the
    // edge scrolls the view out from under it.
    double selectionAnchorStep = 0.0, selectionAnchorPitch = 0.0;
    juce::Rectangle<float> selectionBox;
    // What a marquee begun with Ctrl or Shift is adding to. A plain marquee
    // starts from nothing, so this is empty.
    std::vector<juce::ValueTree> selectionBase;
    float playhead = -1.0f;
    bool loopDragActive = false;
    double loopAnchorStep = 0.0, loopPreviewStartStep = 0.0, loopPreviewEndStep = 0.0;
    juce::TextButton loopButton, drawButton, velocityButton;
    PreviewButton previewButton;
    juce::ScrollBar horizontalScroll {false};
    juce::VBlankAttachment vblank;
    static constexpr float labelWidth = 54.0f, headerHeight = 26.0f, scrollHeight = 14.0f, footerHeight = 24.0f;
public:
    // Sixteen rows that keep their octave names, the velocity lane at its
    // default height and a scrollbar: the shell opens the editor this tall.
    static constexpr int openingHeight = static_cast<int>(headerHeight + Session::pitches * octaveLabelRowHeight
        + velocitylane::dividerHeight + velocitylane::defaultHeight + scrollHeight + footerHeight);
};
}
