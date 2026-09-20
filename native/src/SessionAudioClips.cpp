#include "SessionInternal.h"
#include <cmath>

// An audio clip's own mix: gain, pan, pitch, the two fades, mute and reverse.
// Every value here is a property of the clip rather than of its track, so it
// moves with the clip, is undone with it, and changes nothing about the other
// clips sharing that lane. Serves AudioClipPanel.

namespace rhino
{

Session::AudioClipMix Session::audioClipMix(te::EditItemID id) const
{
    AudioClipMix mix;
    auto* clip = findAudioClip(id);
    if (clip == nullptr)
        return mix;
    const auto position = clip->getPosition();
    mix.valid = true;
    mix.name = clip->getName();
    mix.sourceFile = clip->getSourceFileReference().getFile();
    mix.startSeconds = position.time.getStart().inSeconds();
    mix.endSeconds = position.time.getEnd().inSeconds();
    mix.offsetSeconds = position.offset.inSeconds();
    mix.sourceLengthSeconds = clip->getSourceLength().inSeconds();
    mix.speedRatio = clip->getSpeedRatio();
    mix.fadeInSeconds = clip->getFadeIn().inSeconds();
    mix.fadeOutSeconds = clip->getFadeOut().inSeconds();
    mix.gainDb = clip->getGainDB();
    mix.pan = clip->getPan();
    mix.pitchSemitones = clip->getPitchChange();
    mix.muted = clip->isMuted();
    mix.reversed = clip->getIsReversed();
    return mix;
}

// A drag is one undo step and one notification. Without the bracket a fader
// pulled across the panel would leave several hundred undo entries behind it
// and rebuild the arrangement's clip cache on every pixel.
void Session::beginAudioClipGesture(const juce::String& actionName)
{
    if (audioClipGestureDepth++ == 0)
        edit->getUndoManager().beginNewTransaction(actionName);
}

void Session::endAudioClipGesture()
{
    if (audioClipGestureDepth <= 0)
        return;
    if (--audioClipGestureDepth > 0)
        return;
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
}

juce::Result Session::applyAudioClipEdit(te::EditItemID id, const juce::String& actionName,
                                         const std::function<void(te::WaveAudioClip&)>& apply)
{
    auto* clip = findAudioClip(id);
    if (clip == nullptr)
        return juce::Result::fail("Select an audio clip first.");
    const auto insideGesture = audioClipGestureDepth > 0;
    if (!insideGesture)
        edit->getUndoManager().beginNewTransaction(actionName);
    apply(*clip);
    if (insideGesture)
        return juce::Result::ok();
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::setAudioClipGainDb(te::EditItemID id, float decibels)
{
    if (!std::isfinite(decibels))
        return juce::Result::fail("Invalid clip gain.");
    const auto clamped = juce::jlimit(minimumClipGainDb, maximumClipGainDb, decibels);
    return applyAudioClipEdit(id, "Clip gain", [clamped](te::WaveAudioClip& clip) { clip.setGainDB(clamped); });
}

juce::Result Session::setAudioClipPan(te::EditItemID id, float pan)
{
    if (!std::isfinite(pan))
        return juce::Result::fail("Invalid clip pan.");
    const auto clamped = juce::jlimit(-1.0f, 1.0f, pan);
    return applyAudioClipEdit(id, "Clip pan", [clamped](te::WaveAudioClip& clip) { clip.setPan(clamped); });
}

// Pitch is a resample rather than a transposition of anything written down, so
// the engine renders a stretched proxy for the clip. It costs nothing until
// the value leaves zero, which is why this is a clip property and not a device.
juce::Result Session::setAudioClipPitch(te::EditItemID id, float semitones)
{
    if (!std::isfinite(semitones))
        return juce::Result::fail("Invalid clip pitch.");
    const auto clamped = juce::jlimit(-maximumClipPitchSemitones, maximumClipPitchSemitones, semitones);
    return applyAudioClipEdit(id, "Clip pitch", [clamped](te::WaveAudioClip& clip) { clip.setPitchChange(clamped); });
}

// The engine keeps the two fades from overlapping: a fade in long enough to
// meet the fade out shortens the other one rather than being refused, so the
// panel reads both back after either is set.
juce::Result Session::setAudioClipFadeIn(te::EditItemID id, double seconds)
{
    if (!std::isfinite(seconds) || seconds < 0.0)
        return juce::Result::fail("Invalid fade length.");
    return applyAudioClipEdit(id, "Clip fade in", [seconds](te::WaveAudioClip& clip)
    {
        clip.setFadeIn(tracktion::core::TimeDuration::fromSeconds(seconds));
    });
}

juce::Result Session::setAudioClipFadeOut(te::EditItemID id, double seconds)
{
    if (!std::isfinite(seconds) || seconds < 0.0)
        return juce::Result::fail("Invalid fade length.");
    return applyAudioClipEdit(id, "Clip fade out", [seconds](te::WaveAudioClip& clip)
    {
        clip.setFadeOut(tracktion::core::TimeDuration::fromSeconds(seconds));
    });
}

// Muting one clip, not its track: the lane keeps playing everything else on it.
juce::Result Session::setAudioClipMuted(te::EditItemID id, bool muted)
{
    return applyAudioClipEdit(id, muted ? "Mute clip" : "Unmute clip",
                              [muted](te::WaveAudioClip& clip) { clip.setMuted(muted); });
}

juce::Result Session::setAudioClipReversed(te::EditItemID id, bool reversed)
{
    return applyAudioClipEdit(id, reversed ? "Reverse clip" : "Play clip forwards",
                              [reversed](te::WaveAudioClip& clip) { clip.setIsReversed(reversed); });
}

}
