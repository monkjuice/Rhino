---
title: A track that feeds a bus renders nothing on its own
type: gotcha
summary: Tracktion builds a track whose output names another track only as an input of that track, so a group member rendered alone renders nothing; Bounce to New Track points it at the default output and switches its fader off for the render only, outside the undo history.
tags: [rhino, rendering, groups, tracktion]
sources: []
updated: 2026-10-14
---

# A track that feeds a bus renders nothing on its own

## Symptom

An offline render whose `tracksToDo` names a [group](track-groups.md) member but not its bus produces nothing. The DJ deck's old bounce met it as an immediate "Didn't find any audio to render" ([superseded](dj-bounce-on-a-copy.md)).

## Cause

`createNodeForEdit` (Tracktion at `4536d8a`) skips any track whose output names another track: such a track is built only as an input of its destination's node. A render of the member alone has no node for it. Rendering the whole group works, because the bus is named and builds its members in.

## Workaround

`renderBounce` in `native/src/model/SessionBounce.cpp`, for a track rendered before its mixer ([Bounce to New Track](bounce-to-new-track.md)), sets two properties for the length of the render with a null undo manager, through `ScopedProperty`, which puts the old value back (or removes a property that was absent) when it goes:

- the track's `OUTPUTDEVICES/DEVICE` `name`, set to the default audio output's name, so the track is built as a top-level track;
- its `VolumeAndPanPlugin`'s `enabled`, set false, so the fader and pan stay out of the file.

It works because `TrackOutput` recomputes its destination synchronously when that property changes (no `AsyncUpdater`), and the graph builder passes a disabled plugin through (`createNodeForPlugin`, unless `includeBypassedPlugins`); both read at `4536d8a`. The scopes are declared before the `RenderTask`, so they are restored only after the task has gone.

The writes skip the undo history because a render is not an edit, and they are safe only because the render runs on the message thread, so nothing else edits the document meanwhile. `ClipBounce.inc` checks that a member feeds its bus again after its render, and that a bounced track's fader is switched back on. The DJ bounce did the same on a copy of the document with `setOutputToDefaultDevice`.

## Related

- [Track groups (bus tracks)](track-groups.md)
- [Offline renders that never return](renders-that-never-return.md), where a member naming a deleted bus hangs a render instead
- [A group is an ordinary bus track](group-is-a-bus-track.md)
