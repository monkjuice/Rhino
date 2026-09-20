#include "SessionInternal.h"
#include <algorithm>
#include <set>

// Arrangement clip lookup and editing, and track mute/solo. Serves Arrangement.

namespace rhino
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

juce::Result Session::editClip(te::EditItemID id, ClipGeometry next, ClipGesture gesture, int targetTrack,
                               const std::vector<te::EditItemID>& movingWith)
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
                edit->state.setProperty("rhinoPatternInstrument",
                                        sourceInstrument == Instrument::Drums ? "drums"
                                            : sourceInstrument == Instrument::RhinoWave ? "wave"
                                            : sourceInstrument == Instrument::RhinoForge ? "forge" : "synth",
                                        &edit->getUndoManager());
        }
        if (!clip->moveTo(*target))
            return juce::Result::fail("The clip could not be moved to that track.");
    }
    clip->setPosition({{tracktion::core::TimePosition::fromSeconds(next.start),
                       tracktion::core::TimePosition::fromSeconds(next.end)},
                       tracktion::core::TimeDuration::fromSeconds(std::max(0.0, next.offset))});
    // Whatever it landed on gives way, and the clips still travelling with it
    // are spared: a run carried one beat to the right must not have its
    // leading clip delete the one behind it.
    makeRoomForClip(*clip, movingWith);
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

// One clip is the smallest region there is, so duplicating it is the region
// command applied to its own span on its own track: the copy lands flush
// against its end, replacing whatever was sitting there.
juce::Result Session::duplicateClip(te::EditItemID id)
{
    auto* clip = findClip(id);
    if (!clip) return juce::Result::fail("Select a clip to duplicate.");
    const auto tracks = te::getAudioTracks(*edit);
    const auto track = tracks.indexOf(dynamic_cast<te::AudioTrack*>(clip->getClipTrack()));
    if (track < 0) return juce::Result::fail("The selected clip is not on a track.");
    const auto time = clip->getPosition().time;
    const auto region = copyClipRegion(time.getStart().inSeconds(), time.getEnd().inSeconds(), track, track);
    if (region.clips.empty()) return juce::Result::fail("The duplicate clip could not be created.");
    std::vector<te::EditItemID> pasted;
    return pasteClipRegion(region, time.getEnd().inSeconds(), track, pasted);
}

// Deleting clips can take the one the note editor is pointed at, and pattern()
// dereferences that pointer, so every path that removes a clip ends here. The
// editor falls back to another MIDI clip on track one, or to a fresh starter
// clip when the track has none left.
void Session::repairPatternClip()
{
    if (patternClip != nullptr && findClip(patternClipID) == patternClip)
        return;
    patternClip = nullptr;
    auto* track = patternTrackOf(*edit);
    if (track == nullptr)
        return;
    for (auto* existing : track->getClips())
        if (auto* midi = dynamic_cast<te::MidiClip*>(existing))
        {
            patternClip = midi;
            break;
        }
    if (patternClip == nullptr)
    {
        const auto end = edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(4.0));
        patternClip = track->insertMIDIClip("Pattern 1", {{}, end}, nullptr).get();
        if (patternClip != nullptr)
        {
            patternClip->setColour(presetColour(PatternPreset::WarmPulse));
            patternClip->state.setProperty(starterPlaceholderID, true, nullptr);
        }
    }
    if (patternClip != nullptr)
        patternClipID = patternClip->itemID;
}

void Session::deleteClip(te::EditItemID id)
{
    if (auto* clip = findClip(id))
    {
        edit->getUndoManager().beginNewTransaction("Delete audio clip");
        clip->removeFromParent();
        repairPatternClip();
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
