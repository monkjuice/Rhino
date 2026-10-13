---
title: One instrument per track
type: decision
summary: A track runs exactly one instrument; a new one replaces the old in place, and nothing may cache an instrument pointer.
tags: [rhino, devices, instruments]
sources: []
updated: 2026-10-13
---

# One instrument per track

## Context

Until commit `16ef43a` (2026-09-14) a track kept every instrument dropped on it and toggled `setEnabled` so that one was audible. It sounded right, but it left dormant plugins behind and made the device chain lie about what the track was. Live and Logic give a track exactly one instrument, with MIDI effects before it and audio effects after it; Live nests racks and Logic groups tracks in Track Stacks rather than stacking instruments. The rule also keeps a track's cells coherent: the instrument belongs to the track, so a cell's clip is only note data, played through the track's chain.

## Decision

`switchTrackInstrument` (`native/src/model/SessionInternal.cpp`) is the only thing that changes a track's instrument. The replacement goes in at the old instrument's index, after any MIDI effects, so the chain keeps its order ([Device chain order](device-chain-order.md)). The old instrument is removed with its patch and its automation lanes, in the same undo step (commit `cf85e3f`), and the track is renamed after the new one. Clips are untouched, so a pattern survives the swap. It writes nothing about the track's kind, because only a MIDI track is offered an instrument at all ([A track's kind is fixed when it is made](track-kind-fixed-at-creation.md)).

Rejected: keeping the stack and switching with enable flags.

## Consequences

- **Never cache an instrument pointer**: switching deletes the plugin. `Session` dropped its cached synth, wave and drum pointers; ask `trackInstrument`, `Session::patternInstrument` or `patternInstrumentForTrack` ([Hold ids, not pointers](ids-not-pointers.md)).
- An instrument drop creates no clip; clips come from a double-click or Ctrl+A.
- `collapseStackedInstruments` migrates old projects on load, keeping the enabled instrument, or the first, and dropping the lanes of those it removes. It is the kind of accommodation [No backward compatibility for .rhinoedit](no-rhinoedit-back-compat.md) wants deleted when it next gets in the way.
- A pattern preset dropped on a cell of a DJ console's clips grid sets the whole track's instrument, because a preset bundles notes with a sound ([Clip slots, the DJ consoles' cells](clip-slots.md)).

## Pitfall: clips carry instruments the other way

A swap leaves clips alone, but the converse does not hold. Moving a MIDI clip to another track (`Session::editClip`) and pasting one (`pasteClipRegion`, which duplicating also uses) switch the destination to the instrument of the track the clip came from, replacing the destination's instrument and patch in the same undo step. The source is read by `carriedInstrument` (`native/src/model/SessionInternal.cpp`), which names the source track's instrument by catalog entry, bypassed or not, so every instrument travels. A clip from a track that plays nothing leaves the destination's instrument alone. Until commit `b5d17e0` (2026-10-05) the reader knew only a few instruments and answered 4OSC for the rest, so duplicating a clip on an empty MIDI track installed a 4OSC; `native/src/tests/Arrangement/scenarios/ClipEdits.inc` now covers a bypassed instrument and a lane that plays nothing.

A drum clip carries its sounds as well. A Drum Rack that arrives through the switch is blank, so it takes the source rack's kit (`drumKitOf` and `fillBlankDrumRack` in `SessionInternal.h`). A paste carries the kit as XML in `ClipSnapshot::drumKit`, taken at copy time like the rest of the clipboard snapshot. A destination rack that already holds sounds keeps them ([Drum Rack](drum-rack.md)).

The switch says what it did in an `InstrumentSwitch`: `devices` when the chain changed - an instrument made, another removed, the one kept switched back on - and `renamed` when only the track's name followed the instrument; `fillBlankDrumRack` returns whether the rack took the kit. A clip landing on a track that already runs its instrument, as a duplicate on its own track always does, changes no device there, so the move, paste and duplicate mark the destination's `devices` only when one of the two changed something (since 2026-10-11; [DJ view and the booth](dj-view.md)). An instrument dropped on a track already running it is read the same way (`Session::addInstrumentDevice`): no device changed, so a preset dropped with it marks `parameters` and a name that followed it `none`. The rename still happens on such a landing: Ctrl+D on a clip of a track named after its kit renames the track `Drum Rack`, and a name given to a synth track gives way to the synth's (noted on 2026-10-11, not changed).

## Related

- [Device chain order](device-chain-order.md)
- [A track's kind is fixed when it is made](track-kind-fixed-at-creation.md)
- [Hold ids, not pointers](ids-not-pointers.md)
- [No backward compatibility for .rhinoedit](no-rhinoedit-back-compat.md)
- [Region editing](region-editing.md)
- [Clip slots, the DJ consoles' cells](clip-slots.md)
- [Pattern presets](pattern-presets.md)
