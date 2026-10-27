#include "NoteToolsPanel.h"
#include "ui/shared/Theme.h"
#include "ui/shared/UiVisibility.h"
#include <cmath>

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

    // Its state is the clip's, which sync() reads, so a click asks the model
    // and the change it announces sets the light.
    loopButton.setButtonText("Loop");
    loopButton.setTooltip("Loop the clip - its notes repeat for as long as the clip is dragged out in the "
                          "arrangement, and the editor shows the pattern that repeats. The button over the keys "
                          "loops the song's playback instead.");
    loopButton.setWantsKeyboardFocus(false);
    loopButton.setColour(juce::TextButton::buttonColourId, palette::noteEditorButton);
    loopButton.setColour(juce::TextButton::buttonOnColourId, palette::noteEditorButtonOn);
    loopButton.onClick = [this]
    {
        grid.toggleClipLoop();
        returnFocus();
    };

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

    for (auto* component : std::initializer_list<juce::Component*>{&loopButton, &highlightButton, &rootBox, &scaleBox,
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
    for (auto* component : std::initializer_list<juce::Component*>{&loopButton, &humanizeAmount, &humanizeButton,
                                                                   &reverseButton, &legatoButton})
        component->setEnabled(clip);
    loopButton.setToggleState(session.isPatternLooping(), juce::dontSendNotification);
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
    drawSnappedText(g, "CLIP", clipHeading);
    drawSnappedText(g, "SCALE", scaleHeading);
    drawSnappedText(g, "NOTES", notesHeading);
}

// The editor's own button face, with a loop drawn left of the word, centred
// together: a circle open just right of the top, closed by an arrowhead that
// points the way round it goes.
void NoteToolsPanel::LoopSwitch::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const auto on = getToggleState();
    getLookAndFeel().drawButtonBackground(g, *this, findColour(on ? buttonOnColourId : buttonColourId),
                                          highlighted, down);
    const auto ink = !isEnabled() ? palette::disabled : on ? palette::text : palette::textDim;
    const auto box = getLocalBounds().toFloat();
    const auto side = std::round(box.getHeight() * 0.5f);
    g.setFont(uiFontBold(13.0f));
    const auto word = getButtonText();
    const auto wordWidth = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), word) + 1.0f;
    const auto left = std::round(box.getCentreX() - (side + 7.0f + wordWidth) * 0.5f);
    const auto centre = juce::Point<float>(left + side * 0.5f, box.getCentreY());
    const auto radius = side * 0.5f - 1.0f;
    // Angles from twelve o'clock, clockwise.
    constexpr auto from = 0.55f, to = juce::MathConstants<float>::twoPi - 0.35f;
    juce::Path ring;
    ring.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, from, to, true);
    g.setColour(ink);
    g.strokePath(ring, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const auto tip = centre + juce::Point<float>(radius * std::sin(to), -radius * std::cos(to));
    const auto ahead = juce::Point<float>(std::cos(to), std::sin(to));
    const auto across = juce::Point<float>(-ahead.y, ahead.x);
    const auto head = side * 0.3f;
    juce::Path arrow;
    arrow.addTriangle(tip + ahead * head, tip - ahead * (head * 0.3f) + across * (head * 0.8f),
                      tip - ahead * (head * 0.3f) - across * (head * 0.8f));
    g.fillPath(arrow);
    drawSnappedText(g, word, juce::Rectangle<float>(left + side + 7.0f, box.getY(), wordWidth + 2.0f, box.getHeight())
                                 .toNearestInt(),
                    juce::Justification::centredLeft);
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
    clipHeading = area.removeFromTop(headingHeight);
    // The clip's Loop switch, wider and taller than any other control here.
    loopButton.setBounds(area.removeFromTop(loopHeight));
    area.removeFromTop(rowGap + 6);
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
