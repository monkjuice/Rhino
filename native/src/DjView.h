#pragma once
#include "DjDeckPanel.h"
#include "DjMixerPanel.h"
#include <memory>
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
// the session's loads for it.
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
    // The deck last clicked, for the Device View to follow: its track, or
    // -1 when it plays a file or nothing.
    int selectedDeck() const { return selected; }
    int selectedTrack() const;
    std::function<void(juce::String)> status;
    std::function<void(int track, te::EditItemID clip)> editRequested;
    std::function<void(int track)> deckSelected;

private:
    friend int runArrangementTest();
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void editWillChange() override {}
    void editDidChange() override;
    void timerCallback() override;
    void visibilityChanged() override;
    void sync();
    void rebuildDecks();
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
    bool updatingQuantise = false;
    bool blinkPhase = false;
    int blinkTicks = 0;
    // A change arrived while the shell had this switched off; caught up
    // when it is shown. See UiVisibility.h.
    bool staleWhileHidden = false;
    static constexpr int toolbarHeight = 30;
};
}
