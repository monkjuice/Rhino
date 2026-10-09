---
title: Hold ids, not pointers
type: convention
summary: Anything kept beyond one call holds an id or a copy, never a raw engine pointer, because undo, moves, reloads and publishes free the object.
tags: [both, lifetime, undo]
sources: []
updated: 2026-10-09
---

# Hold ids, not pointers

## The rule

Anything kept beyond the current call (a selection, a test variable, an automation lane, a clipboard, a cached table) holds an identifier or a copy, and looks the object up again when it needs it. A pointer lives for one call.

## Why

Tracktion rebuilds its objects from the edit's `ValueTree`, so a raw `te::Clip*` does not survive an undo, a move to another track or a project restore. The intermittent `native_arrangement_workflow` segfault looked like a 1-in-5 race that tracked no code change, and it was exactly this. `AudioClipEditing.inc` cached a clip pointer, `TrackManagement.inc` moved the clip and undid the move, and `GesturesAndPersistence.inc` then dereferenced freed memory. Whether it crashed depended on the state of the heap. The fix was one line: look the clip up by id again.

## Rhino

- UI selection stores `te::EditItemID`s, and code re-fetches with `Session::findClip` or `findAudioClip`. A test scenario that inherits a clip re-fetches it at the top ([Writing Rhino tests](writing-rhino-tests.md)).
- Never cache an instrument pointer, because switching instruments replaces the plugin. Ask `trackInstrument` or `Session::patternInstrument` each time ([One instrument per track](one-instrument-per-track.md)).
- A stored automation lane keeps no copy of its track index, since deleting a track renumbers every track below it, and no copy of its device's slot, since a reorder moves devices. A lane is identified by the track state that holds it plus `{device key, parameter}`; the key is a UUID Rhino writes onto the plugin's state (`rhinoDeviceKey`, commit `cf85e3f`; [Track automation](automation.md)).
- The Drum Rack's own window keeps its rack's `itemID` and finds it again on every change (`DrumRackView::follow`), so a device added in front of it or a track moved leaves it on the same rack, and it closes once the id is gone ([Drum Rack](drum-rack.md)).
- A DJ deck keeps its track's `EditItemID` or its group's id, and `Session::djDeckInfo` resolves the row on every call (`track`, -1 once it is gone), so a deck follows its track through reorders and refuses to bounce one that was deleted. Material reaches the engine by pointer and is replaced whole, so a display keys what it keeps on `DjDeckInfo::generation`, not on the pointer ([DJ view and the booth](dj-view.md)).
- `tidyRecordedClips` compares a track's clips with the *ids* held before recording started, because making room for one new clip can delete another.
- The clipboard goes further and holds `ClipSnapshot` values, not ids: the source of a cut is gone before the paste, and an undo can remove it too ([Region editing](region-editing.md)).
- A view's own cache is no safer than the engine's objects. Any `Session` command announces synchronously and the arrangement rebuilds its clip list in that call, so a gesture that reads a clip after calling `Session` takes a copy of it first. Two that held a reference read freed or wrong entries until commit `2df76f8`.
- The one long-lived pointer is the note editor's pattern clip, kept beside its id. `Session::pattern()` dereferences it, so `repairPatternClip`, which undo, redo, reopening and every path that removes a clip call, finds it again by id or points it at another clip.

## Never keep a plugin alive past its edit

Holding the other way round fails too. A `te::Plugin::Ptr` keeps its plugin alive, and a plugin that outlives the edit it belongs to corrupted the heap (`0xc0000374`) the first time a document was reopened. The automation mirror's `mirroredCurves` is therefore cleared before `restoreProject` and `newProject` replace the edit (commit `fe163c8`). The rack's floating device window, which holds its device, closes when the device leaves the edit and on `editWillChange` (commit `bac4059`).

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
- [Device rack and device editors](device-rack.md)
