---
title: DJ decks play live through the track's chain
type: decision
summary: A deck on a track plays the track's own cells through the track's live devices, by two hidden devices every track carries from its creation, so every edit is heard at once; why the devices are permanent, what it costs, what a review of it fixed, how Tracktion's real graph proves it, and the bounce, the bind-time devices and the message-thread note injection it rejected.
tags: [rhino, dj, decks, devices, real-time, undo]
sources: []
updated: 2026-10-27
---

# DJ decks play live through the track's chain

Branch `feat/dj-view`, 2026-10-12. All six ctest cases passed that day, after the fixes of an independent review, and again, twice, once a scenario drove Tracktion's real graph (below).

## Context

Until then a deck on a track played a bounce: the track rendered to audio on a worker from a copy of the document, and rendered again after an edit it could hear ([superseded](dj-bounce-on-a-copy.md)), with a preview that played the notes live while a knob was held. On 2026-10-11 the user reported four faults, each traced in the old code:

1. Assigning a track to a deck brought the arrangement's MIDI pattern onto it, though the DJ view's clips must be independent of the arrangement: `loadDjDeckTrack` always bounced the arrangement (the SONG cell, `DjDeckInfo::slot` -1).
2. Turning a Drum Rack knob silenced the deck until the knob was let go,
3. and so did a Rhino Arp knob: the preview faded the bounce out and injected the notes into the track, whose output went to the song's output, not the deck's channel strip.
4. Editing Rhino Forge in its own window did not change the deck at all: nothing observed an external plugin's window, so nothing bounced again.

They asked for the DJ architecture to be revisited from scratch and, mid-task, "why do we need a simulation, keep it simple".

## The choice

A deck on a track is heard through that track's chain, live, and plays only the track's cells ([Clip slots](clip-slots.md)), never the arrangement's clips. Every track carries two hidden devices (`native/src/devices/infra/DjTrackPortDevices.*`): **DJ Input** (`DjTrackInjectorDevice`, `rhino.dj.inject.v1`) at plugin index 0, and **DJ Output** (`DjTrackTapDevice`, `rhino.dj.tap.v1`) where the track's own devices end and its channel strip begins. Bound to a deck's `DjTrackPort` (`core/dj/DjTrackPort.*`), the input adds the deck's audio and notes to the chain and the output hands the chain's result to the deck's channel strip. Nothing renders: a knob, a device added, an external plugin's own window is heard at once. Since commit `4a6a17b` (2026-10-27) the input also records which keys the chain's own MIDI is holding, before a deck's notes join it, and the note editor's keyboard lights those keys ([Note editor](note-editor.md#keys-the-track-is-playing)). That reuses this device rather than adding a third hidden one, which would have shifted the raw indices again. How it runs: [DJ view and the booth](dj-view.md), [DJ deck engine](dj-deck-engine.md).

## Why the devices are permanent

`juce::UndoManager` undoes a child insert by the index it recorded. A device put at the head of a chain outside the history when a deck binds would shift every later index by one, and a later Undo would remove the wrong device. So `ensureDjPortDevices` (`SessionInternal.cpp`), run from `Session::ensureTrackMixers`, which every path that makes a track and every document load already call, creates both inside the step that makes the track, or as a document opens before its history begins. Binding then only sets pointers and a flag (`bindDjTrack`), beyond allocating the song's playback context when there is none: no graph rebuild, nothing to strip on save. `DjLiveChain.inc` adds an effect to a bound track and undoes it.

## Costs

- Every raw plugin index is one higher than before: `Session`'s `slot` arguments, `DeviceTarget::slot`, `DeviceSlot::pluginIndex` ([Device chain order](device-chain-order.md)).
- A track deck is a block late, the round trip through the song's graph; file decks are delayed a block to match.
- The deck takes the track before its channel strip, so the song's trim, fader, pan and mute stay the song's.
- A muted track's devices run, and cost CPU: `RhinoEngineBehaviour::shouldProcessMutedTracks()` returns true, so the song's mute and solo silence only a track's output to the song and leave a deck its track. Read at the pinned engine commit `4536d8a`: `AudioTrack::processAudioNodesWhileMuted()` is that flag or `isSidechainSource()`; `PluginNode` skips every plugin of a track whose contents should not be processed unless it is audible; and `createNodeForAudioTrack` ends the chain with a `TrackMutingNode` that clears a muted track's output after its plugins. The real graph bears it out (below).
- While the song's transport rolls, and in every render, both devices pass the block through: the song owns its tracks, and the deck hears nothing.
- One track plays one deck; a second is refused.
- An audio cell plays the raw stretch of its file: the clip's gain, pan, pitch, fades, reverse and warp are not applied (a documented limit, which the review agreed is not a regression).
- A deck moving to another track's audio cell keeps playing its old material, heard through the new track's chain until the worker's read lands; emptying it first stopped a playing deck, and was reverted.
- While a deck holds a track and the song is stopped, the output clears the block before the engine's sidechain send, so the track is no sidechain carrier ([Sidechains](sidechains.md)), and an audio track's monitored input is heard through the deck's channel, not the song's ([Track inputs and monitoring](inputs-and-monitoring.md)).
- It relies on the song's graph processing a track's plugins with the transport stopped, once a playback context exists (`ensureContextAllocated` on bind): play-in-stop. Read at `4536d8a`, `Edit::playInStopEnabled` defaults to true and is backed by the transport's `IDs::endToEnd`, and `TransportControl::performStop` keeps the nodes unless it clears the devices, so the graph goes on running after the song stops. Proven through Tracktion's real graph since 2026-10-12, not only reasoned (below).

## Proven through the real graph

The scenario "dj live: the song's own graph plays a deck's track while the song stands still" (`tests/Arrangement/scenarios/DjLiveChain.inc`) switches its document's engine to Tracktion's hosted audio device and drives the engine's own graph a block at a time beside the booth ([Tracktion's hosted device is silent until its start is dispatched](hosted-device-silent-until-dispatched.md)). With the song stopped the graph runs the track's chain and the deck hears the instrument while the song's output carries none of it; while the song rolls the deck hears nothing; once the song stops again the deck has the track back; a track muted in the song, and another track soloed, still reach the deck; and an ejected deck leaves nothing sounding into the song. The scenarios that play the chain by hand remain, for levels and knobs.

## What the review fixed (2026-10-12)

- **Stuck notes**, the high-severity finding: a deck ejected, removed, moved to another track or overridden by the song left its notes sounding on the old track's instrument. The input now remembers every note it delivered and sends their note-offs, through its reserved-array swap, the moment it stops playing the deck: unbound, the transport rolling, a render, or the port resyncing.
- **A stale block's note-ons**: after the feed overflowed, the reader skipped to the freshest block and delivered note-ons whose note-offs had been dropped with the lost blocks. An overflow now makes the reader empty the feed and resync ([DJ deck engine](dj-deck-engine.md)).
- **The output's place**: put "before the Volume & Pan", it stood ahead of the Utility on starter tracks and after it on added ones, whose channel strips are in different orders ([Device chain order](device-chain-order.md)).
- **A replaced block**: the input replaced the chain's audio with the deck's, so a monitored input went silent; it now adds.
- **A mono chain widened**: the input answered at least two channels, the trap that once made a mono take play from the left speaker only; it now answers its input count, or two at a chain's head with none ([Built-in devices](built-in-devices.md)).
- **The song's mute and solo silenced a deck**: the engine skipped a muted track's whole chain; the engine-behaviour flag above.
- In the model ([DJ view and the booth](dj-view.md)): a second reader left on a port after an undo; a deck playing on raw through the booth after its track was deleted; a re-chosen cell dropping its read in flight; a remake in flight dropped as stale; a time-signature change that remade no cell; Live switched off after a reopen leaving the track monitoring On.

## Rejected

- **Bounce plus a live preview**, the design replaced: an edit reached the deck a render late, a held knob silenced the deck's channel, and an external plugin's window was invisible to it.
- **Adding the devices when a deck binds**: the undo index skew above.
- **`te::AudioTrack::injectLiveMidiMessage` from the message thread** for the notes, as the preview did: a note lands a message-loop turn late and on a block boundary, so timing jitters. The input writes each note at its frame on the audio thread.

## Related

- [DJ view and the booth](dj-view.md)
- [DJ deck engine](dj-deck-engine.md)
- [Device chain order](device-chain-order.md)
- [Real-time audio rules](real-time-audio-rules.md)
- [Mixer](mixer.md)
- [A deck's bounce renders on a worker against a copy of the document](dj-bounce-on-a-copy.md)
