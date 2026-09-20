#include "ArrangementInternal.h"
#include "Playhead.h"
#include <algorithm>
#include <optional>
#include <set>

// Rebuilding the clip view models and waveform caches from the edit.

namespace rhino
{

void Arrangement::sync()
{
    clips.clear();
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
                           p.offset.inSeconds() + p.time.getLength().inSeconds(), track, clip->getColour(),
                           session.clipPluginCount(clip->itemID)};
            if (auto* audio = dynamic_cast<te::WaveAudioClip*>(clip))
            {
                const auto file = clip->getSourceFileReference().getFile();
                const auto key = file.getFullPathName();
                usedFiles.insert(key);
                auto& waveform = waveforms[key];
                if (!waveform) waveform = std::make_unique<Waveform>(*this, file);
                view.waveform = waveform.get();
                view.sourceDuration = audio->getSourceLength().inSeconds() / std::max(0.0001, view.speed);
            }
            else if (auto* midi = dynamic_cast<te::MidiClip*>(clip))
            {
                view.sourceDuration = te::Edit::getMaximumEditEnd().inSeconds();
                for (auto* note : midi->getSequence().getNotes())
                {
                    const auto noteStart = session.edit->tempoSequence.toTime(note->getStartBeat()).inSeconds();
                    const auto noteEnd = session.edit->tempoSequence.toTime(note->getStartBeat() + note->getLengthBeats()).inSeconds();
                    view.midiNotes.push_back({view.position.start + noteStart, view.position.start + noteEnd, note->getNoteNumber()});
                }
            }
            songEnd = std::max(songEnd, view.position.end);
            clips.push_back(view);
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
        muteButton->setTooltip("Mute track");
        soloButton->setTooltip("Solo track");
        muteButton->onClick = [this, track] { session.toggleTrackMute(track); };
        soloButton->onClick = [this, track] { session.toggleTrackSolo(track); };
        muteButton->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff97634c));
        soloButton->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff657440));
        auto volumeSlider = std::make_unique<juce::Slider>();
        volumeSlider->setSliderStyle(juce::Slider::LinearBar);
        // The bar paints the value itself, in a colour picked for whichever of
        // the fill and the trough each half of it lands on.
        volumeSlider->setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        volumeSlider->setRange(Session::minimumVolumeDb, Session::maximumVolumeDb, 0.1);
        // The bar is small enough that the unit costs more room than it earns.
        volumeSlider->setTextValueSuffix({});
        volumeSlider->setDoubleClickReturnValue(true, 0.0);
        volumeSlider->setTooltip("Track volume");
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
        panSlider->setTooltip("Track pan");
        panSlider->onDragStart = [this, track] { session.beginTrackPanGesture(track); };
        panSlider->onDragEnd = [this, track] { session.endTrackPanGesture(track); };
        panSlider->onValueChange = [this, track, slider = panSlider.get()]
        {
            session.setTrackPan(track, static_cast<float>(slider->getValue()));
        };
        // Level and position are different quantities, so they are not the
        // same colour: the fader keeps the accent green the app uses for
        // amounts, and pan takes the blue it uses for placement.
        volumeSlider->setColour(juce::Slider::trackColourId, juce::Colour(0xff45d0d4));
        volumeSlider->setColour(juce::Slider::backgroundColourId, juce::Colour(0xff1a2026));
        volumeSlider->setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xff0e1317));
        volumeSlider->setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0x33000000));
        panSlider->setColour(juce::Slider::trackColourId, juce::Colour(0xffd4564e));
        panSlider->setColour(juce::Slider::backgroundColourId, juce::Colour(0xff1a2026));
        panSlider->setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xff0e1317));
        panSlider->setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0x33000000));
        // The Info View explains a control while the pointer rests on it, so
        // every one of them reports its enter and exit to the arrangement.
        for (auto* control : std::initializer_list<juce::Component*>{muteButton.get(), soloButton.get(),
                                                                     volumeSlider.get(), panSlider.get()})
            control->addMouseListener(this, false);
        laneHeaders.addAndMakeVisible(*muteButton);
        laneHeaders.addAndMakeVisible(*soloButton);
        laneHeaders.addAndMakeVisible(*volumeSlider);
        laneHeaders.addAndMakeVisible(*panSlider);
        mute.push_back(std::move(muteButton));
        solo.push_back(std::move(soloButton));
        volume.push_back(std::move(volumeSlider));
        pan.push_back(std::move(panSlider));
    }
    while (static_cast<int>(mute.size()) > count)
    {
        mute.pop_back();
        solo.pop_back();
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
