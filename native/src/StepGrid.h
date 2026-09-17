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
    void scrollDraggedNotes();
    void moveDraggedNotesAt(juce::Point<float>);
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
    bool canPasteAt(int step) const;
    void clearSelection();
    bool copySelection();
    bool pasteSelection();
    bool deleteSelection();
    bool fillSelectionToClipEnd();
    juce::Result moveCurrentNotesBy(double stepDelta, int pitchDelta);
    juce::Result resizeCurrentNoteTo(int index);
    juce::Result resizeCurrentNoteTo(juce::Point<float>, bool freeLength);
    void updatePointer(juce::Point<float>, const juce::ModifierKeys&);
    void updateMarqueeSelection();
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
    std::vector<MovingNote> movingNotes;
    Gesture gesture = Gesture::none;
    bool adding = true, showingDrumLabels = false, noteMoved = false, manualPitchScroll = false, movingGroup = false, resizingFromLeft = false;
    int lastHit = -1, pasteAnchorIndex = -1, clipboardBasePitch = 0;
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
    juce::Point<float> selectionAnchor {-1.0f, -1.0f};
    juce::Rectangle<float> selectionBox;
    float playhead = -1.0f;
    bool loopDragActive = false;
    double loopAnchorStep = 0.0, loopPreviewStartStep = 0.0, loopPreviewEndStep = 0.0;
    juce::TextButton loopButton;
    juce::ScrollBar horizontalScroll {false};
    juce::VBlankAttachment vblank;
    static constexpr float labelWidth = 54.0f, headerHeight = 26.0f, scrollHeight = 14.0f, footerHeight = 24.0f;
};
}
