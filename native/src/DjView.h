#pragma once
#include "DjDeckPanel.h"
#include "DjMixerPanel.h"
#include <memory>
#include <utility>
#include <vector>

namespace rhino
{
// The DJ view: up to six decks either side of the mixer, the way two CDJs
// stand either side of a DJM. It is what the Arrange/DJ switch in the
// control bar opens, in the rectangle the arrangement otherwise fills.
//
// The booth plays whether or not the view is showing, so a hidden view
// marks itself stale rather than following every change, and catches up
// when it is shown (UiVisibility.h). Its displays move on the display's
// own refresh, its keys and meters on a timer, and the shell's timer polls
// the session's loads for it. A change the session announces reads each
// deck once and hands what it read to the deck's console and strip, and
// repaints only the controls whose value changed.
class DjView final : public juce::Component,
                     private juce::ChangeListener,
                     private juce::Timer,
                     private Session::Listener
{
public:
    explicit DjView(Session&);
    ~DjView() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    // The deck last pressed, which Space plays and the lower pane follows:
    // its index, and its track, or -1 when it plays a file or nothing.
    int selectedDeck() const { return selected; }
    int selectedTrack() const;
    // What Space does in this view: play or pause the focused deck.
    void togglePlayFocused();
    std::function<void(juce::String)> status;
    std::function<void(int track, te::EditItemID clip)> editRequested;
    // The focus moved to a deck, or its name bar was pressed: the index of
    // the deck now focused. A key pressed on the console already focused
    // says nothing here; a cell it plays is told below.
    std::function<void(int deck)> deckSelected;
    // The deck focused now plays another track, or another cell of its
    // track: the lower pane follows it, to the clip it plays, without
    // opening.
    std::function<void(int deck)> focusedSourceChanged;

private:
    friend int runArrangementTest();
    friend int runUiProfile();
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void editWillChange() override {}
    void editDidChange() override;
    void timerCallback() override;
    void visibilityChanged() override;
    // Once a display refresh: every console's screen and platter, from one
    // read of its deck, and the damage handed to the window before it
    // paints (Playhead.h).
    void frame();
    void sync();
    void rebuildDecks();
    void focusDeck(int index, bool followAgain);
    void markFocus();
    void addDeck();
    void removeDeck(int deck);

    Session& session;
    juce::TextButton addDeckButton, stopAllButton;
    juce::ComboBox quantiseBox;
    DjPad phaseLock {"LOCK", palette::midiEffect};
    std::vector<std::unique_ptr<DjDeckPanel>> decks;
    DjMixerPanel mixer;
    juce::VBlankAttachment vblank;
    int selected = 0;
    // What the focused deck played when the shell was last told: its track,
    // and the cell of it, or -1 for the track's song.
    std::pair<int, int> followedSource {-1, -1};
    // The mixer width the decks were last laid out around.
    int laidOutMixerWidth = -1;
    bool updatingQuantise = false;
    bool blinkPhase = false;
    int blinkTicks = 0;
    // A change arrived while the shell had this switched off; caught up
    // when it is shown. See UiVisibility.h.
    bool staleWhileHidden = false;
    // The toolbar's words and what an empty booth says, shaped where the
    // layout puts them.
    ShapedLine titleLine, quantiseLine, emptyLine;
    static constexpr int toolbarHeight = 30;
};
}
