---
title: Device chain order
type: concept
summary: A track's plugin list is its signal chain, ordered MIDI FX, instrument, audio FX; how every add and every drag keeps that order.
tags: [rhino, devices, signal-chain]
sources: []
updated: 2026-10-03
---

# Device chain order

A track's plugin list *is* its signal chain: each device reads what the one in front of it wrote. So order is meaningful and editable. A vocoder followed by [Rhino Tune](rhino-tune.md) tunes the vocoded signal; swap them and the vocoder is handed an already-tuned voice. Long form: *The order of a chain* in `native/README.md`.

## The order

MIDI effects, then the instrument, then audio effects. Behind them, hidden from the Device View, sits the channel strip: Rhino's Utility and the engine's Volume & Pan (`isTrackInfrastructure` in `native/src/SessionDevices.cpp`, which also counts the engine's `volume` and `level` types). Each add path builds the order:

- `switchTrackInstrument` puts an instrument after any leading MIDI effects, at the old instrument's index ([One instrument per track](one-instrument-per-track.md)).
- `addMidiEffectDevice` puts a MIDI effect immediately in front of the instrument.
- An audio effect, a channel-strip facility like Utility included, goes on either kind of track, in front of the first channel-strip plugin (`channelStripInsertIndex`).
- On the main track an effect is appended to the edit's master list, and the engine's `canBeAddedToMaster` refuses the rest ([The main track](main-track.md)). A group bus takes audio effects only.

## Moving a device

The [Device rack and device editors](device-rack.md) starts the drag and `Session::moveDevice` does the move. It is the one device call that speaks in **positions within `deviceSlots`** rather than plugin indices, because the plugin list also holds the hidden channel strip and what is dragged is what is on screen. The destination plugin index is taken from the neighbour the device lands against, never counted, and the plugin leaves the list before rejoining it, so everything behind it has shifted down one. A move that would break the order is refused with a reason ("MIDI FX run before the instrument."). The rule is `chainRank` in `SessionDevices.cpp`, in the model, so every path inherits the refusal.

## Pitfall: a MIDI effect added before the instrument

On a MIDI track with no instrument yet, `addMidiEffectDevice` has nothing to stand in front of and appends the effect at the end of the list, behind the channel strip. An instrument added afterwards goes in at index 0, leaving the MIDI effect behind it, where `chainRank`'s comment says it has no notes left to rewrite. Drag it back in front. (Read from the code on 2026-10-03; no scenario covers this order of drops.)

## Clip effects

An audio effect dropped on an audio clip goes into that clip's own plugin list (`Session::addClipDevice`), not the track's. A warped or reversed clip cannot host effects, and the drop then falls back to the track. Clip effects have no editor yet; the clip shows only an `FX<n>` badge.

## Related

- [Device rack and device editors](device-rack.md)
- [One instrument per track](one-instrument-per-track.md)
- [Rhino Vocoder and sidechains](rhino-vocoder.md)
- [Track kinds: audio and MIDI](track-kinds.md)
- [The main track](main-track.md)
