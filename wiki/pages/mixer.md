---
title: Mixer
type: component
summary: One mixer, drawn in the arrangement's track cards, with a VolumeAndPanPlugin in each track's channel strip, mute and solo that silence only a track's output, and the master volume for Main; a DJ deck takes its track ahead of the channel strip.
tags: [rhino, mixer, tracks]
sources: []
updated: 2026-10-13
---

# Mixer

Rhino has one mixer. `native/src/model/SessionMixer.cpp` reads and writes it, and the arrangement draws it as a horizontal strip in each track card (`ArrangementSync.cpp`) through the same `Session` methods, so it cannot show a stale level. A DJ deck playing a track takes the chain's output from the hidden DJ Output, which stands where the track's own devices end and its channel strip begins, so the track's trim, fader, pan and mute are the song's alone and the booth's channel strip is the deck's mixer ([DJ view and the booth](dj-view.md)). Until 2026-10-12 a deck played a bounce of the track, which passed through the fader.

## Where each value lives

| Control | Stored as |
| --- | --- |
| Track volume (-60 to +6 dB) and pan | the track's `te::VolumeAndPanPlugin`, in the channel strip after its devices: last on a track added later, ahead of the meter and the Utility on a starter track ([Device chain order](device-chain-order.md)) |
| Mute and solo | the track's own `mute` / `solo` properties, each toggle one undo step |
| Main level and pan | the edit's master volume plugin |

`ensureTrackMixers` adds the fader to any track that lacks one, at the end of the chain so it sits after the devices. It runs from `addTrack`, the starter stack, `groupTracks`, an import that adds a lane, a lane made by dragging a clip below the last lane or by a paste running off the bottom (since commit `8dd3627`; until then such a lane answered "That track has no fader."), and every reopen, which is how tracks from documents older than the mixer got one. A group bus is a track, so it gets the same strip. The same pass gives every track the DJ booth's two hidden ends (`ensureDjPortDevices`): made inside the step that makes the track, or as a document opens before its history begins, they never shift an index an undo recorded ([DJ decks play live through the track's chain](dj-decks-play-live-through-the-track.md)).

## Rules

- **`UtilityDevice` is not the fader.** It is a device in the chain, like Live's Utility. Two bottom-bar gain sliders once drove Utility devices; they read as a second, contradictory mixer and were replaced by the main output level.
- **A fader drag is one undo step and one announcement.** `beginTrackVolumeGesture`/`endTrackVolumeGesture`, and the pan and main equivalents, open a named transaction and bracket the parameter's change gesture, so the engine records one move and undo one entry. Inside the gesture `setTrackVolumeDb` and its siblings announce nothing, the dragged control drawing its own value, and the gesture's end announces once (commit `e7c9554`; [What a change costs the interface](ui-cost-of-a-change.md)).
- **Mute and solo silence only what reaches the song** (since 2026-10-12). `RhinoEngineBehaviour::shouldProcessMutedTracks()` returns true (`SessionInternal.h`), so a muted or solo-excluded track still runs its devices and the engine's muting node after them clears its output; the engine used to skip the whole chain. A DJ deck takes its track from inside the chain, so the song's mute and solo leave the deck playing, which `DjLiveChain.inc` hears through Tracktion's real graph ([Tracktion's hosted device is silent until its start is dispatched](hosted-device-silent-until-dispatched.md)). The price is the CPU a muted track's devices take.
- **Colour carries meaning.** Volume bars are cyan and pan bars red on every card and on the main row; mute darkens its key and solo brightens it, so the two differ by value rather than hue.
- **The main row** has level and pan only, no mute or solo. Its devices and fader reach an exported WAV because the export renders master plugins ([The main track](main-track.md)).

## Pitfall

`appendTrack` creates a track without the engine's default plugins, so every caller that makes a lane must also run `ensureTrackMixers` (and `ensureSceneSlots` and `reconcileTrackGroups`). The drag and paste paths once did not ([Hazards found while seeding the wiki](known-hazards.md)). A lane that skipped it would also lack the DJ ends, and no deck could play it: `bindDjTrack` logs that the track "has no way in for a DJ deck" and leaves the deck unbound.

## Not built

No level meters, sends or return tracks (nothing in `native/src` creates a level-measuring plugin or an aux send, checked 2026-10-03; the starter tracks carry the engine's default level meter, which nothing in Rhino reads, checked 2026-10-12). Meters need a per-track measuring plugin read at a bounded rate, never polled from `paint`.

## Related

- [The main track](main-track.md)
- [DJ view and the booth](dj-view.md)
- [DJ decks play live through the track's chain](dj-decks-play-live-through-the-track.md)
- [Track groups (bus tracks)](track-groups.md)
- [Device chain order](device-chain-order.md)
- [Built-in devices](built-in-devices.md)
