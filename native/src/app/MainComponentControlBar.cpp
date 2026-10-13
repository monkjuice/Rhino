#include "MainComponent.h"
#include <algorithm>

namespace rhino
{

// One continuous band across the top of the window, and the hairlines that
// separate the few groups that need separating. The micro-headings that
// used to sit over each group - TEMPO, SIGNATURE, CLICK, TRANSPORT,
// POSITION, EDIT - are gone: six words of eight pixel type to label six
// controls that each say what they are, at the cost of a second row of
// height in the one band that is on screen the whole time.
void MainComponent::paintControlBar(juce::Graphics& g)
{
    g.setColour(palette::globalBar);
    g.fillRect(0, 0, getWidth(), controlBarHeight);
    g.setColour(palette::border);
    for (const auto divider : barDividers)
        g.fillRect(divider, barDividerTop, 1, controlBarHeight - barDividerTop * 2);
    // The foot of the bar, which is also the top edge of everything docked
    // beneath it. Drawn here rather than by the panels, so it runs the
    // whole width whether or not the browser is showing.
    g.fillRect(0, controlBarHeight - 1, getWidth(), 1);
}

// Four sections, ruled off from each other: the browser toggle, the song's
// own fields, the transport, and - from the right client edge inwards - the
// metronome and the undo pair. Every control takes the width it needs, and
// the readout is centred on the window in whatever is left between the two
// sides. It is the thing the bar is built around, but by being the one
// panel on it and the tallest thing in the row rather than by being wide:
// stretched across the whole leftover span it read as an empty bezel.
void MainComponent::layoutControlBar()
{
    barDividers.clear();
    const auto rule = [this](int x) { barDividers.push_back(x); };
    // A run of controls at one height, laid out from a running x.
    auto x = barEdgeMargin;
    const auto place = [&x](juce::Component& component, int width, int height, int top, int gap)
    {
        component.setBounds(x, top, width, height);
        x += width + gap;
    };

    // A square glyph and a field are different heights, so the glyphs are
    // centred on the fields rather than sharing their top edge: the bar
    // reads as one row because everything on it shares a middle.
    const auto transportTop = barControlTop + (fieldHeight - transportSize) / 2;
    // The sections spread apart on a wide bar and close up on a narrow one.
    // The readout is what the space is being spent on, and at the smallest
    // window a clock is worth more than air between the groups: held at the
    // full gap, the bar ran out of room for the readout at 976 pixels and
    // dropped it, which is inside the smallest window this supports.
    const auto sectionGap = juce::jlimit(barGroupGap, barSectionGap,
                                         barGroupGap + (getWidth() - 960) / 12);

    place(browserToggle, browserToggleWidth, transportSize, transportTop, sectionGap);
    rule(x - sectionGap / 2);

    // Tempo and signature are one group - they are both the song's own
    // settings - and take no rule between them, only the field gap.
    place(tempoBox, tempoWidth, fieldHeight, barControlTop, barFieldGap);
    place(signatureField, signatureWidth, fieldHeight, barControlTop, sectionGap);
    rule(x - sectionGap / 2);

    for (auto* button : std::initializer_list<juce::Component*>{&rewind, &stop, &play, &record})
        place(*button, transportSize, transportSize, transportTop, barTransportGap);
    x += sectionGap - barTransportGap;

    // The right of the bar, laid out from the right client edge inwards:
    // the view switch pinned to it, then the metronome in a section of its
    // own, then undo and redo. The click belongs with the tools rather
    // than with the song's settings - it is something switched on while
    // working, not a property of the document - so it sits over here.
    auto rightEdge = getWidth() - barEdgeMargin;
    const auto placeRight = [&rightEdge](juce::Component& component, int width, int height, int top, int gap)
    {
        rightEdge -= width;
        component.setBounds(rightEdge, top, width, height);
        rightEdge -= gap;
    };
    // Arrange then DJ, the one showing in green.
    placeRight(djToggle, transportSize, transportSize, transportTop, barTransportGap);
    placeRight(arrangementToggle, transportSize, transportSize, transportTop, sectionGap);
    djToggle.setToggleState(djViewOpen, juce::dontSendNotification);
    arrangementToggle.setToggleState(!djViewOpen, juce::dontSendNotification);
    rule(rightEdge + sectionGap / 2);
    placeRight(metronomeMenu, metronomeMenuWidth, transportSize, transportTop, 0);
    placeRight(metronome, transportSize, transportSize, transportTop, sectionGap);
    rule(rightEdge + sectionGap / 2);
    placeRight(redo, undoSize, transportSize, transportTop, barTransportGap);
    placeRight(undo, undoSize, transportSize, transportTop, sectionGap);

    // Centred in what the two sides have left rather than on the window:
    // the left of the bar carries four sections and the right two, so a
    // readout centred on the window sat hard against the transport with a
    // gulf on the other side of it. Below the width its three columns need
    // it is dropped rather than squeezed: a clock with half its digits
    // missing is worse than no clock, and the controls around it stay
    // reachable.
    const auto room = rightEdge - x;
    position.setVisible(room >= 260);
    if (position.isVisible())
    {
        const auto width = std::min(displayWidth, room);
        position.setBounds(x + (room - width) / 2, displayTop, width, displayHeight);
    }
}

// Record is one button for three things - start the count-in, start
// recording, stop what is running - because that is one idea to the person
// pressing it. Session decides which of them it is.
void MainComponent::toggleRecording()
{
    const auto result = session.toggleRecording();
    if (result.failed())
    {
        logStatus(result.getErrorMessage());
        return;
    }
    if (session.isCountingIn())
        logStatus("Counting in " + juce::String(session.countInBars())
                  + (session.countInBars() == 1 ? " bar..." : " bars..."));
    else if (session.isRecording())
        logStatus("Recording into the armed tracks");
    else
        logStatus("Recording stopped");
}

void MainComponent::showMetronomeMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader("METRONOME");
    menu.addItem(1, "Emphasize first beat", true, session.clickTrackEmphasiseBars());
    menu.addSeparator();
    menu.addSectionHeader("Level");
    menu.addItem(10, "-12 dB", true, std::abs(session.clickTrackGain() + 12.0f) < 0.1f);
    menu.addItem(11, "-6 dB", true, std::abs(session.clickTrackGain() + 6.0f) < 0.1f);
    menu.addItem(12, "0 dB", true, std::abs(session.clickTrackGain()) < 0.1f);
    menu.addSeparator();
    // The count-in is a metronome setting: it is the click, counted before
    // the transport moves, so it belongs in the metronome's own menu rather
    // than in a preferences dialog nobody would look in.
    menu.addSectionHeader("Count-in");
    menu.addItem(20, "Off", true, session.countInBars() == 0);
    for (int bars = 1; bars <= Session::maximumCountInBars; ++bars)
        menu.addItem(20 + bars, juce::String(bars) + (bars == 1 ? " bar" : " bars"),
                     true, session.countInBars() == bars);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(metronomeMenu),
        [safe = juce::Component::SafePointer<MainComponent>(this)] (int result)
        {
            if (safe == nullptr) return;
            if (result == 1) safe->session.setClickTrackEmphasiseBars(!safe->session.clickTrackEmphasiseBars());
            else if (result >= 10 && result <= 12) safe->session.setClickTrackGain(static_cast<float>((result - 12) * 6));
            else if (result >= 20 && result <= 20 + Session::maximumCountInBars)
            {
                const auto bars = result - 20;
                safe->session.setCountInBars(bars);
                safe->logStatus(bars == 0 ? "Recording starts immediately"
                                          : "Recording counts in " + juce::String(bars)
                                                + (bars == 1 ? " bar" : " bars"));
            }
        });
}

void MainComponent::showDisplayMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader("DISPLAY");
    menu.addItem(1, "Musical position", true, displayPosition);
    menu.addItem(2, "Tempo", true, displayTempo);
    menu.addItem(3, "Time signature", true, displayTimeSignature);
    menu.addItem(4, "Loop range", true, displayLoop);
    menu.addSeparator();
    menu.addItem(5, "Time and song length", true, displayTime);
    menu.addItem(6, "CPU", true, displayCpu);
    menu.addItem(7, "Memory", true, displayMemory);
    menu.addItem(8, "Audio device", true, displayAudio);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(position),
        [safe = juce::Component::SafePointer<MainComponent>(this)] (int result)
        {
            if (safe == nullptr) return;
            if (result == 1) safe->displayPosition = !safe->displayPosition;
            else if (result == 2) safe->displayTempo = !safe->displayTempo;
            else if (result == 3) safe->displayTimeSignature = !safe->displayTimeSignature;
            else if (result == 4) safe->displayLoop = !safe->displayLoop;
            else if (result == 5) safe->displayTime = !safe->displayTime;
            else if (result == 6) safe->displayCpu = !safe->displayCpu;
            else if (result == 7) safe->displayMemory = !safe->displayMemory;
            else if (result == 8) safe->displayAudio = !safe->displayAudio;
            safe->timerCallback();
        });
}

void MainComponent::updateRecordButton()
{
    const auto state = session.isRecording()   ? RecordButton::State::recording
                     : session.isCountingIn()  ? RecordButton::State::countingIn
                     : session.anyTrackArmed() ? RecordButton::State::armed
                                               : RecordButton::State::idle;
    // A count-in starts the transport itself, so the button press that
    // began it is long over by the time recording actually starts. This is
    // what says so, rather than leaving the count-in message standing while
    // the take is being made.
    if (state != recordState)
    {
        if (state == RecordButton::State::recording && recordState == RecordButton::State::countingIn)
            logStatus("Recording into the armed tracks");
        recordState = state;
    }
    record.setState(state);
}

}
