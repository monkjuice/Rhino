#pragma once
#include "DjControls.h"
#include "DjDeckDisplay.h"
#include "DjDeck.h"
#include <memory>

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
class DjDeckPanel final : public juce::Component,
                          public juce::FileDragAndDropTarget,
                          public juce::DragAndDropTarget
{
public:
    DjDeckPanel(Session&, int deck);
    int deckIndex() const { return deck; }
    void setDeck(int);
    // Everything the session announces: the deck's source, the keys' states.
    void sync();
    // At the view's timer rate: the keys that follow the audio thread, and
    // the blink of a start held for the beat.
    void tick(bool blinkPhase);
    // Once a display refresh: the screen and the platter.
    void tickDisplay();
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int x, int y) override;
    bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails&) override;
    void itemDropped(const juce::DragAndDropTarget::SourceDetails&) override;
    std::function<void(juce::String)> status;
    std::function<void()> selected;
    std::function<void(int track, te::EditItemID clip)> editRequested;
    std::function<void()> removeRequested;

    // Below this the keys squash; the view would rather clip a deck.
    static constexpr int minimumHeight = 240;

private:
    friend int runArrangementTest();
    void showSourceMenu();
    void chooseFile(const juce::File& startIn);
    void load(const juce::File&);
    void showBeatLoopMenu();
    void showJumpMenu();
    void showTempoRangeMenu();
    juce::Rectangle<int> headerArea() const;
    static void layoutRow(juce::Rectangle<int> row, const std::vector<std::pair<juce::Component*, int>>& cells);

    Session& session;
    int deck;
    DjDeckDisplay display;
    juce::TextButton source;
    DjPad live {"LIVE", palette::recordAccent}, edit {"EDIT", palette::activeNeutral}, reload {"RELOAD", palette::djWaiting};
    DjPad eject {"EJECT", palette::activeNeutral};
    std::array<std::unique_ptr<DjPad>, DjDeck::hotCueCount> hotCues;
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
    bool dropHighlight = false;
    juce::Time lastSelectTime;
    static constexpr int headerHeight = 24, padRowHeight = 22;
};
}
