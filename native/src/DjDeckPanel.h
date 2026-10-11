#pragma once
#include "DjClipGrid.h"
#include "DjConsoleLayout.h"
#include "DjControls.h"
#include "DjDeckDisplay.h"
#include <memory>
#include <tuple>

namespace rhino
{
// One deck's console, laid out as a CDJ is: the name bar; the screen; the
// eight hot cues under it; then, left of the jog wheel, the loop keys, the
// beat loop, the beat jump, the cue search, the direction and quantize
// keys, and the big round CUE and PLAY; the jog wheel in the middle with
// its vinyl mode and brake under it; and to its right sync, master, the
// tempo range and the tempo fader with its reset. A track deck carries
// three more keys in its name bar: Live, which plays the track's
// instrument over the bounce from the keys; Edit, which opens the track's
// clip in the note editor; and a reload for a stale bounce.
//
// What the deck plays comes from the session's announcements (sync); what
// it is doing - the keys that light, the tempo, the brake - from the view's
// 30 Hz tick, so a key the booth changes shows without an announcement;
// where it is, from the display's refresh. Each control repaints itself
// when what it shows changes, and the console's own paint draws only what
// a repaint reaches. DjDeckPanel.cpp, DjDeckPanelLayout.cpp and
// DjDeckPanelMenus.cpp.
class DjDeckPanel final : public juce::Component,
                          public juce::FileDragAndDropTarget,
                          public juce::DragAndDropTarget
{
public:
    DjDeckPanel(Session&, int deck);
    // What the deck plays, as the session announced it: the source, the
    // keys that need material, the screen and the clips grid.
    void sync(const Session::DjDeckInfo&);
    // At the view's timer rate: the keys that follow the audio thread, and
    // the blink of a start held for the beat.
    void tick(const Session::DjDeckState&, bool blinkPhase);
    // Once a display refresh: the screen and the platter.
    void tickDisplay(const Session::DjDeckState&);
    void tickDisplay();
    // The focused deck wears a mark: Space and the lower pane answer to it.
    void setFocused(bool);
    bool isFocused() const { return focused; }
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int x, int y) override;
    bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails&) override;
    void itemDropped(const juce::DragAndDropTarget::SourceDetails&) override;
    std::function<void(juce::String)> status;
    // A press anywhere on the console: the view focuses its deck.
    std::function<void()> selected;
    // A press on the name bar, which also asks for the deck's track to be
    // followed again when the deck is focused already, as a second click on
    // a track card brings the Device View back.
    std::function<void()> nameBarPressed;
    std::function<void(int track, te::EditItemID clip)> editRequested;
    std::function<void()> removeRequested;

    // Below this the keys squash; the view would rather clip a deck.
    static constexpr int minimumHeight = 240;

private:
    friend int runArrangementTest();
    friend int runUiProfile();
    static constexpr int hotCueCount = static_cast<int>(std::tuple_size_v<decltype(Session::DjDeckState::hotCueSeconds)>);
    void showSourceMenu();
    void chooseFile(const juce::File& startIn);
    void load(const juce::File&);
    void showBeatLoopMenu();
    void showJumpMenu();
    void showTempoRangeMenu();
    juce::Rectangle<int> headerArea() const;
    // Whether a repaint reaches an area; everything does with culling off.
    bool reaches(const juce::Graphics&, juce::Rectangle<int> area) const;

    Session& session;
    int deck;
    DjDeckDisplay display;
    // The track's clips, beside the big keys; shown on a track deck only.
    DjClipGrid grid;
    juce::TextButton source;
    DjPad live {"LIVE", palette::recordAccent}, edit {"EDIT", palette::activeNeutral}, reload {"RELOAD", palette::djWaiting};
    DjPad eject {"EJECT", palette::activeNeutral};
    std::array<std::unique_ptr<DjPad>, hotCueCount> hotCues;
    DjPad cue {"CUE", palette::djCue}, play {"PLAY", palette::djPlay};
    DjPad loopIn {"IN", palette::djCue}, loopOut {"OUT", palette::djCue}, reloop {"RELOOP / EXIT", palette::djCue};
    DjPad loopHalve {"1/2", palette::activeNeutral}, loopDouble {"2X", palette::activeNeutral};
    DjPad beatLoop {"BEAT LOOP", palette::djCue};
    DjPad jumpBack {"<", palette::activeNeutral}, jumpForward {">", palette::activeNeutral};
    DjPad jumpSize {"JUMP", palette::activeNeutral};
    DjPad searchBack {"|<<", palette::activeNeutral}, searchForward {">>|", palette::activeNeutral};
    DjPad reverse {"REV", palette::activeNeutral}, quantise {"Q", palette::djCue};
    DjJogWheel jog;
    DjPad vinyl {"VINYL", palette::djCue};
    juce::Slider brake;
    DjPad syncKey {"SYNC", palette::midiEffect}, master {"MASTER", palette::djCue};
    DjPad tempoRange {"RANGE", palette::activeNeutral};
    DjFader tempo {true, palette::activeNeutral};
    DjPad tempoReset {"RESET", palette::activeNeutral};
    std::unique_ptr<juce::FileChooser> chooser;
    Session::DjDeckInfo info;
    Session::DjDeckState state;
    double beatLoopBeats = 4.0;
    int jumpBeats = 4;
    // The range the RANGE key shows, set only when it changes.
    int shownRange = -1;
    // Where a scrub began: a drag's steps land from there.
    double scrubFrom = 0.0;
    bool focused = false;
    // The phase the view's timer last gave, which a sync keeps.
    bool blinkPhase = false;
    juce::Time lastSelectTime;
    // The deck's number and the captions under and over the controls they
    // name, shaped where the layout puts them.
    ShapedLine numberLine, tempoCaption, brakeCaption;
    // Off for the check that a culled paint matches drawing everything.
    bool cullRepaints = true;
};
}
