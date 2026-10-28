---
title: Tracktion re-sorts the edit through the undo manager a message later
type: gotcha
summary: After a clip or track edit, Tracktion re-sorts a clip owner's or the edit's children through the edit's undo manager on a later message, and a cell's clip makes its follow actions the same way when the graph is next built; Tracktion's 350 ms undo timer folds these into the user's step, but Rhino closes its steps itself, so each became an unnamed step that the next Ctrl+Z undid alone, until closeUndoStep began doing the sorts first (2026-10-28).
tags: [rhino, tracktion, undo, gotcha]
sources: []
updated: 2026-10-28
---

# Tracktion re-sorts the edit through the undo manager a message later

## What the engine writes, and when

At the pinned Tracktion commit `4536d8a`, three things write to the edit's state through its undo manager after the command that caused them has returned:

- **`te::ClipOwner`'s private `ClipList::sortClips`** orders a clip owner's children, on a track or in a clip slot. Automation tracks come first, then clips by start, then `PLUGIN`s, `OUTPUTDEVICES` and `LFOS`. It runs after a clip is added, removed or reordered, or after a clip's `start` or `length` changes. It is skipped while the edit loads or undoes.
- **`te::TrackList::sortTracksByType`**, called from `TrackList::handleAsyncUpdate` after a track is added, orders the edit's children by type, with the tracks last.
- **`te::MidiClip::getFollowActions`** makes the clip's `FOLLOWACTIONS` child the first time anything asks for it. The playback graph asks for a cell's clip the next time it is built (`createNodeForLauncherClips`).

## Why it broke Rhino's undo

Tracktion ends undo steps with `Edit::UndoTransactionTimer`, 350 ms after the last change, so a sort normally lands inside the user's step, which is still open ([Undo and the history](undo-history.md#a-gesture-holds-tracktions-undo-timer-off)). Rhino closes each command's step at once. The sort arrived a message later, so it became an unnamed step of its own. Until commit `8d2d883` (2026-10-28) this caused three problems:

- After nearly every clip or track edit, the first Ctrl+Z undid only the invisible reorder, so the user had to press it again. This hit create, import, split, paste, duplicate, merge, slice, group, duplicate or paste a track, and bounce.
- The history held half as many of the user's actions as it seemed to.
- A new document began with an unnamed step of ten actions: the sort of the starter stack.

The test runners saw none of this, because they dispatch no message loop, so the sorts never ran.

## What Rhino does

- **`Session::closeUndoStep` runs `sortAsTheEngineWill` before it closes the step.** An `EngineOrderWatcher` follows the engine's triggers. It is a `ValueTree::Listener` on `edit->state` that notes each clip owner that changed and whether a track arrived.
- **The sort writes inside the open step.** It runs `te::TrackList::sortTracksByType` on the edit when a track arrived. It sorts each noted owner, stably, by `EngineClipOrder` in `Session.cpp`, Rhino's copy of the private sorter. It asks each cell's clip that lacks `FOLLOWACTIONS` to make them. The engine's own pass then finds nothing to move and writes nothing.
- **A new or opened document is sorted before its history starts.** `buildStarterEdit` and `restoreProject` run the same sort with no undo manager before `clearUndoHistory`. `restoreProject` also asks every cell's clip for its follow actions.

**`EngineClipOrder` must match the engine's sorter exactly.** Otherwise the engine's pass still moves something, and the unnamed step comes back. The copy is pinned to `4536d8a`, so check it against `tracktion_ClipOwner.cpp` when Tracktion moves ([Directories not to read](off-limits-directories.md)). `UndoSteps.inc` requires a created clip, an import, a split, a duplicated track and a cell's clip each to be one step after the message loop has run, so it fails if the copy drifts.

The engine functions were named from stacks captured in a Release build ([Naming a Release stack with a linker map](naming-a-release-stack-with-a-linker-map.md)).

## Related

- [Undo and the history](undo-history.md)
- [Session, the model](session-model.md)
- [Naming a Release stack with a linker map](naming-a-release-stack-with-a-linker-map.md)
- [Clip slots, the DJ consoles' cells](clip-slots.md)
- [Tracktion resolves a track-to-track output only when its text changes](tracktion-track-output-resolves-on-change.md)
