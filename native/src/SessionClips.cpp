#include "SessionInternal.h"
#include <algorithm>
#include <set>

// Arrangement clip lookup and editing, and track mute/solo. Serves Arrangement.

namespace theta
{

te::Clip* Session::findClip(te::EditItemID id) const
{
    for (auto* track : te::getAudioTracks(*edit))
        if (auto* clip = track->findClipForID(id))
            return clip;
    return nullptr;
}

te::WaveAudioClip* Session::findAudioClip(te::EditItemID id) const
{
    return dynamic_cast<te::WaveAudioClip*>(findClip(id));
}

bool Session::shouldShowClipInArrangement(te::Clip& clip) const
{
    auto* midi = dynamic_cast<te::MidiClip*>(&clip);
    if (midi == nullptr)
        return true;
    return !static_cast<bool>(clip.state.getProperty(starterPlaceholderID, false))
        || midi->getSequence().getNumNotes() > 0;
}

juce::Result Session::editClip(te::EditItemID id, ClipGeometry next, ClipGesture gesture, int targetTrack)
{
    auto* clip = findClip(id);
    if (!clip) return juce::Result::fail("Select a clip first.");
    if (!std::isfinite(next.start) || !std::isfinite(next.end) || !std::isfinite(next.offset)
        || next.start < 0.0 || next.end <= next.start || next.offset < -1.0e-8
        || next.end > te::Edit::getMaximumEditEnd().inSeconds())
        return juce::Result::fail("Invalid clip position.");
    const auto tracks = te::getAudioTracks(*edit);
    auto* oldTrack = clip->getClipTrack();
    const auto oldTrackIndex = tracks.indexOf(dynamic_cast<te::AudioTrack*>(oldTrack));
    const auto movingMidi = dynamic_cast<te::MidiClip*>(clip) != nullptr;
    const auto sourceInstrument = movingMidi && oldTrackIndex >= 0 ? activeTrackInstrument(*tracks[oldTrackIndex])
                                                                   : Instrument::Utility;
    if (targetTrack < 0 || gesture != ClipGesture::move)
        targetTrack = oldTrackIndex;
    if (targetTrack < 0 || targetTrack > tracks.size())
        return juce::Result::fail("Drop the clip on a track lane.");
    const auto old = clip->getPosition();
    if (std::abs(old.time.getStart().inSeconds() - next.start) < 1.0e-8
        && std::abs(old.time.getEnd().inSeconds() - next.end) < 1.0e-8
        && std::abs(old.offset.inSeconds() - next.offset) < 1.0e-8
        && targetTrack == oldTrackIndex)
        return juce::Result::ok();
    auto& transport = edit->getTransport();
    const auto wasPlaying = transport.isPlaying();
    const auto hadPlaybackContext = transport.isPlayContextActive();
    edit->getUndoManager().beginNewTransaction(gesture == ClipGesture::move ? "Move clip" : "Trim clip");
    if (targetTrack == tracks.size())
    {
        auto newTrack = edit->insertNewAudioTrack(te::TrackInsertPoint::getEndOfTracks(*edit), nullptr, false);
        if (newTrack == nullptr)
            return juce::Result::fail("Could not create a track for the moved clip.");
        newTrack->setName("Audio " + juce::String(tracks.size()));
        auto audioDevice = edit->getPluginCache().createNewPlugin(UtilityDevice::xmlTypeName, {});
        newTrack->pluginList.insertPlugin(audioDevice, 0, nullptr);
        targetTrack = tracks.size();
    }
    const auto refreshedTracks = te::getAudioTracks(*edit);
    if (targetTrack != oldTrackIndex)
    {
        auto* target = refreshedTracks[targetTrack];
        if (movingMidi)
        {
            bool instrumentChanged = false;
            const auto result = switchTrackInstrument(*edit, *target, sourceInstrument, instrumentChanged,
                                                      forgeDescription ? &*forgeDescription : nullptr);
            if (result.failed())
                return result;
            if (targetTrack == 0)
                edit->state.setProperty("thetaPatternInstrument",
                                        sourceInstrument == Instrument::Drums ? "drums"
                                            : sourceInstrument == Instrument::ThetaWave ? "wave"
                                            : sourceInstrument == Instrument::ThetaForge ? "forge" : "synth",
                                        &edit->getUndoManager());
        }
        if (!clip->moveTo(*target))
            return juce::Result::fail("The clip could not be moved to that track.");
    }
    clip->setPosition({{tracktion::core::TimePosition::fromSeconds(next.start),
                       tracktion::core::TimePosition::fromSeconds(next.end)},
                       tracktion::core::TimeDuration::fromSeconds(std::max(0.0, next.offset))});
    refreshLoop();
    edit->getUndoManager().beginNewTransaction();
    markModified();
    if (targetTrack != oldTrackIndex && (wasPlaying || hadPlaybackContext))
    {
        transport.freePlaybackContext();
        transport.ensureContextAllocated(true);
        if (wasPlaying)
            transport.play(true);
    }
    else if (wasPlaying)
    {
        edit->restartPlayback();
    }
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::splitClip(te::EditItemID id, double splitTimeSeconds)
{
    auto* clip = findClip(id);
    if (!clip) return juce::Result::fail("Select a clip to split.");
    if (!std::isfinite(splitTimeSeconds)) return juce::Result::fail("Invalid split position.");

    const auto old = clip->getPosition();
    const auto split = tracktion::core::TimePosition::fromSeconds(splitTimeSeconds);
    constexpr double minimumSeconds = 0.01;
    if (split <= old.time.getStart() + tracktion::core::TimeDuration::fromSeconds(minimumSeconds)
        || split >= old.time.getEnd() - tracktion::core::TimeDuration::fromSeconds(minimumSeconds))
        return juce::Result::fail("Move the playhead inside the selected clip before splitting.");

    edit->getUndoManager().beginNewTransaction("Split audio clip");
    auto* track = clip->getClipTrack();
    auto* right = track != nullptr ? track->splitClip(*clip, split) : nullptr;
    if (right == nullptr)
        return juce::Result::fail("The right-hand split clip could not be created.");
    refreshLoop();
    edit->getUndoManager().beginNewTransaction();
    markModified();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::duplicateClip(te::EditItemID id)
{
    auto* clip = findClip(id);
    if (!clip) return juce::Result::fail("Select a clip to duplicate.");
    const auto old = clip->getPosition();
    auto* track = clip->getClipTrack();
    if (track == nullptr) return juce::Result::fail("The selected clip is not on a track.");
    edit->getUndoManager().beginNewTransaction("Duplicate clip");
    const auto duplicateRange = firstFreeDuplicateRange(*clip);
    te::Clip* copy = nullptr;
    if (auto* audio = dynamic_cast<te::WaveAudioClip*>(clip))
    {
        copy = track->insertWaveClip(audio->getName() + " copy", audio->getSourceFileReference().getFile(),
            {duplicateRange, old.offset}, false).get();
        if (copy != nullptr)
            copy->setColour(audio->getColour());
    }
    else if (auto* midi = dynamic_cast<te::MidiClip*>(clip))
        if (auto midiCopy = track->insertMIDIClip(midi->getName() + " copy",
            duplicateRange, nullptr))
        {
            midiCopy->cloneFrom(midi);
            midiCopy->setPosition({duplicateRange, old.offset});
            copy = midiCopy.get();
        }
    if (copy == nullptr)
        return juce::Result::fail("The duplicate clip could not be created.");
    refreshLoop();
    edit->getUndoManager().beginNewTransaction();
    markModified();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::pasteClips(const std::vector<te::EditItemID>& source, double destinationStart,
                                 int destinationTrack, std::vector<te::EditItemID>& pasted)
{
    struct SourceClip { te::Clip* clip; ClipGeometry position; int track; };
    std::vector<SourceClip> originals;
    auto tracks = te::getAudioTracks(*edit);
    for (const auto id : source)
    {
        auto* clip = findClip(id);
        if (clip == nullptr) continue;
        const auto track = tracks.indexOf(dynamic_cast<te::AudioTrack*>(clip->getClipTrack()));
        if (track < 0) continue;
        const auto p = clip->getPosition();
        originals.push_back({clip, {p.time.getStart().inSeconds(), p.time.getEnd().inSeconds(), p.offset.inSeconds()}, track});
    }
    if (originals.empty()) return juce::Result::fail("Copy one or more clips first.");
    if (!std::isfinite(destinationStart) || destinationStart < 0.0 || destinationTrack < 0)
        return juce::Result::fail("Choose a valid paste location.");

    const auto firstTime = std::min_element(originals.begin(), originals.end(), [] (const auto& a, const auto& b) { return a.position.start < b.position.start; })->position.start;
    const auto firstTrack = std::min_element(originals.begin(), originals.end(), [] (const auto& a, const auto& b) { return a.track < b.track; })->track;
    const auto lastTrack = std::max_element(originals.begin(), originals.end(), [] (const auto& a, const auto& b) { return a.track < b.track; })->track;
    const auto requiredTracks = destinationTrack + lastTrack - firstTrack + 1;

    edit->getUndoManager().beginNewTransaction("Paste clips");
    while (te::getAudioTracks(*edit).size() < requiredTracks)
    {
        const auto index = te::getAudioTracks(*edit).size();
        auto newTrack = edit->insertNewAudioTrack(te::TrackInsertPoint::getEndOfTracks(*edit), nullptr, false);
        if (newTrack == nullptr) return juce::Result::fail("Could not create a track for pasted clips.");
        newTrack->setName("Audio " + juce::String(index));
        newTrack->pluginList.insertPlugin(edit->getPluginCache().createNewPlugin(UtilityDevice::xmlTypeName, {}), 0, nullptr);
    }

    pasted.clear();
    for (const auto& item : originals)
    {
        auto* target = te::getAudioTracks(*edit)[destinationTrack + item.track - firstTrack];
        const auto start = destinationStart + item.position.start - firstTime;
        const auto range = tracktion::core::TimeRange {tracktion::core::TimePosition::fromSeconds(start),
                                                        tracktion::core::TimePosition::fromSeconds(start + item.position.end - item.position.start)};
        te::Clip* copy = nullptr;
        if (auto* audio = dynamic_cast<te::WaveAudioClip*>(item.clip))
        {
            copy = target->insertWaveClip(audio->getName() + " copy", audio->getSourceFileReference().getFile(),
                                          {range, tracktion::core::TimeDuration::fromSeconds(item.position.offset)}, false).get();
            if (copy != nullptr) copy->setColour(audio->getColour());
        }
        else if (auto* midi = dynamic_cast<te::MidiClip*>(item.clip))
        {
            bool instrumentChanged = false;
            const auto sourceInstrument = activeTrackInstrument(*te::getAudioTracks(*edit)[item.track]);
            const auto instrumentResult = switchTrackInstrument(*edit, *target, sourceInstrument, instrumentChanged,
                                                                forgeDescription ? &*forgeDescription : nullptr);
            if (instrumentResult.failed()) return instrumentResult;
            if (auto midiCopy = target->insertMIDIClip(midi->getName() + " copy", range, nullptr))
            {
                midiCopy->cloneFrom(midi);
                midiCopy->setPosition({range, tracktion::core::TimeDuration::fromSeconds(item.position.offset)});
                copy = midiCopy.get();
            }
        }
        if (copy == nullptr) return juce::Result::fail("The clip could not be pasted.");
        pasted.push_back(copy->itemID);
    }
    refreshLoop();
    edit->getUndoManager().beginNewTransaction();
    markModified();
    if (edit->getTransport().isPlaying()) edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

void Session::deleteClip(te::EditItemID id)
{
    if (auto* clip = findClip(id))
    {
        const auto deletingPattern = clip == patternClip;
        edit->getUndoManager().beginNewTransaction("Delete audio clip");
        clip->removeFromParent();
        if (deletingPattern)
        {
            patternClip = nullptr;
            const auto tracks = te::getAudioTracks(*edit);
            if (!tracks.isEmpty())
            {
                for (auto* existing : tracks[0]->getClips())
                    if (auto* midi = dynamic_cast<te::MidiClip*>(existing))
                    {
                        patternClip = midi;
                        break;
                    }
                if (patternClip == nullptr)
                {
                    const auto end = edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(4.0));
                    patternClip = tracks[0]->insertMIDIClip("Pattern 1", {{}, end}, nullptr).get();
                    if (patternClip != nullptr)
                    {
                        patternClip->setColour(presetColour(PatternPreset::WarmPulse));
                        patternClip->state.setProperty(starterPlaceholderID, true, nullptr);
                    }
                }
                if (patternClip != nullptr)
                    patternClipID = patternClip->itemID;
            }
        }
        refreshLoop();
        edit->getUndoManager().beginNewTransaction();
        markModified();
        sendSynchronousChangeMessage();
    }
}

juce::Result Session::cycleClipColour(te::EditItemID id)
{
    auto* clip = findClip(id);
    if (clip == nullptr)
        return juce::Result::fail("Select a clip first.");
    edit->getUndoManager().beginNewTransaction("Color clip");
    clip->setColour(nextClipColour(clip->getColour()));
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

int Session::clipPluginCount(te::EditItemID id) const
{
    auto* clip = dynamic_cast<te::AudioClipBase*>(findClip(id));
    if (clip == nullptr || clip->getPluginList() == nullptr)
        return 0;
    return clip->getPluginList()->size();
}

void Session::toggleTrackMute(int track)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size())) return;
    edit->getUndoManager().beginNewTransaction("Track mute");
    tracks[track]->state.setProperty(te::IDs::mute, !tracks[track]->isMuted(false), &edit->getUndoManager());
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
}

void Session::toggleTrackSolo(int track)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size())) return;
    edit->getUndoManager().beginNewTransaction("Track solo");
    tracks[track]->state.setProperty(te::IDs::solo, !tracks[track]->isSolo(false), &edit->getUndoManager());
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
}

}
