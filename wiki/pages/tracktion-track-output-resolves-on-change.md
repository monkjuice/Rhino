---
title: Tracktion resolves a track-to-track output only when its text changes
type: gotcha
summary: Tracktion stores a track's output into another track as the text "track N", N being the destination's position, and resolves it to a track only when that text changes, so after a reorder the text is stale while the routing is right, and writing the same text again resolves nothing; Rhino's reconcileTrackGroups clears an output whose text already reads right over the wrong track before re-pointing it, and every undo and redo re-resolves the members' outputs top-down.
tags: [rhino, groups, tracktion, routing, undo]
sources: []
updated: 2026-10-27
---

# Tracktion resolves a track-to-track output only when its text changes

## Symptom

Met with [nested groups](track-groups.md#nesting) on 2026-10-27 (commit `7965c72`): insert an outer group above a nested one, ungroup the inner group, then delete the inner bus. The inner group's former members were left routed to nothing: `getDestinationTrack()` was no longer the outer bus.

## Cause

Read in `tracktion_TrackOutput.cpp` at the pinned commit `4536d8a`:

- `TrackOutput::setOutputToTrack` writes the destination into `OUTPUTDEVICES/DEVICE`'s `name` as the text `"track N"`, where N is the destination's `getAudioTrackNumber()`, its **position** in the stack, not its id.
- The text is turned into an in-memory `destTrackID` only by `TrackOutput::updateOutput()`, which runs from `valueTreePropertyChanged` when that `name` **changes**. `updateOutput` first unfreezes the destination chain, then resolves `"track N"` by track number, and returns early when the id is unchanged.

So after a reorder the text names whatever now sits at N while the resolved track stays right. In the symptom, inserting the outer group left the members' text as `"track 1"`; ungrouping the inner group re-pointed them to the outer bus, also `"track 1"`. The same text written again is no change, so nothing resolved and they still fed the inner bus, which the delete then removed.

## What Rhino does

- **`reconcileTrackGroups`** (`native/src/model/SessionGroups.cpp`) compares both `getDestinationTrack()` and the stored text (`storedOutputName`) with the expected `"track N"`. If either differs it re-points the track; if the text already reads right over the wrong track, it calls `setOutputToDefaultDevice(false)` first, so `setOutputToTrack` is a real change.
- **`Session::refreshAfterUndoRedo`** (`Session.cpp`): undo puts the text back mid-replay, possibly before the tracks are back in place, so it resolves against the wrong stack. After every undo or redo, `updateOutput()` is called on every track whose output is not the default (`!usesDefaultAudioOut()`, in Rhino the group members), **top-down**, because its recursion check (`feedsInto`) reads the other tracks' resolutions. It is limited to those tracks because for any other output `updateOutput` falls through to `edit.restartPlayback()` and `owner.changed()`.

**Rule: after any reorder, trust neither the stored text nor the resolved destination alone.** Code that re-routes a track must check both, and must force a change when the text it wants is already there.

## Related

- [Track groups (bus tracks)](track-groups.md), whose pitfall *A member's output names its bus by position, not id* is the same mechanism seen from a copied document
- [A track that feeds a bus renders nothing on its own](bus-member-renders-nothing-alone.md): `ScopedStraightOut` relies on the same resolve-on-change
- [A group is an ordinary bus track](group-is-a-bus-track.md)
- [Directories not to read](off-limits-directories.md), for checking an engine body at the pinned commit
