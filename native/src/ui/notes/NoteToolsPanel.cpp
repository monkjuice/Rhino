#include "NoteToolsPanel.h"
#include "ui/shared/Theme.h"
#include "ui/shared/UiVisibility.h"

namespace rhino
{
namespace
{
// A command, pressed once: the editor's own button face, its words in full
// brightness so it reads as something to press rather than a state.
void styleAction(juce::TextButton& button, const juce::String& text, const juce::String& tooltip)
{
    button.setButtonText(text);
    button.setTooltip(tooltip);
    button.setWantsKeyboardFocus(false);
    button.setColour(juce::TextButton::buttonColourId, palette::noteEditorButton);
    button.setColour(juce::TextButton::textColourOffId, palette::text);
}
}

NoteToolsPanel::NoteToolsPanel(Session& s, StepGrid& g) : session(s), grid(g)
{
    setOpaque(true);
    setWantsKeyboardFocus(false);

    highlightButton.setButtonText("Highlight");
    highlightButton.setTooltip("Highlight Scale - light the rows of the scale's notes in the note editor, so the "
                               "notes that belong to it stand out. Choosing a root or a scale turns it on.");
    highlightButton.setClickingTogglesState(true);
    highlightButton.setWantsKeyboardFocus(false);
    highlightButton.setColour(juce::TextButton::buttonColourId, palette::noteEditorButton);
    highlightButton.setColour(juce::TextButton::buttonOnColourId, palette::noteEditorButtonOn);
    highlightButton.onClick = [this]
    {
        pushScale(highlightButton.getToggleState());
        returnFocus();
    };

    for (int pitchClass = 0; pitchClass < 12; ++pitchClass)
        rootBox.addItem(pitchClassName(pitchClass), pitchClass + 1);
    rootBox.setSelectedId(1, juce::dontSendNotification);
    rootBox.setTooltip("The scale's root note.");
    // Chromatic is left out: it would light every row, which is no highlight.
    for (int scale = 0; scale < musicalScaleCount; ++scale)
        if (static_cast<MusicalScale>(scale) != MusicalScale::Chromatic)
            scaleBox.addItem(musicalScaleName(static_cast<MusicalScale>(scale)), scale + 1);
    scaleBox.setSelectedId(static_cast<int>(MusicalScale::Major) + 1, juce::dontSendNotification);
    scaleBox.setTooltip("The scale whose rows the note editor lights.");
    for (auto* box : {&rootBox, &scaleBox})
    {
        box->setWantsKeyboardFocus(false);
        box->onChange = [this]
        {
            pushScale(true);
            returnFocus();
        };
    }

    humanizeAmount.setSliderStyle(juce::Slider::LinearBar);
    humanizeAmount.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    humanizeAmount.setRange(0.0, 100.0, 1.0);
    humanizeAmount.setValue(defaultHumanizePercent, juce::dontSendNotification);
    humanizeAmount.setDoubleClickReturnValue(true, defaultHumanizePercent);
    humanizeAmount.textFromValueFunction = [](double value) { return juce::String(juce::roundToInt(value)) + "%"; };
    humanizeAmount.setTooltip("How far Humanize may move a note's start: at 100%, a quarter of a step either way. "
                              "Double-click for 25%.");
    humanizeAmount.setWantsKeyboardFocus(false);
    humanizeAmount.setColour(juce::Slider::trackColourId, palette::noteEditorButtonOn);
    humanizeAmount.setColour(juce::Slider::backgroundColourId, palette::displayInset);
    humanizeAmount.setColour(juce::Slider::textBoxOutlineColourId, palette::border);

    styleAction(humanizeButton, "Humanize",
                "Humanize - move the start of each selected note, or of every note in the clip when none is "
                "selected, by a random amount up to the one beside it.");
    humanizeButton.onClick = [this]
    {
        grid.humanizeNotes(humanizeAmount.getValue() / 100.0);
        returnFocus();
    };
    styleAction(reverseButton, "Reverse",
                "Reverse - flip the selected notes in time, or the whole clip when none is selected, so the "
                "last note comes first.");
    reverseButton.onClick = [this]
    {
        grid.reverseNotes();
        returnFocus();
    };
    styleAction(legatoButton, "Legato",
                "Legato - lengthen or shorten each selected note, or every note when none is selected, so it "
                "reaches the start of the next one; the last reaches the end of the clip.");
    legatoButton.onClick = [this]
    {
        grid.legatoNotes();
        returnFocus();
    };

    for (auto* component : std::initializer_list<juce::Component*>{&highlightButton, &rootBox, &scaleBox,
                                                                   &humanizeAmount, &humanizeButton,
                                                                   &reverseButton, &legatoButton})
        addAndMakeVisible(component);
    session.addChangeListener(this);
    sync();
}

NoteToolsPanel::~NoteToolsPanel()
{
    session.removeChangeListener(this);
}

void NoteToolsPanel::pushScale(bool lightIt)
{
    highlightButton.setToggleState(lightIt, juce::dontSendNotification);
    grid.setScaleHighlight(lightIt, rootBox.getSelectedId() - 1, static_cast<MusicalScale>(scaleBox.getSelectedId() - 1));
}

void NoteToolsPanel::returnFocus()
{
    if (grid.isShowing())
        grid.grabKeyboardFocus();
}

// A drum clip's rows are pads, which the editor never lights, and with no MIDI
// clip open there are no notes for a command to reach.
void NoteToolsPanel::sync()
{
    const auto clip = session.hasPatternClip();
    const auto piano = clip && !session.isPatternDrums();
    for (auto* component : std::initializer_list<juce::Component*>{&highlightButton, &rootBox, &scaleBox})
        component->setEnabled(piano);
    for (auto* component : std::initializer_list<juce::Component*>{&humanizeAmount, &humanizeButton,
                                                                   &reverseButton, &legatoButton})
        component->setEnabled(clip);
}

void NoteToolsPanel::changeListenerCallback(juce::ChangeBroadcaster*)
{
    if (isHiddenInShell(*this))
    {
        staleWhileHidden = true;
        return;
    }
    sync();
}

void NoteToolsPanel::visibilityChanged()
{
    if (staleWhileHidden && !isHiddenInShell(*this))
    {
        staleWhileHidden = false;
        sync();
    }
}

void NoteToolsPanel::paint(juce::Graphics& g)
{
    g.fillAll(palette::sideSurface);
    // A rule down the right edge parts the panel from the editor's keys,
    // which stand on the same ground.
    g.setColour(palette::border);
    g.fillRect(getWidth() - 1, 0, 1, getHeight());
    g.setColour(palette::textDim);
    g.setFont(uiFontBold(9.0f));
    drawSnappedText(g, "SCALE", scaleHeading);
    drawSnappedText(g, "NOTES", notesHeading);
}

void NoteToolsPanel::resized()
{
    auto area = getLocalBounds().withTrimmedRight(1).reduced(margin, 0);
    const auto half = (area.getWidth() - gap) / 2;
    const auto row = [&area]
    {
        auto next = area.removeFromTop(rowHeight);
        area.removeFromTop(rowGap);
        return next;
    };
    // The first heading in the band the editor's ruler takes, so the two read
    // as one line across the pane.
    area.removeFromTop(4);
    scaleHeading = area.removeFromTop(headingHeight);
    auto choosers = row();
    rootBox.setBounds(choosers.removeFromLeft(52));
    choosers.removeFromLeft(gap);
    scaleBox.setBounds(choosers);
    highlightButton.setBounds(row());
    area.removeFromTop(6);
    notesHeading = area.removeFromTop(headingHeight);
    auto humanize = row();
    humanizeAmount.setBounds(humanize.removeFromLeft(half));
    humanize.removeFromLeft(gap);
    humanizeButton.setBounds(humanize);
    auto order = row();
    reverseButton.setBounds(order.removeFromLeft(half));
    order.removeFromLeft(gap);
    legatoButton.setBounds(order);
}
}
