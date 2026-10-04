---
title: Hold ids, not pointers
type: convention
summary: Anything kept beyond one call holds an id or a copy, never a raw engine pointer, because undo, moves, reloads and publishes free the object.
tags: [both, lifetime, undo]
sources: []
updated: 2026-10-03
---

# Hold ids, not pointers

## The rule

Anything kept beyond the current call (a selection, a test variable, an automation lane, a clipboard, a cached table) holds an identifier or a copy, and looks the object up again when it needs it. A pointer lives for one call.

## Why

Tracktion rebuilds its objects from the edit's `ValueTree`, so a raw `te::Clip*` does not survive an undo, a move to another track or a project restore. The intermittent `native_arrangement_workflow` segfault looked like a 1-in-5 race that tracked no code change, and it was exactly this. `AudioClipEditing.inc` cached a clip pointer, `TrackManagement.inc` moved the clip and undid the move, and `GesturesAndPersistence.inc` then dereferenced freed memory. Whether it crashed depended on the state of the heap. The fix was one line: look the clip up by id again.

## Rhino

- UI selection stores `te::EditItemID`s, and code re-fetches with `Session::findClip` or `findAudioClip`. A test scenario that inherits a clip re-fetches it at the top ([Writing Rhino tests](writing-rhino-tests.md)).
- Never cache an instrument pointer, because switching instruments replaces the plugin. Ask `trackInstrument` or `Session::patternInstrument` each time ([One instrument per track](one-instrument-per-track.md)).
- A stored automation lane keeps no copy of its track index, since deleting a track renumbers every track below it. A lane is identified by the track state that holds it plus `{slot, parameter}` ([Track automation](automation.md)).
- `tidyRecordedClips` compares a track's clips with the *ids* held before recording started, because making room for one new clip can delete another.
- The clipboard goes further and holds `ClipSnapshot` values, not ids: the source of a cut is gone before the paste, and an undo can remove it too ([Region editing](region-editing.md)).
- The one long-lived pointer is the note editor's pattern clip, kept beside its id. `Session::pattern()` dereferences it, so `ensureEditablePatternClip` re-resolves it by id after an undo, and `repairPatternClip`, where every path that removes a clip ends, points it at another clip.

## Forge

- Never hold a `const Wavetable*` across `WavetableStore::publish()`. With no audio block in flight, the old table is freed at once. Copy what you need into a vector instead; only a running `processBlock` may hold one ([Forge oscillators and wavetables](forge-oscillators.md)). `SampleStore` works the same way.
- `FxSelector` copies its choices out of the FX type table rather than pointing into it. The type in a slot changes underneath it, and a pointer to the old type's choices is "a dangling read waiting for the next repaint".

## How to apply

Store an `itemID`, an index into something you own, or a copy. Resolve it to a pointer in the function that uses it. If a pointer must persist, keep its id beside it and give it one repair point that every invalidating path calls.

## Related

- [Session, the model](session-model.md)
- [Arrangement view](arrangement-view.md)
- [Recording and the count-in](recording.md)
- [Real-time audio rules](real-time-audio-rules.md)
