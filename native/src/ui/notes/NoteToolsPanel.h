#pragma once
#include "StepGrid.h"

namespace rhino
{
// The column down the note editor's left, Live's Clip View panels in one
// strip: the clip's Loop switch, the scale whose rows the editor lights, and
// the Pitch and Time Utilities that act on the selected notes or, with none
// selected, on the whole clip - Humanize beside its amount, Reverse and
// Legato. The commands are the editor's (StepGrid::reverseNotes and the rest),
// because the selection is, so the panel holds no notes: only the scale it
// shows and how far Humanize moves a note. Both are view settings of this
// run, as the shell's scale chooser it replaced was. The Loop switch shows
// the clip's own state, which the model keeps.
class NoteToolsPanel final : public juce::Component, private juce::ChangeListener
{
public:
    NoteToolsPanel(Session&, StepGrid&);
    ~NoteToolsPanel() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    // As wide as a root, a scale and the two buttons on a row need.
    static constexpr int width = 168;

private:
    friend int runArrangementTest();
    // Live's clip Loop switch, the panel's largest control: a loop drawn
    // beside its name, lit while the clip loops.
    struct LoopSwitch final : juce::TextButton
    {
        void paintButton(juce::Graphics&, bool highlighted, bool down) override;
    };
    // Choosing a root or a scale lights it, since lighting it is all the
    // choice does; the Highlight switch turns it off and on again.
    void pushScale(bool lightIt);
    // Gives the keys back to the editor after a click here, so B, the arrows
    // and the clipboard keep working as they did before the click.
    void returnFocus();
    void sync();
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void visibilityChanged() override;

    Session& session;
    StepGrid& grid;
    // A change arrived while the shell had this pane switched off; caught up
    // when it is shown. See UiVisibility.h.
    bool staleWhileHidden = false;
    LoopSwitch loopButton;
    juce::TextButton highlightButton, humanizeButton, reverseButton, legatoButton;
    juce::ComboBox rootBox, scaleBox;
    juce::Slider humanizeAmount;
    juce::Rectangle<int> clipHeading, scaleHeading, notesHeading;
    static constexpr double defaultHumanizePercent = 25.0;
    static constexpr int margin = 8, gap = 4, headingHeight = 18, rowHeight = 20, rowGap = 4, loopHeight = 32;
};
}
