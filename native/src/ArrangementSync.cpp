#include "ArrangementInternal.h"
#include "Playhead.h"
#include <algorithm>
#include <optional>
#include <set>

// Rebuilding the clip view models and waveform caches from the edit.

namespace rhino
{
namespace
{
// The record dot on a track card, drawn rather than set as text. It used to be
// U+25CF handed to the UI face, which does not carry it: the glyph came from
// whatever the system fell back to, and a fallback face sets its own side
// bearings and its own baseline - so the dot sat left of centre and high in the
// key, differently on every machine. A filled ellipse is centred by
// construction and is the same mark at every size.
class RecordArmButton final : public juce::TextButton
{
public:
    RecordArmButton() : juce::TextButton("Arm") {}

    void paintButton(juce::Graphics& g, bool highlighted, bool pressed) override
    {
        auto& look = getLookAndFeel();
        look.drawButtonBackground(g, *this,
                                  findColour(getToggleState() ? buttonOnColourId : buttonColourId),
                                  highlighted, pressed);
        g.setColour(findColour(getToggleState() ? textColourOnId : textColourOffId));
        const auto bounds = getLocalBounds().toFloat();
        const auto diameter = juce::jmin(7.0f, bounds.getWidth() * 0.45f, bounds.getHeight() * 0.45f);
        if (diameter <= 0.0f) return;
        g.fillEllipse(juce::Rectangle<float>(diameter, diameter).withCentre(bounds.getCentre()));
    }
};
}

const Arrangement::TrackFacts& Arrangement::factsFor(int track) const
{
    static const TrackFacts none;
    return juce::isPositiveAndBelow(track, static_cast<int>(trackFacts.size()))
        ? trackFacts[static_cast<size_t>(track)] : none;
}

void Arrangement::sync()
{
    clips.clear();
    armedTracks.clear();
    trackFacts.clear();
    clipNameWidths.clear();
    std::set<juce::String> usedFiles;
    const auto tracks = te::getAudioTracks(*session.edit);
    syncTrackControls();
    selectedTrack = juce::jlimit(0, session.masterTrackIndex(), selectedTrack);
    // A gathered selection can name tracks the edit no longer has.
    std::erase_if(selectedTracks, [this](int track) { return !juce::isPositiveAndBelow(track, session.trackCount()); });
    if (selectedTracks.empty())
        selectedTracks = {selectedTrack};
    trackSelectionAnchor = juce::jlimit(0, std::max(0, session.trackCount() - 1), trackSelectionAnchor);
    songEnd = 0.0;
    for (int track = 0; track < tracks.size(); ++track)
    {
        const auto mixer = session.trackMixer(track);
        const auto index = static_cast<size_t>(track);
        mute[index]->setToggleState(mixer.muted, juce::dontSendNotification);
        solo[index]->setToggleState(mixer.soloed, juce::dontSendNotification);
        armedTracks.push_back(session.isTrackArmed(track));
        arm[index]->setToggleState(armedTracks.back(), juce::dontSendNotification);
        // What each of the card's controls does, named for the track it is on.
        // Set here rather than where the controls are built, because every one
        // of these sentences has the track's name in it and two of them change
        // with what the track has become - a lane that takes an instrument
        // starts recording MIDI instead of audio. The shell shows them in the
        // Info View while the pointer rests on the control.
        const auto name = session.trackName(track);
        trackFacts.push_back({name, session.trackColour(track),
                              session.trackType(track) == Session::TrackType::midi,
                              session.trackHasInstrument(track), session.isGroupBusTrack(track)});
        mute[index]->setTooltip("Mute " + name + " - silences this track while the rest keeps playing.");
        solo[index]->setTooltip("Solo " + name + " - silences every track that is not soloed.");
        // The dot is the same whatever the track records, because the track
        // already says which that is. What it would capture goes in the
        // description, which follows the track as an instrument lands on it.
        const auto takes = session.trackRecordInput(track);
        arm[index]->setTooltip(takes == Session::RecordInput::midi
                                   ? "Arm " + name + " - records what you play on the MIDI input into a new clip."
                                   : "Arm " + name + " - records its input, "
                                         + session.trackAudioInputName(track) + ", into a new clip.");
        // Whatever the card shows here is what the track actually listens to:
        // the name comes from the session rather than from anything the menu
        // remembered. Which of the two inputs is named follows from what the
        // track is, which is fixed when the track is made.
        trackInput[index]->setButtonText((takes == Session::RecordInput::midi
                                              ? session.trackMidiInputName(track)
                                              : session.trackAudioInputName(track)) + "  v");
        trackInput[index]->setTooltip(takes == Session::RecordInput::midi
            ? "MIDI From on " + name + " - which input plays it. All Ins is every keyboard "
              "plus the typing keyboard; Computer Keyboard is the typing keyboard alone."
            : "Audio From on " + name + " - which input it records. Default In is whichever input "
              "Audio settings chose, so it follows the machine the project is opened on.");
        monitor[index]->setMode(session.trackMonitoring(track));
        monitor[index]->setTooltip("Monitor on " + name + " - On plays its input back at all times, Auto only "
                                   "while the track is armed, Off never."
                                   + juce::String(takes == Session::RecordInput::audio
                                                      ? " Use headphones: a built-in microphone and speakers "
                                                        "will feed back." : ""));
        volume[index]->setTooltip("Volume of " + name + " - drag to set the level, double-click for 0.0 dB.");
        pan[index]->setTooltip("Pan of " + name + " - drag to place it between the speakers, double-click to "
                               "centre it.");
        if (!volume[index]->isMouseButtonDown())
            volume[index]->setValue(mixer.volumeDb, juce::dontSendNotification);
        if (!pan[index]->isMouseButtonDown())
            pan[index]->setValue(mixer.pan, juce::dontSendNotification);
        for (auto* clip : tracks[track]->getClips())
        {
            if (!session.shouldShowClipInArrangement(*clip))
                continue;
            const auto p = clip->getPosition();
            ClipView view {clip->itemID, clip->getName(), {p.time.getStart().inSeconds(), p.time.getEnd().inSeconds(), p.offset.inSeconds()}, nullptr, {}, clip->getSpeedRatio(),
                           p.offset.inSeconds() + p.time.getLength().inSeconds(), track, Session::clipColour(*clip),
                           Session::clipPluginCount(*clip)};
            if (auto* audio = dynamic_cast<te::WaveAudioClip*>(clip))
            {
                const auto file = clip->getSourceFileReference().getFile();
                const auto key = file.getFullPathName();
                usedFiles.insert(key);
                auto& waveform = waveforms[key];
                if (!waveform) waveform = std::make_unique<Waveform>(*this, file);
                view.waveform = waveform.get();
                // Not getSpeedRatio: a warped clip leaves that at one and takes
                // its speed from its own tempo against the song's, so reading
                // the ratio would draw the first half of a stretched file
                // across the whole clip and silence after it.
                view.speed = session.clipPlaybackSpeed(*audio);
                view.sourceDuration = audio->getSourceLength().inSeconds() / std::max(0.0001, view.speed);
            }
            else if (auto* midi = dynamic_cast<te::MidiClip*>(clip))
            {
                view.sourceDuration = te::Edit::getMaximumEditEnd().inSeconds();
                // The sequence is the whole recording; the clip shows a window
                // into it that opens at the offset. Drawing from the sequence
                // start instead puts a left-trimmed clip - one made by a split,
                // or by pasting a region that began part-way through a clip -
                // full of notes it never plays, with the ones it does play
                // pushed off the right-hand end.
                const auto& notes = midi->getSequence().getNotes();
                view.midiNotes.reserve(static_cast<size_t>(notes.size()));
                for (auto* note : notes)
                {
                    const auto noteStart = session.edit->tempoSequence.toTime(note->getStartBeat()).inSeconds()
                                         - p.offset.inSeconds();
                    const auto noteEnd = session.edit->tempoSequence.toTime(note->getStartBeat() + note->getLengthBeats()).inSeconds()
                                       - p.offset.inSeconds();
                    view.midiNotes.push_back({view.position.start + noteStart, view.position.start + noteEnd, note->getNoteNumber()});
                }
            }
            songEnd = std::max(songEnd, view.position.end);
            clips.push_back(std::move(view));
        }
    }
    if (!masterVolume.isMouseButtonDown())
        masterVolume.setValue(session.masterVolumeDb(), juce::dontSendNotification);
    if (!masterPan.isMouseButtonDown())
        masterPan.setValue(session.masterPan(), juce::dontSendNotification);
    std::erase_if(waveforms, [&usedFiles](const auto& item) { return !usedFiles.contains(item.first); });
    // Read off the rebuilt clips, so a region that belongs to a selection
    // travels with it: moving, trimming or nudging a clip carries its
    // highlight along instead of leaving it behind at the old position. A
    // region dragged out over the lanes belongs to the timeline, so it only
    // has its rows clamped to tracks that still exist.
    if (regionFollowsClips && (!selectedClips.empty() || selected != te::EditItemID()))
        setRegionFromSelectedClips();
    else if (timeSelection.active)
    {
        const auto kept = timeSelection;
        setTimeSelection(kept.start, kept.end, kept.firstTrack, kept.lastTrack);
    }
    buildRows();
    // A name being typed on a track that has just gone is abandoned: committing
    // it would rename whatever took that index instead.
    if (renamingTrack >= 0 && !juce::isPositiveAndBelow(renamingTrack, session.trackCount()))
        endRename(false);
    if (focusedAutomation.isValid() && !session.trackAutomationState(focusedAutomation).visible)
    {
        focusedAutomation = {};
        if (focus == Focus::automation)
            focus = Focus::none;
    }
    // A track created just now has controls but no bounds yet, and a component
    // at nothing by nothing draws nothing. Placing them here means a new card
    // arrives complete rather than waiting for the next resize to fill in.
    resized();
    updateScroll();
    repaint();
}

void Arrangement::syncTrackControls()
{
    const auto count = session.trackCount();
    while (static_cast<int>(mute.size()) < count)
    {
        const auto track = static_cast<int>(mute.size());
        auto muteButton = std::make_unique<juce::TextButton>("M");
        auto soloButton = std::make_unique<juce::TextButton>("S");
        // The record dot, as in Live and Logic, painted by the button itself.
        auto armButton = std::make_unique<RecordArmButton>();
        muteButton->onClick = [this, track] { session.toggleTrackMute(track); };
        soloButton->onClick = [this, track] { session.toggleTrackSolo(track); };
        // Arming reports twice: whether the track can be armed at all, and
        // whether the input behind it is actually there to record from.
        armButton->onClick = [this, track]
        {
            const auto wanted = !session.isTrackArmed(track);
            const auto result = session.setTrackArmed(track, wanted);
            if (status)
            {
                if (result.failed()) status(result.getErrorMessage());
                else if (!wanted) status(session.trackName(track) + " is no longer armed");
                // Armed is not the same as playable. With no keyboard plugged
                // in, the typing keyboard is the only thing that can play the
                // track, and an armed track that makes no sound is the most
                // confusing state the app has.
                else if (session.trackRecordInput(track) == Session::RecordInput::midi
                         && !session.hasHardwareMidiInput()
                         && (!typingKeyboardEnabled || !typingKeyboardEnabled()))
                    status(session.trackName(track) + " is armed, but no MIDI keyboard is plugged in. "
                           "Turn on Edit > Computer keyboard plays MIDI (M) to play it from the typing keyboard.");
                else status(session.trackName(track) + " is armed: press Record or F9");
            }
        };
        // Mute and solo are opposite states and are lit as opposites: a muted
        // track's key goes dark and recedes, a soloed track's goes bright and
        // takes dark text, so the one track that is being listened to is the
        // one lit thing in the stack. Telling them apart by value rather than
        // by hue is what lets the chrome stay grey without the two keys
        // becoming indistinguishable - which is exactly what happened when
        // both were simply given the neutral "on" fill.
        muteButton->setColour(juce::TextButton::buttonOnColourId, palette::disabled);
        muteButton->setColour(juce::TextButton::textColourOnId, palette::text);
        soloButton->setColour(juce::TextButton::buttonOnColourId, palette::activeNeutral);
        soloButton->setColour(juce::TextButton::textColourOnId, palette::appBackground);
        // Arm keeps red. It is the one state in the interface that is worth a
        // colour, and it is the same red the record button and the record dot
        // on the card already wear.
        armButton->setColour(juce::TextButton::buttonOnColourId, palette::recordAccent.darker(0.2f));
        armButton->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        // What the track takes in. The list is the machine's devices at the
        // moment it is opened, so it is a menu rather than a ComboBox holding
        // a snapshot of them, and which list that is follows from the track.
        auto inputButton = std::make_unique<juce::TextButton>("All Ins  v");
        inputButton->onClick = [this, track] { showTrackInputMenu(track); };
        inputButton->setColour(juce::TextButton::buttonColourId, palette::control);
        inputButton->setColour(juce::TextButton::textColourOffId, palette::textDim);
        // And whether you hear it. The three settings are one answer, so they
        // are one control; see MonitorSelector.h.
        auto monitorControl = std::make_unique<MonitorSelector>();
        monitorControl->onChoose = [this, track](Session::InputMonitoring mode)
        {
            applyMonitorChoice(track, mode);
        };
        auto volumeSlider = std::make_unique<juce::Slider>();
        volumeSlider->setSliderStyle(juce::Slider::LinearBar);
        // The bar paints the value itself, in a colour picked for whichever of
        // the fill and the trough each half of it lands on.
        volumeSlider->setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        volumeSlider->setRange(Session::minimumVolumeDb, Session::maximumVolumeDb, 0.1);
        // The bar is small enough that the unit costs more room than it earns.
        volumeSlider->setTextValueSuffix({});
        volumeSlider->setDoubleClickReturnValue(true, 0.0);
        volumeSlider->onDragStart = [this, track] { session.beginTrackVolumeGesture(track); };
        volumeSlider->onDragEnd = [this, track] { session.endTrackVolumeGesture(track); };
        volumeSlider->onValueChange = [this, track, slider = volumeSlider.get()]
        {
            session.setTrackVolumeDb(track, static_cast<float>(slider->getValue()));
        };
        auto panSlider = std::make_unique<juce::Slider>();
        panSlider->setSliderStyle(juce::Slider::LinearBar);
        panSlider->setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        panSlider->setRange(-1.0, 1.0, 0.01);
        panSlider->setDoubleClickReturnValue(true, 0.0);
        panSlider->onDragStart = [this, track] { session.beginTrackPanGesture(track); };
        panSlider->onDragEnd = [this, track] { session.endTrackPanGesture(track); };
        panSlider->onValueChange = [this, track, slider = panSlider.get()]
        {
            session.setTrackPan(track, static_cast<float>(slider->getValue()));
        };
        // Level and position are different quantities, so they are not the
        // same colour: volume is cyan and pan is red, here and on the main row
        // alike. These two are the reason the chrome around them is grey -
        // they say which fader is which at a glance, and they cannot do that
        // from within a bar full of other coloured things.
        volumeSlider->setColour(juce::Slider::trackColourId, palette::volume);
        volumeSlider->setColour(juce::Slider::backgroundColourId, palette::displayInset);
        volumeSlider->setColour(juce::Slider::textBoxTextColourId, palette::appBackground);
        volumeSlider->setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0x33000000));
        panSlider->setColour(juce::Slider::trackColourId, palette::pan);
        panSlider->setColour(juce::Slider::backgroundColourId, palette::displayInset);
        panSlider->setColour(juce::Slider::textBoxTextColourId, palette::appBackground);
        panSlider->setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0x33000000));
        laneHeaders.addAndMakeVisible(*muteButton);
        laneHeaders.addAndMakeVisible(*soloButton);
        laneHeaders.addAndMakeVisible(*armButton);
        laneHeaders.addAndMakeVisible(*inputButton);
        laneHeaders.addAndMakeVisible(*monitorControl);
        laneHeaders.addAndMakeVisible(*volumeSlider);
        laneHeaders.addAndMakeVisible(*panSlider);
        mute.push_back(std::move(muteButton));
        solo.push_back(std::move(soloButton));
        arm.push_back(std::move(armButton));
        trackInput.push_back(std::move(inputButton));
        monitor.push_back(std::move(monitorControl));
        volume.push_back(std::move(volumeSlider));
        pan.push_back(std::move(panSlider));
    }
    while (static_cast<int>(mute.size()) > count)
    {
        mute.pop_back();
        solo.pop_back();
        arm.pop_back();
        trackInput.pop_back();
        monitor.pop_back();
        volume.pop_back();
        pan.pop_back();
    }
}

void Arrangement::updateScroll()
{
    const auto total = std::max({8.0, songEnd + 60.0 / session.tempo() * 4.0, viewSpan});
    viewStart = std::clamp(viewStart, 0.0, std::max(0.0, total - viewSpan));
    scroll.setRangeLimits(0.0, total, juce::dontSendNotification);
    scroll.setCurrentRange(viewStart, viewSpan, juce::dontSendNotification);
    const auto trackTotal = static_cast<double>(rowsHeight);
    const auto trackVisible = static_cast<double>(laneContentHeight());
    trackScroll = std::clamp(trackScroll, 0.0, std::max(0.0, trackTotal - trackVisible));
    trackScrollBar.setRangeLimits(0.0, std::max(trackVisible, trackTotal), juce::dontSendNotification);
    trackScrollBar.setCurrentRange(trackScroll, trackVisible, juce::dontSendNotification);
    trackScrollBar.setVisible(trackTotal > trackVisible + 1.0);
}

}
