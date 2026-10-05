---
title: One instrument per track
type: decision
summary: A track runs exactly one instrument; a new one replaces the old in place, and nothing may cache an instrument pointer.
tags: [rhino, devices, instruments]
sources: []
updated: 2026-10-05
---

# One instrument per track

## Context

Until commit `16ef43a` (2026-09-14) a track kept every instrument dropped on it and toggled `setEnabled` so that one was audible. It sounded right, but it left dormant plugins behind and made the device chain lie about what the track was. Live and Logic give a track exactly one instrument, with MIDI effects before it and audio effects after it; Live nests racks and Logic groups tracks in Track Stacks rather than stacking instruments. The rule also makes a clip launcher coherent: the instrument belongs to the track, so a slot clip is only note data.

## Decision

`switchTrackInstrument` (`native/src/SessionInternal.cpp`) is the only thing that changes a track's instrument. The replacement goes in at the old instrument's index, after any MIDI effects, so the chain keeps its order ([Device chain order](device-chain-order.md)). The old instrument is removed with its patch and its automation lanes, in the same undo step (commit `cf85e3f`), and the track is renamed after the new one. Clips are untouched, so a pattern survives the swap. It writes nothing about the track's kind, because only a MIDI track is offered an instrument at all ([A track's kind is fixed when it is made](track-kind-fixed-at-creation.md)).

Rejected: keeping the stack and switching with enable flags.

## Consequences

- **Never cache an instrument pointer**: switching deletes the plugin. `Session` dropped its cached synth, wave and drum pointers; ask `trackInstrument`, `Session::patternInstrument` or `patternInstrumentForTrack` ([Hold ids, not pointers](ids-not-pointers.md)).
- An instrument drop creates no clip; clips come from a double-click or Ctrl+A.
- `collapseStackedInstruments` migrates old projects on load, keeping the enabled instrument, or the first, and dropping the lanes of those it removes. It is the kind of accommodation [No backward compatibility for .rhinoedit](no-rhinoedit-back-compat.md) wants deleted when it next gets in the way.
- A preset dropped into a session slot sets the whole track's instrument, because a preset bundles notes with a sound ([Session view (paused)](session-view.md)).

## Pitfall: clips carry instruments the other way

A swap leaves clips alone, but the converse does not hold. Moving a MIDI clip to another track (`Session::editClip`) and pasting one (`pasteClipRegion`, which duplicating also uses) switch the destination to the instrument of the track the clip came from, replacing the destination's instrument and patch in the same undo step. The source is read by `carriedInstrument` (`native/src/SessionInternal.cpp`), which names the source track's instrument by catalog entry, bypassed or not, so every instrument travels. A clip from a track that plays nothing leaves the destination's instrument alone. Until commit `b5d17e0` (2026-10-05) the reader knew only a few instruments and answered 4OSC for the rest, so duplicating a clip on an empty MIDI track installed a 4OSC; `native/src/tests/Arrangement/scenarios/ClipEdits.inc` now covers a bypassed instrument and a lane that plays nothing.

## Related

- [Device chain order](device-chain-order.md)
- [A track's kind is fixed when it is made](track-kind-fixed-at-creation.md)
- [Hold ids, not pointers](ids-not-pointers.md)
- [No backward compatibility for .rhinoedit](no-rhinoedit-back-compat.md)
- [Region editing](region-editing.md)
- [Session view (paused)](session-view.md)
- [Pattern presets](pattern-presets.md)
