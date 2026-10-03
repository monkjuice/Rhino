---
title: Mixer
type: component
summary: One mixer serves both views, with a VolumeAndPanPlugin ending each chain, mute and solo on the track, and the master volume for Main.
tags: [rhino, mixer, tracks]
sources: []
updated: 2026-10-03
---

# Mixer

Rhino has one mixer, not one per view. `native/src/SessionMixer.cpp` reads and writes it; the arrangement draws it as a horizontal strip in each track card (`ArrangementSync.cpp`) and the paused session view as a vertical strip per column (`SessionView.cpp`). Both call the same `Session` methods, so neither can show a stale level.

## Where each value lives

| Control | Stored as |
| --- | --- |
| Track volume (-60 to +6 dB) and pan | the track's `te::VolumeAndPanPlugin`, last in its plugin list |
| Mute and solo | the track's own `mute` / `solo` properties, each toggle one undo step |
| Main level and pan | the edit's master volume plugin |

`ensureTrackMixers` adds the fader to any track that lacks one, at the end of the chain so it sits after the devices. It runs from `addTrack`, the starter stack, `groupTracks` and every reopen, which is how tracks from documents older than the mixer got one. A group bus is a track, so it gets the same strip.

## Rules

- **`UtilityDevice` is not the fader.** It is a device in the chain, like Live's Utility. Two bottom-bar gain sliders once drove Utility devices; they read as a second, contradictory mixer and were replaced by the main output level (`SESSION-VIEW.md`, *Design decisions worth keeping*).
- **A fader drag is one undo step.** `beginTrackVolumeGesture`/`endTrackVolumeGesture`, and the pan and main equivalents, open a named transaction and bracket the parameter's change gesture, so the engine records one move and undo one entry.
- **Colour carries meaning.** Volume bars are cyan and pan bars red on every card and on the main row; mute darkens its key and solo brightens it, so the two differ by value rather than hue.
- **The main row** has level and pan only, no mute or solo. Its devices and fader reach an exported WAV because the export renders master plugins ([The main track](main-track.md)).

## Pitfall

A lane made by dragging a clip below the last lane, or by a paste that runs off the bottom, comes from `appendTrack`, which creates the track without the engine's default plugins, and neither caller runs `ensureTrackMixers`. Until the next `addTrack`, grouping or reopen, `setTrackVolumeDb` on that track fails with "That track has no fader." (traced in the code, 2026-10-03; [Hazards found while seeding the wiki](known-hazards.md)).

## Not built

No level meters, sends or return tracks (nothing in `native/src` creates a level-measuring plugin or an aux send, checked 2026-10-03). `SESSION-VIEW.md` notes that meters need a per-track measuring plugin read at a bounded rate, never polled from `paint`.

## Related

- [The main track](main-track.md)
- [Session view (paused)](session-view.md)
- [Track groups (bus tracks)](track-groups.md)
- [Device chain order](device-chain-order.md)
- [Built-in devices](built-in-devices.md)
