#include "ArrangementInternal.h"
#include "Playhead.h"
#include <optional>
#include <set>

// Rebuilding the clip view models and waveform caches from the edit.

namespace theta
{

void Arrangement::sync()
{
    clips.clear();
    std::set<juce::String> usedFiles;
    const auto tracks = te::getAudioTracks(*session.edit);
    syncTrackControls();
    selectedTrack = juce::jlimit(0, std::max(0, session.trackCount() - 1), selectedTrack);
    songEnd = 0.0;
    for (int track = 0; track < tracks.size(); ++track)
    {
        mute[static_cast<size_t>(track)]->setToggleState(tracks[track]->isMuted(false), juce::dontSendNotification);
        solo[static_cast<size_t>(track)]->setToggleState(tracks[track]->isSolo(false), juce::dontSendNotification);
        for (auto* clip : tracks[track]->getClips())
        {
            if (!session.shouldShowClipInArrangement(*clip))
                continue;
            const auto p = clip->getPosition();
            ClipView view {clip->itemID, clip->getName(), {p.time.getStart().inSeconds(), p.time.getEnd().inSeconds(), p.offset.inSeconds()}, nullptr, {}, clip->getSpeedRatio(),
                           p.offset.inSeconds() + p.time.getLength().inSeconds(), track, clip->getColour(), session.clipPluginCount(clip->itemID),
                           session.clipAutomations(clip->itemID)};
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
    std::erase_if(waveforms, [&usedFiles](const auto& item) { return !usedFiles.contains(item.first); });
    if (activeAutomationClip != te::EditItemID())
    {
        bool foundActive = false;
        for (const auto& clip : clips)
            if (clip.id == activeAutomationClip && activeAutomationIndex(clip) >= 0)
            {
                foundActive = true;
                break;
            }
        if (!foundActive)
        {
            activeAutomationClip = {};
            activeAutomationTarget = {};
        }
    }
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
        addAndMakeVisible(*muteButton);
        addAndMakeVisible(*soloButton);
        mute.push_back(std::move(muteButton));
        solo.push_back(std::move(soloButton));
    }
    while (static_cast<int>(mute.size()) > count)
    {
        mute.pop_back();
        solo.pop_back();
    }
}

void Arrangement::updateScroll()
{
    const auto total = std::max({8.0, songEnd + 60.0 / session.tempo() * 4.0, viewSpan});
    viewStart = std::clamp(viewStart, 0.0, std::max(0.0, total - viewSpan));
    scroll.setRangeLimits(0.0, total, juce::dontSendNotification);
    scroll.setCurrentRange(viewStart, viewSpan, juce::dontSendNotification);
    const auto trackTotal = static_cast<double>(session.trackCount()) * laneHeight();
    const auto trackVisible = static_cast<double>(laneContentHeight());
    trackScroll = std::clamp(trackScroll, 0.0, std::max(0.0, trackTotal - trackVisible));
    trackScrollBar.setRangeLimits(0.0, std::max(trackVisible, trackTotal), juce::dontSendNotification);
    trackScrollBar.setCurrentRange(trackScroll, trackVisible, juce::dontSendNotification);
    trackScrollBar.setVisible(trackTotal > trackVisible + 1.0);
}

}
