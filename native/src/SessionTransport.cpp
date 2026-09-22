#include "SessionInternal.h"
#include <array>

namespace rhino
{
namespace
{
void releasePluginList(te::PluginList* list)
{
    if (list == nullptr)
        return;
    for (auto* plugin : *list)
        if (plugin != nullptr)
            plugin->midiPanic();
}

// Automation lanes hang off the track they are drawn under, or off the edit for
// the master, and hold their points in seconds. They sweep alongside the clips,
// so they follow the same factor rather than drifting out from under them.
void rescaleAutomationPoints(juce::ValueTree owner, double scale, juce::UndoManager* undoManager)
{
    for (int lane = 0; lane < owner.getNumChildren(); ++lane)
    {
        auto state = owner.getChild(lane);
        if (!state.hasType(trackAutomationID))
            continue;
        for (int child = 0; child < state.getNumChildren(); ++child)
        {
            auto point = state.getChild(child);
            if (!point.hasType(automationPointID))
                continue;
            const auto seconds = static_cast<double>(point.getProperty(automationTimeID, 0.0));
            if (std::isfinite(seconds))
                point.setProperty(automationTimeID, std::max(0.0, seconds * scale), undoManager);
        }
    }
}
}

juce::Result Session::importAudio(const juce::File& file)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    te::AudioFile audio(engine, file);
    const auto duration = audio.getLength();
    if (duration <= 0.0)
        return juce::Result::fail("This file could not be read as audio.");

    // No track is an audio track by default, so an import with no target gets
    // one of its own rather than landing on whatever happens to be first.
    const auto added = addAudioTrack();
    if (added.failed())
        return added;
    return importAudioAt(file, trackCount() - 1, 0.0);
}

juce::Result Session::importAudioAt(const juce::File& file, int trackIndex, double startSeconds)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (!std::isfinite(startSeconds) || startSeconds < 0.0)
        return juce::Result::fail("Invalid audio drop position.");
    if (isGroupBusTrack(trackIndex))
        return juce::Result::fail("A group track carries its members' audio, so it takes no clips.");
    // Audio belongs on an audio track. Dropping a sample onto a MIDI track is
    // the one drop Rhino refuses outright.
    if (trackType(trackIndex) == TrackType::midi)
        return juce::Result::fail(trackHasInstrument(trackIndex)
            ? "That track runs an instrument. Drop audio on an audio track instead."
            : "That is a MIDI track. Drop audio on an audio track instead.");
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(trackIndex, tracks.size()))
        return juce::Result::fail("Drop audio on an audio track.");

    te::AudioFile audio(engine, file);
    const auto duration = audio.getLength();
    if (duration <= 0.0)
        return juce::Result::fail("This file could not be read as audio.");

    auto* track = tracks[trackIndex];
    const auto start = tracktion::core::TimePosition::fromSeconds(startSeconds);
    edit->getUndoManager().beginNewTransaction("Import audio");
    auto clip = track->insertWaveClip(file.getFileNameWithoutExtension(), file,
        {{start, start + tracktion::core::TimeDuration::fromSeconds(duration)}, {}}, false);
    if (clip == nullptr)
        return juce::Result::fail("The audio clip could not be added.");
    clip->setColour(juce::Colour(0xff4d6975));
    makeRoomForClip(*clip);
    refreshLoop();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

void Session::togglePlayback()
{
    auto& transport = edit->getTransport();
    // A count-in is the transport about to start, so the play button stands in
    // for the record button that began it and calls the whole thing off.
    if (isCountingIn())
    {
        cancelCountIn();
        sendSynchronousChangeMessage();
        return;
    }
    if (transport.isPlaying())
    {
        const auto wasRecording = transport.isRecording();
        transport.stop(false, false);
        releasePlayingNotes();
        if (wasRecording) finishRecording();
    }
    else transport.play(false);
}

void Session::setPlaybackStart(double seconds)
{
    playbackStartSeconds = std::isfinite(seconds) ? std::max(0.0, seconds) : 0.0;
}

void Session::stop()
{
    auto& transport = edit->getTransport();
    cancelCountIn();
    const auto wasRecording = transport.isRecording();
    transport.stop(false, false);
    // Back to the line the arrangement's selection is on rather than to zero,
    // so stopping and playing again repeats the passage that was being worked
    // on. A document nothing has been clicked on still starts at the top.
    transport.setPosition(tracktion::core::TimePosition::fromSeconds(playbackStartSeconds));
    releasePlayingNotes();
    if (wasRecording) finishRecording();
}

void Session::returnToStart()
{
    stop();
    // Stop parks the transport on the line the arrangement is working from;
    // this overrides that for good rather than for one press, so what the next
    // stop does agrees with where the transport already is. A click in the
    // arrangement picks a new line and takes it back.
    playbackStartSeconds = 0.0;
    edit->getTransport().setPosition({});
}

void Session::releasePlayingNotes()
{
    for (auto* track : te::getAudioTracks(*edit))
    {
        releasePluginList(&track->pluginList);
        for (auto* clip : track->getClips())
            releasePluginList(clip->getPluginList());
    }
}

void Session::releaseAudioDevice()
{
    // The preview and the count-in are callbacks on the device that is about
    // to close, so they come off first rather than being left pointing at a
    // shut device.
    cancelCountIn();
    releasePreview();
    te::TransportControl::stopAllTransports(engine, false, true);
    if (edit != nullptr)
    {
        auto& transport = edit->getTransport();
        transport.stop(false, true);
        transport.freePlaybackContext();
    }
    engine.getDeviceManager().deviceManager.closeAudioDevice();
}

double Session::tempo() const { return edit->tempoSequence.getTempo(0)->getBpm(); }

Session::TimeSignature Session::timeSignature() const
{
    if (auto* signature = edit->tempoSequence.getTimeSig(0))
        return {static_cast<int>(signature->numerator), static_cast<int>(signature->denominator)};
    return {};
}

double Session::beatsPerBar() const
{
    const auto signature = timeSignature();
    return signature.numerator * 4.0 / signature.denominator;
}

juce::Result Session::setTimeSignature(int numerator, int denominator)
{
    constexpr std::array validDenominators {1, 2, 4, 8, 16};
    if (numerator < 1 || numerator > 99
        || std::find(validDenominators.begin(), validDenominators.end(), denominator) == validDenominators.end())
        return juce::Result::fail("Time signature must use a numerator from 1 to 99 and denominator 1, 2, 4, 8, or 16.");
    auto* signature = edit->tempoSequence.getTimeSig(0);
    if (signature == nullptr)
        return juce::Result::fail("The project has no initial time signature.");
    if (timeSignature().numerator == numerator && timeSignature().denominator == denominator)
        return juce::Result::ok();
    edit->getUndoManager().beginNewTransaction("Change time signature");
    signature->setStringTimeSig(juce::String(numerator) + "/" + juce::String(denominator));
    edit->tempoSequence.updateTempoData();
    refreshLoop();
    markModified();
    if (edit->getTransport().isPlaying()) edit->restartPlayback();
    edit->getUndoManager().beginNewTransaction();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

bool Session::clickTrackEnabled() const { return edit->clickTrackEnabled; }
bool Session::clickTrackEmphasiseBars() const { return edit->clickTrackEmphasiseBars; }

// Rhino talks about levels in decibels and the engine stores this one as a
// linear gain, which it then clamps to 0.2 - 1.0. Storing the decibels
// directly put every setting the menu offers below that floor, so all three of
// them came out at the same volume. The conversion belongs here, at the one
// boundary between the two units.
float Session::clickTrackGain() const
{
    return juce::Decibels::gainToDecibels(juce::jlimit(0.0001f, 4.0f, edit->clickTrackGain.get()), -60.0f);
}

void Session::setClickTrackEnabled(bool enabled)
{
    if (clickTrackEnabled() == enabled) return;
    edit->clickTrackEnabled = enabled;
    markModified();
    sendSynchronousChangeMessage();
}

void Session::setClickTrackEmphasiseBars(bool enabled)
{
    if (clickTrackEmphasiseBars() == enabled) return;
    edit->clickTrackEmphasiseBars = enabled;
    markModified();
    sendSynchronousChangeMessage();
}

void Session::setClickTrackGain(float gainDb)
{
    gainDb = juce::jlimit(-60.0f, 6.0f, gainDb);
    if (std::abs(clickTrackGain() - gainDb) < 0.001f) return;
    edit->clickTrackGain = juce::Decibels::decibelsToGain(gainDb, -60.0f);
    markModified();
    sendSynchronousChangeMessage();
}

void Session::setTempo(double bpm)
{
    if (!std::isfinite(bpm)) return;
    bpm = juce::jlimit(40.0, 240.0, bpm);
    const auto previousBpm = tempo();
    if (bpm == previousBpm) return;
    edit->getUndoManager().beginNewTransaction("Change tempo");
    edit->tempoSequence.getTempo(0)->setBpm(bpm);
    markModified();
    edit->tempoSequence.updateTempoData();

    // Clips are the engine's to move: it anchors them to beats and rewrites
    // their seconds as the tempo changes, so an edit keeps its musical shape
    // without help. Rhino's own timeline state is not in that snapshot and has
    // to follow by hand, or it drifts out from under the clips it was drawn
    // against. A tempo with one entry scales the whole timeline by one factor.
    const auto scale = previousBpm / bpm;
    for (auto* track : te::getAudioTracks(*edit))
        rescaleAutomationPoints(track->state, scale, &edit->getUndoManager());
    rescaleAutomationPoints(edit->state, scale, &edit->getUndoManager());
    if (manualLoop)
        manualLoopRange = {tracktion::core::TimePosition::fromSeconds(manualLoopRange.getStart().inSeconds() * scale),
                           tracktion::core::TimePosition::fromSeconds(manualLoopRange.getEnd().inSeconds() * scale)};

    refreshLoop();
    edit->getUndoManager().beginNewTransaction();
    sendSynchronousChangeMessage();
}

// Looping is always on, so this always answers: the span dragged on the ruler
// when there is one, and otherwise everything up to the last clip. The readout
// in the control bar asks the same question the transport does, which is why
// the answer lives here rather than being recomputed beside it.
tracktion::core::TimeRange Session::loopRange() const
{
    if (manualLoop)
        return manualLoopRange;

    auto end = tracktion::core::TimePosition::fromSeconds(0.0);
    for (auto* track : te::getAudioTracks(*edit))
        for (auto* clip : track->getClips())
            end = std::max(end, clip->getPosition().time.getEnd());
    if (end <= tracktion::core::TimePosition::fromSeconds(0.0))
        end = edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(beatsPerBar()));
    return {{}, end};
}

void Session::refreshLoop()
{
    edit->getTransport().setLoopRange(loopRange());
    edit->getTransport().looping = true;
}

juce::Result Session::setLoopRange(double startSeconds, double endSeconds)
{
    if (endSeconds < startSeconds)
        std::swap(startSeconds, endSeconds);
    startSeconds = std::max(0.0, startSeconds);
    endSeconds = std::max(startSeconds, endSeconds);
    if (endSeconds - startSeconds < 0.02)
        return juce::Result::fail("Drag a longer span on the ruler to set a loop.");

    manualLoop = true;
    manualLoopRange = {tracktion::core::TimePosition::fromSeconds(startSeconds),
                       tracktion::core::TimePosition::fromSeconds(endSeconds)};
    edit->getTransport().setLoopRange(manualLoopRange);
    edit->getTransport().looping = true;
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

void Session::clearManualLoopRange()
{
    if (!manualLoop)
        return;
    manualLoop = false;
    refreshLoop();
    markModified();
    sendSynchronousChangeMessage();
}
}
