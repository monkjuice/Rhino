#pragma once
#include "Session.h"
#include <array>
#include <bitset>
#include <vector>

namespace rhino
{
class StepGrid final : public juce::Component, private juce::ChangeListener, private juce::ScrollBar::Listener, private juce::Timer
{
public:
    explicit StepGrid(Session&);
    ~StepGrid() override;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void zoomIn();
    void zoomOut();
    void setScaleHighlight(int selection);
    // Draw mode: the pointer becomes a pencil, so a press paints notes and a
    // drag paints a run of them. With it off the pointer selects, which is
    // what it does in the arrangement, and a note is put down by
    // double-clicking a cell. B toggles it, as it does in Live.
    void setDrawMode(bool shouldDraw);
    bool isDrawMode() const { return drawMode; }
    void focusGained(juce::Component::FocusChangeType) override;
    void focusLost(juce::Component::FocusChangeType) override;
    void resized() override;
private:
    friend int runArrangementTest();
    enum class Gesture { none, draw, move, resize, select, keyboard };
    // The visible pitch range is a zoom, not a constant: Session::pitches is
    // only where it starts. These bound how far the lanes may be squeezed, and
    // maxPitchRows is the stride the row-addressed caches below are built for.
    static constexpr int minPitchRows = 8, maxPitchRows = 48;
    static constexpr float minimumRowHeight = 7.0f;
    struct CopiedNote { double step = 0.0; int pitch = 0; double length = 1.0; int velocity = 100; };
    // The arrangement's time selection, in steps instead of seconds, and it
    // drives the same four commands. A click leaves it zero-length - an insert
    // point, which is where a paste lands - and selecting notes sets it to
    // their span rounded out to whole steps, so a bar of notes duplicates as a
    // bar rather than as the distance between its first and last note.
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
    juce::Rectangle<float> cell(int step, int row) const;
    float rowAreaHeight() const;
    float rowHeight() const;
    int visiblePitchRows() const { return pitchRowCount; }
    int computePitchRowCount() const;
    bool isOverKeyboard(juce::Point<float>) const;
    void zoomPitchAt(double factor, float pointerY);
    void scrollPitchBy(int semitones);
    void paintKeyboard(juce::Graphics&);
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
    void moveDraggedNotesAt(juce::Point<float>);
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
    void finishSubdivision();
    bool beginSubdivision();
    bool adjustSubdivision(int delta);
    bool beginVelocityAdjustment();
    bool adjustVelocity(int delta);
    void finishVelocityAdjustment();
    int selectedVelocityPercent() const;
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
    void paintStepSelection(juce::Graphics&);
    bool copySelection();
    bool cutSelection();
    bool pasteSelection();
    bool duplicateSelection();
    bool deleteSelection();
    bool fillSelectionToClipEnd();
    juce::Result moveCurrentNotesBy(double stepDelta, int pitchDelta);
    juce::Result resizeCurrentNoteTo(int index);
    juce::Result resizeCurrentNoteTo(juce::Point<float>, bool freeLength);
    void updatePointer(juce::Point<float>, const juce::ModifierKeys&);
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
    void scrollBarMoved(juce::ScrollBar*, double) override;
    void timerCallback() override;
    void updatePlayhead();
    Session& session;
    std::bitset<Session::steps * maxPitchRows> notes, visited, selectedNotes;
    std::array<float, Session::steps * maxPitchRows> noteLengths {}, noteStartOffsets {};
    std::vector<VisibleNote> visibleNotes;
    std::vector<juce::ValueTree> selectedNoteStates;
    std::vector<CopiedNote> noteClipboard;
    StepSelection stepSelection;
    // A region read off the selected notes is not drawn: the notes already
    // show what is selected, and a band across every pitch row on top of them
    // reads as a marquee that is still being dragged. Only a span the user
    // asked for by itself - a marquee, a click, a paste - is worth drawing.
    bool regionFromNotes = false;
    std::vector<MovingNote> movingNotes;
    Gesture gesture = Gesture::none;
    bool adding = true, showingDrumLabels = false, noteMoved = false, manualPitchScroll = false, movingGroup = false, resizingFromLeft = false;
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
    int lastHit = -1, clipboardBasePitch = 0;
    // How wide the copied region was, which is what a paste occupies and what
    // a duplicate steps forward by.
    double clipboardSpanSteps = 0.0;
    int moveGrabPitch = -1;
    int visibleStepCount = Session::defaultSteps;
    int lowestVisiblePitch = Session::lowestNote;
    double stepScroll = 0.0, stepZoom = 1.0, pitchZoom = 1.0;
    int pitchRowCount = Session::pitches;
    juce::Point<float> keyboardDragPosition {-1.0f, -1.0f};
    double keyboardScrollRemainder = 0.0;
    double dragStartStep = -1.0;
    double resizingStartStep = 0.0;
    juce::ValueTree movingNoteState, resizingNoteState;
    bool subdivisionActive = false;
    int subdivisionCount = 0;
    juce::Rectangle<float> subdivisionSourceBounds;
    bool velocityAdjustActive = false;
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
    juce::TextButton loopButton, drawButton;
    juce::ScrollBar horizontalScroll {false};
    juce::VBlankAttachment vblank;
    static constexpr float labelWidth = 54.0f, headerHeight = 26.0f, scrollHeight = 14.0f, footerHeight = 24.0f;
};
}
