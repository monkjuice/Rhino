#pragma once
#include "DjControls.h"
#include "DjDeckDisplay.h"
#include "DjDeck.h"
#include <memory>

namespace rhino
{
// One deck's console: a CDJ laid flat. The name bar chooses what the deck
// plays; the screen below it; the eight hot cues; cue and play; the loop
// and beat-jump keys; sync, master and reverse; and the tempo fader with
// its range. A track deck carries three more keys: Live, which plays the
// track's instrument over the bounce from the keys; Edit, which opens the
// track's clip in the note editor; and a reload for a stale bounce.
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
    // Once a display refresh: the screen.
    void tickDisplay() { display.tick(); }
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

    static constexpr int minimumHeight = 150, preferredHeight = 230;

private:
    friend int runArrangementTest();
    void showSourceMenu();
    void chooseFile(const juce::File& startIn);
    void load(const juce::File&);
    void showBeatLoopMenu();
    void showJumpMenu();
    void showTempoRangeMenu();
    juce::Rectangle<int> headerArea() const;
    void layoutRow(juce::Rectangle<int> row, const std::vector<std::pair<juce::Component*, int>>& cells);

    Session& session;
    int deck;
    DjDeckDisplay display;
    juce::TextButton source;
    DjPad live {"LIVE", palette::recordAccent}, edit {"EDIT", palette::activeNeutral}, reload {"RELOAD", palette::djWaiting};
    DjPad eject {"EJECT", palette::activeNeutral};
    std::array<std::unique_ptr<DjPad>, DjDeck::hotCueCount> hotCues;
    DjPad cue {"CUE", palette::djCue}, play {"PLAY", palette::djPlay};
    DjPad loopIn {"IN", palette::djCue}, loopOut {"OUT", palette::djCue}, reloop {"RELOOP", palette::djCue};
    DjPad loopHalve {"1/2", palette::activeNeutral}, loopDouble {"2X", palette::activeNeutral};
    juce::TextButton beatLoop;
    DjPad jumpBack {"<", palette::activeNeutral}, jumpForward {">", palette::activeNeutral};
    juce::TextButton jumpSize;
    DjPad syncKey {"SYNC", palette::midiEffect}, master {"MASTER", palette::djCue}, reverse {"REV", palette::activeNeutral};
    DjFader tempo {false, palette::activeNeutral};
    juce::TextButton tempoRange;
    DjPad tempoReset {"RESET", palette::activeNeutral};
    std::unique_ptr<juce::FileChooser> chooser;
    Session::DjDeckInfo info;
    Session::DjDeckState state;
    double beatLoopBeats = 4.0;
    int jumpBeats = 4;
    bool dropHighlight = false;
    static constexpr int headerHeight = 24, rowHeight = 22, rowGap = 3;
};
}
