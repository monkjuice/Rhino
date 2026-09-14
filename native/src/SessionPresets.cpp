#include "SessionInternal.h"
#include <algorithm>
#include <set>

// Pattern presets and instrument selection.

namespace theta
{

void Session::clearPattern()
{
    if (pattern().getSequence().getNumNotes() == 0) return;
    edit->getUndoManager().beginNewTransaction("Clear pattern");
    pattern().getSequence().removeAllNotes(&edit->getUndoManager());
    pattern().state.setProperty(starterPlaceholderID, true, &edit->getUndoManager());
    markModified();
    edit->getUndoManager().beginNewTransaction();
    sendSynchronousChangeMessage();
}

void Session::applyPatternPreset(PatternPreset preset)
{
    const auto data = presetPattern(preset);
    edit->getUndoManager().beginNewTransaction("Load " + data.name);
    if (data.useThetaWave)
    {
        bool instrumentChanged = false;
        juce::ignoreUnused(switchTrackInstrument(*edit, *te::getAudioTracks(*edit)[0], Instrument::ThetaWave, instrumentChanged));
        edit->state.setProperty("thetaPatternInstrument", "wave", &edit->getUndoManager());
    }
    else
    {
        setPatternInstrument(data.useDrums);
    }
    if (data.synthPatch != SynthPatch::Default)
        if (auto* fourOsc = findFourOsc(*te::getAudioTracks(*edit)[0]))
            applySynthPatch(data.synthPatch, *fourOsc, edit->getUndoManager());
    if (data.useThetaWave)
        if (auto* wave = findThetaWave(*te::getAudioTracks(*edit)[0]))
            applyThetaWavePatch(preset, *wave);
    fillMidiClip(pattern(), data, edit->getUndoManager());
    markModified();
    edit->getUndoManager().beginNewTransaction();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
}

juce::Result Session::insertPatternPreset(PatternPreset preset, int trackIndex, double startSeconds)
{
    if (!std::isfinite(startSeconds) || startSeconds < 0.0)
        return juce::Result::fail("Invalid pattern drop position.");
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(trackIndex, tracks.size()))
        return juce::Result::fail("Drop clips on a track lane.");
    const auto data = presetPattern(preset);
    const auto start = tracktion::core::TimePosition::fromSeconds(startSeconds);
    const auto startBeat = edit->tempoSequence.toBeats(start).inBeats();
    const auto end = edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(startBeat + beatsPerBar()));
    edit->getUndoManager().beginNewTransaction("Add " + data.name);
    auto* track = tracks[trackIndex];
    if (trackIndex == 0)
    {
        if (data.useThetaWave)
        {
            bool instrumentChanged = false;
            const auto result = switchTrackInstrument(*edit, *track, Instrument::ThetaWave, instrumentChanged);
            if (result.failed())
                return result;
            edit->state.setProperty("thetaPatternInstrument", "wave", &edit->getUndoManager());
        }
        else
        {
            setPatternInstrument(data.useDrums);
        }
    }
    else
    {
        bool instrumentChanged = false;
        const auto result = switchTrackInstrument(*edit, *track,
                                                  data.useDrums ? Instrument::Drums
                                                      : data.useThetaWave ? Instrument::ThetaWave
                                                      : Instrument::FourOsc,
                                                  instrumentChanged);
        if (result.failed())
            return result;
    }
    if (data.synthPatch != SynthPatch::Default)
        if (auto* fourOsc = findFourOsc(*track))
            applySynthPatch(data.synthPatch, *fourOsc, edit->getUndoManager());
    if (data.useThetaWave)
        if (auto* wave = findThetaWave(*track))
            applyThetaWavePatch(preset, *wave);
    auto clip = track->insertMIDIClip(data.name, {start, end}, nullptr);
    if (clip == nullptr)
        return juce::Result::fail("The pattern clip could not be added.");
    clip->setColour(presetColour(preset));
    fillMidiClip(*clip, data, edit->getUndoManager());
    refreshLoop();
    markModified();
    edit->getUndoManager().beginNewTransaction();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::insertInstrumentClip(Instrument instrument, int trackIndex, double startSeconds)
{
    if (!std::isfinite(startSeconds) || startSeconds < 0.0)
        return juce::Result::fail("Invalid instrument drop position.");
    if (instrument == Instrument::Utility)
        return addInstrument(instrument, trackIndex);

    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(trackIndex, tracks.size()))
        return juce::Result::fail("Drop instruments on a track lane.");

    const auto useDrums = instrument == Instrument::Drums;
    const auto name = useDrums ? juce::String("Theta Drums")
        : instrument == Instrument::ThetaWave ? juce::String("Theta Wave")
        : instrument == Instrument::ThetaForge ? juce::String("Theta Forge")
        : juce::String("4OSC synth");
    const auto start = tracktion::core::TimePosition::fromSeconds(startSeconds);
    const auto startBeat = edit->tempoSequence.toBeats(start).inBeats();
    const auto end = edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(startBeat + beatsPerBar()));
    auto* track = tracks[trackIndex];

    edit->getUndoManager().beginNewTransaction("Add " + name);
    bool instrumentChanged = false;
    const auto result = switchTrackInstrument(*edit, *track, instrument, instrumentChanged,
                                              forgeDescription ? &*forgeDescription : nullptr);
    if (result.failed())
        return result;
    if (trackIndex == 0)
        edit->state.setProperty("thetaPatternInstrument",
                                useDrums ? "drums"
                                    : instrument == Instrument::ThetaWave ? "wave"
                                    : instrument == Instrument::ThetaForge ? "forge" : "synth",
                                &edit->getUndoManager());

    auto clip = track->insertMIDIClip(name, {start, end}, nullptr);
    if (clip == nullptr)
        return juce::Result::fail("The instrument clip could not be added.");
    clip->setColour(instrumentColour(instrument));
    patternClip = clip.get();
    patternClipID = patternClip->itemID;
    refreshLoop();
    markModified();
    edit->getUndoManager().beginNewTransaction();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

void Session::setPatternInstrument(bool useDrums)
{
    if (synth) synth->setEnabled(!useDrums);
    if (thetaWave) thetaWave->setEnabled(false);
    if (drums) drums->setEnabled(useDrums);
    edit->state.setProperty("thetaPatternInstrument", useDrums ? "drums" : "synth", &edit->getUndoManager());
}

bool Session::isPatternDrums() const
{
    auto* track = patternClip != nullptr ? patternClip->getClipTrack() : nullptr;
    if (track == nullptr) return drums != nullptr && drums->isEnabled();
    auto* audioTrack = dynamic_cast<te::AudioTrack*>(track);
    if (audioTrack == nullptr) return false;
    if (auto* drumDevice = findDrumDevice(*audioTrack))
        return drumDevice->isEnabled();
    return false;
}

juce::Result Session::selectPatternClip(te::EditItemID id)
{
    auto* midi = dynamic_cast<te::MidiClip*>(findClip(id));
    if (midi == nullptr) return juce::Result::fail("Select a MIDI clip to edit notes.");
    patternClip = midi;
    patternClipID = id;
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

}
