#include "SessionInternal.h"
#include <algorithm>
#include <cmath>

// Time warp: an audio clip that follows the song's tempo rather than playing at
// the speed it was recorded at.
//
// The engine has most of this already and Rhino had none of it wired up.
// AudioClipBase::setAutoTempo makes a clip beat-based, setTimeStretchMode picks
// the algorithm, and WarpTimeManager holds the markers. What this file adds is
// the vocabulary - five warp modes over the stretchers this build ships - a
// clip tempo that rescales the clip rather than silently changing its meaning,
// and marker edits that are single undo steps.
//
// Why the switch and the mode are Rhino's own properties rather than the
// engine's: four of the five modes are auto-tempo with a different stretcher,
// but Repitch is not stretching at all. The engine refuses to leave its
// stretcher disabled while auto-tempo is on - getActualTimeStretchMode
// substitutes the default mode - so a warped Repitch clip cannot be expressed
// as auto-tempo. It is expressed as a speed ratio instead, recomputed whenever
// the song's tempo moves, which is exactly what a record player does and what
// Live's Re-Pitch is. Rhino therefore records what the person asked for and
// derives the engine's state from it, rather than trying to read the answer
// back out of an engine that cannot hold it.
//
// Nothing in this file renders, analyses or touches the audio thread.
// Stretching is the engine's, on its own graph.

namespace rhino
{
// Warping is a property of the clip, so it is saved with the clip. Rhino's
// because the engine has no single flag that covers all five modes.
const juce::Identifier clipWarpOnID {"rhinoWarpOn"};
const juce::Identifier clipWarpModeID {"rhinoWarpMode"};

namespace
{
// Rhino's five modes over the stretchers this build actually ships. The names
// carry the same meaning as Live's, because that is the vocabulary anyone
// coming to this already has, but each one is a distinct algorithm rather than
// a label over the same code: there is no point offering a choice that does
// not change what is heard.
te::TimeStretcher::Mode engineModeFor(Session::WarpMode mode)
{
    switch (mode)
    {
        case Session::WarpMode::repitch: return te::TimeStretcher::disabled;
        case Session::WarpMode::beats:   return te::TimeStretcher::soundtouchNormal;
        case Session::WarpMode::texture: return te::TimeStretcher::soundtouchBetter;
        case Session::WarpMode::tones:   return te::TimeStretcher::signalsmithCheaper;
        case Session::WarpMode::complex: return te::TimeStretcher::signalsmithDefault;
    }
    return te::TimeStretcher::soundtouchNormal;
}

double bpmOf(const te::AudioClipBase& clip)
{
    const auto beats = clip.getLoopInfo().getNumBeats();
    const auto seconds = clip.getSourceLength().inSeconds();
    if (beats <= 0.0 || seconds <= 0.0) return 0.0;
    return beats / seconds * 60.0;
}

Session::WarpMode storedMode(const te::AudioClipBase& clip)
{
    const auto value = static_cast<int>(clip.state.getProperty(clipWarpModeID,
                                                              static_cast<int>(Session::WarpMode::beats)));
    return static_cast<Session::WarpMode>(juce::jlimit(0, Session::warpModeCount - 1, value));
}

bool storedWarpOn(const te::AudioClipBase& clip)
{
    return static_cast<bool>(clip.state.getProperty(clipWarpOnID, false));
}

// How fast a Repitch clip has to run to land on the song's tempo. The limits
// are the engine's own for a speed ratio; past them the clip would be a click.
double repitchSpeedFor(const te::AudioClipBase& clip, double songBpm)
{
    const auto clipBpm = bpmOf(clip);
    if (!(clipBpm > 0.0) || !(songBpm > 0.0)) return 1.0;
    return juce::jlimit(0.1, 10.0, songBpm / clipBpm);
}
}

juce::String Session::warpModeName(WarpMode mode)
{
    switch (mode)
    {
        case WarpMode::repitch: return "Repitch";
        case WarpMode::beats:   return "Beats";
        case WarpMode::tones:   return "Tones";
        case WarpMode::texture: return "Texture";
        case WarpMode::complex: return "Complex";
    }
    return "Beats";
}

juce::String Session::warpModeBlurb(WarpMode mode)
{
    switch (mode)
    {
        case WarpMode::repitch: return "No stretching - speed changes the pitch, like a record player";
        case WarpMode::beats:   return "Drums and loops: keeps the attacks sharp";
        case WarpMode::tones:   return "Melody and voice: holds the pitch steady";
        case WarpMode::texture: return "Pads and ambience: smooth rather than sharp";
        case WarpMode::complex: return "A whole mix or anything busy: the most careful and the most expensive";
    }
    return {};
}

double Session::clipPlaybackSpeed(const te::AudioClipBase& clip) const
{
    if (!storedWarpOn(clip) || storedMode(clip) == WarpMode::repitch)
        return std::max(0.0001, clip.getSpeedRatio());
    const auto clipBpm = bpmOf(clip);
    if (!(clipBpm > 0.0)) return std::max(0.0001, clip.getSpeedRatio());
    return std::max(0.0001, tempo() / clipBpm);
}

Session::ClipWarp Session::clipWarp(te::EditItemID id) const
{
    ClipWarp warp;
    auto* clip = findAudioClip(id);
    if (clip == nullptr)
        return warp;
    warp.valid = true;
    warp.followsTempo = storedWarpOn(*clip);
    warp.markersEnabled = clip->getWarpTime();
    warp.mode = storedMode(*clip);
    warp.clipBpm = bpmOf(*clip);
    warp.beats = clip->getLoopInfo().getNumBeats();
    warp.sourceLengthSeconds = clip->getSourceLength().inSeconds();
    // The manager is created on demand and seeds itself with a marker at each
    // end of the file, so asking for the markers of a clip that has never been
    // warped is what puts those two there. Only asked for once the markers are
    // switched on, so an unwarped clip carries no WARPTIME node at all.
    if (warp.markersEnabled)
    {
        auto& manager = clip->getWarpTimeManager();
        for (const auto* marker : manager.getMarkers())
            if (marker != nullptr)
                warp.markers.push_back({marker->sourceTime.inSeconds(), marker->warpTime.inSeconds()});
        const auto transients = manager.getTransientTimes();
        warp.transientsReady = transients.first;
        for (const auto& transient : transients.second)
            warp.transients.push_back(transient.inSeconds());
    }
    return warp;
}

// A clip whose content is measured in beats has to be as long on the timeline
// as its content is, or a change to that content's length would quietly crop
// it. The engine says how long that is - getMaximumLength follows the tempo
// sequence for a warped clip and the speed ratio for a repitched one - and
// this holds the clip's window into the source at the same fraction of it, so
// a clip someone had already trimmed stays trimmed by the same amount.
void Session::rescaleWarpedClip(te::WaveAudioClip& clip, double previousContentSeconds)
{
    const auto content = clip.getMaximumLength().inSeconds();
    if (!(content > 0.0) || !(previousContentSeconds > 0.0))
        return;
    const auto ratio = content / previousContentSeconds;
    if (!std::isfinite(ratio) || std::abs(ratio - 1.0) < 1.0e-9)
        return;
    const auto position = clip.getPosition();
    const auto start = position.time.getStart().inSeconds();
    const auto length = position.time.getLength().inSeconds();
    const auto offset = position.offset.inSeconds();
    clip.setPosition({{tracktion::core::TimePosition::fromSeconds(start),
                       tracktion::core::TimePosition::fromSeconds(start + length * ratio)},
                      tracktion::core::TimeDuration::fromSeconds(offset * ratio)});
}

// The one place the engine's warp state is written. Everything else here
// records what the person asked for and calls this to make it so, which is
// what keeps the two representations of Repitch - a speed ratio - and of the
// other four - auto-tempo plus a stretcher - from ever being half applied.
void Session::applyWarpState(te::WaveAudioClip& clip, bool on, WarpMode mode)
{
    const auto before = clip.getMaximumLength().inSeconds();
    auto* undoManager = &edit->getUndoManager();
    clip.state.setProperty(clipWarpOnID, on, undoManager);
    clip.state.setProperty(clipWarpModeID, static_cast<int>(mode), undoManager);
    // A clip with no length in beats has nothing to follow the tempo with.
    // setLoopDefaults fills that in from the file and the tempo at the clip's
    // start, which is the same assumption Live makes when it cannot detect a
    // tempo: that the material is already at the song's.
    if (on && clip.getLoopInfo().getNumBeats() <= 0.0)
        clip.setLoopDefaults();
    if (on && mode == WarpMode::repitch)
    {
        // Auto-tempo off first: setSpeedRatio does nothing while it is on,
        // because a beat-based clip has no speed of its own to set.
        clip.setAutoTempo(false);
        clip.setTimeStretchMode(te::TimeStretcher::disabled);
        clip.setSpeedRatio(repitchSpeedFor(clip, tempo()));
    }
    else if (on)
    {
        clip.setSpeedRatio(1.0);
        clip.setAutoTempo(true);
        clip.setTimeStretchMode(engineModeFor(mode));
    }
    else
    {
        clip.setAutoTempo(false);
        clip.setSpeedRatio(1.0);
        clip.setTimeStretchMode(engineModeFor(mode));
    }
    // A warped clip stretches while it plays rather than through a rendered
    // copy of itself. The engine offers both: with proxies on it renders a
    // stretched file per clip and plays that back plainly, which is cheaper
    // per block and wrong for this. Every tempo change and every warp marker
    // drag invalidates that file, so the clip would fall silent or play its
    // last version until the render caught up - and those are exactly the two
    // things someone warping a clip does repeatedly while listening. The cost
    // is one stretcher per warped clip while the transport rolls, which is
    // what Live pays too.
    clip.setUsesProxy(!on);
    rescaleWarpedClip(clip, before);
}

// Repitch is the one mode whose engine state depends on the song's tempo, so
// it is the one that has to be rewritten when the tempo moves. Called from
// setTempo, after the engine has finished moving everything it moves itself.
void Session::updateRepitchedClips()
{
    for (auto* track : te::getAudioTracks(*edit))
        for (auto* clip : track->getClips())
            if (auto* audio = dynamic_cast<te::WaveAudioClip*>(clip))
                if (storedWarpOn(*audio) && storedMode(*audio) == WarpMode::repitch)
                {
                    const auto wanted = repitchSpeedFor(*audio, tempo());
                    if (std::abs(audio->getSpeedRatio() - wanted) < 1.0e-9)
                        continue;
                    const auto before = audio->getMaximumLength().inSeconds();
                    audio->setSpeedRatio(wanted);
                    rescaleWarpedClip(*audio, before);
                }
}

juce::Result Session::setClipFollowsTempo(te::EditItemID id, bool follows)
{
    auto* clip = findAudioClip(id);
    if (clip == nullptr)
        return juce::Result::fail("Select an audio clip first.");
    if (storedWarpOn(*clip) == follows)
        return juce::Result::ok();
    const auto mode = storedMode(*clip);
    return applyAudioClipEdit(id, follows ? "Warp clip" : "Play clip unwarped",
                              [this, follows, mode](te::WaveAudioClip& target)
    {
        applyWarpState(target, follows, mode);
        // Markers describe a warp, so they go with it rather than lying in
        // wait for the next time the switch is turned on.
        if (!follows && target.getWarpTime())
            target.setWarpTime(false);
        // Warping can lengthen the clip, and a clip that grew wins the ground
        // it grew over, as every other clip edit does.
        makeRoomForClip(target);
    });
}

juce::Result Session::setClipWarpMode(te::EditItemID id, WarpMode mode)
{
    auto* clip = findAudioClip(id);
    if (clip == nullptr)
        return juce::Result::fail("Select an audio clip first.");
    const auto on = storedWarpOn(*clip);
    return applyAudioClipEdit(id, "Warp mode", [this, on, mode](te::WaveAudioClip& target)
    {
        applyWarpState(target, on, mode);
        makeRoomForClip(target);
    });
}

juce::Result Session::setClipBpm(te::EditItemID id, double bpm)
{
    if (!std::isfinite(bpm))
        return juce::Result::fail("Invalid clip tempo.");
    auto* clip = findAudioClip(id);
    if (clip == nullptr)
        return juce::Result::fail("Select an audio clip first.");
    const auto seconds = clip->getSourceLength().inSeconds();
    if (!(seconds > 0.0))
        return juce::Result::fail("This clip has no audio to measure a tempo against.");
    const auto clamped = juce::jlimit(minimumClipBpm, maximumClipBpm, bpm);
    return applyAudioClipEdit(id, "Clip tempo", [this, clamped, seconds](te::WaveAudioClip& target)
    {
        const auto before = target.getMaximumLength().inSeconds();
        // Written as beats rather than through LoopInfo::setBpm, which wants
        // an AudioFileInfo to work the beats out from. The two say the same
        // thing and this one cannot disagree with the length we measured.
        target.getLoopInfo().setNumBeats(seconds * clamped / 60.0);
        // A repitched clip's speed is worked out from its tempo, so changing
        // the tempo has to change the speed with it.
        if (storedWarpOn(target) && storedMode(target) == WarpMode::repitch)
            target.setSpeedRatio(repitchSpeedFor(target, tempo()));
        rescaleWarpedClip(target, before);
        // Halving the clip tempo doubles the clip, and a clip that grew wins
        // the ground it grew over, as every other clip edit does.
        makeRoomForClip(target);
    });
}

juce::Result Session::scaleClipBpm(te::EditItemID id, double factor)
{
    if (!std::isfinite(factor) || factor <= 0.0)
        return juce::Result::fail("Invalid tempo change.");
    const auto warp = clipWarp(id);
    if (!warp.valid)
        return juce::Result::fail("Select an audio clip first.");
    if (!(warp.clipBpm > 0.0))
        return juce::Result::fail("This clip has no tempo to halve or double.");
    const auto wanted = warp.clipBpm * factor;
    if (wanted < minimumClipBpm || wanted > maximumClipBpm)
        return juce::Result::fail("A clip tempo of " + juce::String(wanted, 2)
                                  + " is outside " + juce::String(minimumClipBpm, 0) + " to "
                                  + juce::String(maximumClipBpm, 0) + " BPM.");
    return setClipBpm(id, wanted);
}

juce::Result Session::detectClipBpm(te::EditItemID id)
{
    auto* clip = findAudioClip(id);
    if (clip == nullptr)
        return juce::Result::fail("Select an audio clip first.");
    edit->getUndoManager().beginNewTransaction("Detect clip tempo");
    const auto before = clip->getMaximumLength().inSeconds();
    // Blocking, and the engine says so in its own header. It reads the file
    // rather than the audio graph, so it is a message-thread wait on disk and
    // not a stall of anything being played.
    const auto detected = clip->performTempoDetect();
    if (!detected)
    {
        edit->getUndoManager().beginNewTransaction();
        return juce::Result::fail("No steady tempo found in this clip - set it by hand.");
    }
    if (storedWarpOn(*clip) && storedMode(*clip) == WarpMode::repitch)
        clip->setSpeedRatio(repitchSpeedFor(*clip, tempo()));
    rescaleWarpedClip(*clip, before);
    makeRoomForClip(*clip);
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::setClipWarpMarkersEnabled(te::EditItemID id, bool enabled)
{
    auto* clip = findAudioClip(id);
    if (clip == nullptr)
        return juce::Result::fail("Select an audio clip first.");
    if (enabled && !storedWarpOn(*clip))
        return juce::Result::fail("Turn warping on before placing warp markers.");
    // Markers stretch the audio between them, and Repitch does not stretch:
    // it plays the file faster or slower, whole. Saying so is better than
    // offering markers that would do nothing.
    if (enabled && storedMode(*clip) == WarpMode::repitch)
        return juce::Result::fail("Repitch plays the clip faster or slower whole, so it takes no warp markers. "
                                  "Choose another warp mode to place them.");
    return applyAudioClipEdit(id, enabled ? "Warp markers on" : "Warp markers off",
                              [enabled](te::WaveAudioClip& target)
    {
        if (enabled)
        {
            // Asking for the manager is what seeds the two end markers and
            // starts the transient scan, so it has to happen before the switch
            // rather than the first time the panel draws.
            target.getWarpTimeManager();
        }
        target.setWarpTime(enabled);
    });
}

juce::Result Session::addClipWarpMarker(te::EditItemID id, double sourceSeconds)
{
    auto* clip = findAudioClip(id);
    if (clip == nullptr)
        return juce::Result::fail("Select an audio clip first.");
    if (!clip->getWarpTime())
        return juce::Result::fail("Turn warp markers on first.");
    if (!std::isfinite(sourceSeconds))
        return juce::Result::fail("Invalid marker position.");
    auto& manager = clip->getWarpTimeManager();
    const auto length = manager.getSourceLength().inSeconds();
    if (!(length > 0.0))
        return juce::Result::fail("This clip has no audio to place a marker in.");
    sourceSeconds = juce::jlimit(0.0, length, sourceSeconds);
    // A marker dropped near an attack lands on the attack. That is the whole
    // reason the transients are detected, and it is what makes placing them by
    // hand tolerable at any zoom.
    if (const auto transients = manager.getTransientTimes(); transients.first)
    {
        auto best = sourceSeconds;
        auto bestDistance = warpMarkerSnapSeconds;
        for (const auto& transient : transients.second)
            if (const auto distance = std::abs(transient.inSeconds() - sourceSeconds); distance < bestDistance)
            {
                bestDistance = distance;
                best = transient.inSeconds();
            }
        sourceSeconds = best;
    }
    // Two markers at the same moment in the file would make the segment
    // between them zero seconds long, which is a stretch ratio of infinity.
    for (const auto* marker : manager.getMarkers())
        if (marker != nullptr && std::abs(marker->sourceTime.inSeconds() - sourceSeconds) < 1.0e-4)
            return juce::Result::fail("There is already a warp marker there.");
    // Placed where the clip plays that moment today, so adding a marker is not
    // itself an edit to how anything sounds: it only gives the next drag
    // something to pull against.
    const auto warpSeconds = manager.sourceTimeToWarpTime(
        tracktion::core::TimePosition::fromSeconds(sourceSeconds)).inSeconds();
    edit->getUndoManager().beginNewTransaction("Add warp marker");
    manager.insertMarker({tracktion::core::TimePosition::fromSeconds(sourceSeconds),
                          tracktion::core::TimePosition::fromSeconds(warpSeconds)});
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::moveClipWarpMarker(te::EditItemID id, int index, double warpSeconds)
{
    auto* clip = findAudioClip(id);
    if (clip == nullptr)
        return juce::Result::fail("Select an audio clip first.");
    if (!clip->getWarpTime())
        return juce::Result::fail("Turn warp markers on first.");
    if (!std::isfinite(warpSeconds))
        return juce::Result::fail("Invalid marker position.");
    auto& manager = clip->getWarpTimeManager();
    if (!juce::isPositiveAndBelow(index, manager.getMarkers().size()))
        return juce::Result::fail("No warp marker there.");
    const auto insideGesture = audioClipGestureDepth > 0;
    if (!insideGesture)
        edit->getUndoManager().beginNewTransaction("Move warp marker");
    // The engine clamps the move so neither neighbouring segment is squeezed
    // past a tenth or stretched past twenty times, and returns where the
    // marker actually went. Nothing here second-guesses that.
    manager.moveMarker(index, tracktion::core::TimePosition::fromSeconds(std::max(0.0, warpSeconds)));
    if (insideGesture)
        return juce::Result::ok();
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::removeClipWarpMarker(te::EditItemID id, int index)
{
    auto* clip = findAudioClip(id);
    if (clip == nullptr)
        return juce::Result::fail("Select an audio clip first.");
    if (!clip->getWarpTime())
        return juce::Result::fail("Turn warp markers on first.");
    auto& manager = clip->getWarpTimeManager();
    if (!juce::isPositiveAndBelow(index, manager.getMarkers().size()))
        return juce::Result::fail("No warp marker there.");
    edit->getUndoManager().beginNewTransaction("Remove warp marker");
    // The engine straightens the first and last markers rather than removing
    // them, because they are the ends of the file and the mapping needs both.
    manager.removeMarker(index);
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::resetClipWarpMarkers(te::EditItemID id)
{
    auto* clip = findAudioClip(id);
    if (clip == nullptr)
        return juce::Result::fail("Select an audio clip first.");
    if (!clip->getWarpTime())
        return juce::Result::fail("Turn warp markers on first.");
    edit->getUndoManager().beginNewTransaction("Straighten warp markers");
    clip->getWarpTimeManager().removeAllMarkers();
    edit->getUndoManager().beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

}
