#include "Session.h"

namespace theta
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
}

juce::Result Session::importAudio(const juce::File& file)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    te::AudioFile audio(engine, file);
    const auto duration = audio.getLength();
    if (duration <= 0.0)
        return juce::Result::fail("This file could not be read as audio.");

    auto* track = te::getAudioTracks(*edit)[1];
    tracktion::core::TimePosition start;
    for (auto* existing : track->getClips())
        start = std::max(start, existing->getPosition().time.getEnd());
    return importAudioAt(file, 1, start.inSeconds());
}

juce::Result Session::importAudioAt(const juce::File& file, int trackIndex, double startSeconds)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (!std::isfinite(startSeconds) || startSeconds < 0.0)
        return juce::Result::fail("Invalid audio drop position.");
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
    refreshLoop();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

void Session::togglePlayback()
{
    auto& transport = edit->getTransport();
    if (transport.isPlaying())
    {
        transport.stop(false, false);
        releasePlayingNotes();
    }
    else transport.play(false);
}

void Session::stop()
{
    edit->getTransport().stop(false, false);
    edit->getTransport().setPosition({});
    releasePlayingNotes();
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

void Session::setTempo(double bpm)
{
    if (!std::isfinite(bpm)) return;
    bpm = juce::jlimit(40.0, 240.0, bpm);
    if (bpm == tempo()) return;
    edit->getUndoManager().beginNewTransaction("Change tempo");
    edit->tempoSequence.getTempo(0)->setBpm(juce::jlimit(40.0, 240.0, bpm));
    markModified();
    edit->tempoSequence.updateTempoData();
    // Keep this initial editor exactly one bar; MIDI positions remain in beats.
    const auto end = edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(4.0));
    pattern().setLength(end - tracktion::core::TimePosition{}, true);
    refreshLoop();
    edit->getUndoManager().beginNewTransaction();
    sendSynchronousChangeMessage();
}

void Session::refreshLoop()
{
    if (manualLoop)
    {
        edit->getTransport().setLoopRange(manualLoopRange);
        edit->getTransport().looping = true;
        return;
    }

    auto end = tracktion::core::TimePosition::fromSeconds(0.0);
    const auto tracks = te::getAudioTracks(*edit);
    for (auto* track : tracks)
        for (auto* clip : track->getClips())
            end = std::max(end, clip->getPosition().time.getEnd());
    if (end <= tracktion::core::TimePosition::fromSeconds(0.0))
        end = edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(4.0));
    edit->getTransport().setLoopRange({{}, end});
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
