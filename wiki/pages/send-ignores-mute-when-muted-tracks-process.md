---
title: A send ignores mute while muted tracks are processed
type: gotcha
summary: With shouldProcessMutedTracks true, Tracktion's default shouldProcessAuxSendWhenTrackIsMuted (true) never silences an aux send, so a muted or solo-excluded track kept feeding its returns; Rhino overrides it, and because the engine reads the answer only when it builds the graph, Session::followReturnSolo rebuilds the graph whenever the answer changes.
tags: [rhino, mixer, returns, tracktion, gotcha]
sources: []
updated: 2026-10-25
---

# A send ignores mute while muted tracks are processed

## What went wrong

Since 2026-10-12, `RhinoEngineBehaviour::shouldProcessMutedTracks()` returns true, so a muted track still runs its chain and a DJ deck keeps its track ([Mixer](mixer.md)). With that set, `TrackMuteState::shouldTrackContentsBeProcessed` is always true. The engine's `EngineBehaviour::shouldProcessAuxSendWhenTrackIsMuted(send)` defaults to true, and with that answer a send never goes silent. So when return tracks arrived (commit `b7bd73c`), a muted track, or one left out by another track's solo, kept feeding its returns, and could still be heard through them.

This was checked at the pinned Tracktion commit `4536d8a`: the `AuxSendNode` gain lambda in `playback/graph/tracktion_AuxSendNode.cpp`, `createPluginNodeForList` in the EditNodeBuilder, `TrackMuteState` in `tracktion_TrackMutingNode.cpp`, and the default in `utilities/tracktion_EngineBehaviour.h`. [Directories not to read](off-limits-directories.md) says how to check such things without reading `native/.deps`.

## The fix

`RhinoEngineBehaviour::shouldProcessAuxSendWhenTrackIsMuted` (`SessionReturns.cpp`) normally answers false, so a send follows whether its track can be heard. While any return is soloed, it answers true for every send whose track is not muted, so the soloed return plays what the tracks send it, including tracks its solo leaves out.

## The trap in the fix

The engine asks this question, and reads the track's mute, only when it builds the graph. `Session::followReturnSolo` therefore calls `edit->restartPlayback()` when "a return is soloed" changes (it keeps the last answer in `returnSoloHeard`), and on any mute change while that holds. It also solo-isolates the returns unless one of them is soloed. Its callers are `toggleTrackMute`, `toggleTrackSolo`, undo and redo (`refreshAfterUndoRedo`), `restoreProject`, and deleting a return. A new path that writes `te::IDs::mute` or `te::IDs::solo` must call it too, or the graph keeps the old answer.

`tests/Arrangement/scenarios/ReturnTracks.inc` measures each case in rendered audio ([Return tracks and sends](return-tracks.md#tests)).

## Related

- [Return tracks and sends](return-tracks.md)
- [Mixer](mixer.md)
- [Session, the model](session-model.md)
- [DJ decks play live through the track's chain](dj-decks-play-live-through-the-track.md)
