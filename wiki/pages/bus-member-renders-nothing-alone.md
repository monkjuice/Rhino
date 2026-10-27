---
title: A track that feeds a bus renders nothing on its own
type: gotcha
summary: Tracktion builds a track whose output names another track only as an input of that track, so a group member rendered alone renders nothing ("Didn't find any audio to render"); every Rhino render of one track alone - Bounce, Ctrl+J merge, Slice to New MIDI Track - wraps itself in ScopedStraightOut, which points the track at the default output for the render only, outside the undo history.
tags: [rhino, rendering, groups, tracktion, merge]
sources: []
updated: 2026-10-27
---

# A track that feeds a bus renders nothing on its own

## Symptom

An offline render whose `tracksToDo` names a [group](track-groups.md) member but not its bus fails at once with "Didn't find any audio to render", written to `rhino.log` and shown in the status line. The DJ deck's old bounce met it first ([superseded](dj-bounce-on-a-copy.md)). Until commit `e000e79` (2026-10-27) Ctrl+J merge and Slice to New MIDI Track failed this way on every grouped track; the user met it merging two clips on a bounced track inside a group.

## Cause

`createNodeForEdit` (`tracktion_EditNodeBuilder.cpp` at `4536d8a`, about lines 2171-2175) skips at top level any track whose output names another track: such a track is built only as an input of its destination's node. A render of the member alone therefore builds nothing, and `NodeRenderContext` (`checkNodesForAudio`, about lines 93-95) finds no audio and gives up. Rendering the whole group works, because the bus is named and builds its members in.

## Workaround: `ScopedStraightOut`

`ScopedStraightOut(te::AudioTrack&)`, declared in `native/src/model/SessionInternal.h` and defined in `SessionInternal.cpp` since `e000e79`, sets the track's `OUTPUTDEVICES/DEVICE` `name` to the default audio output's name for its lifetime, so the track is built as a top-level track. A track already going there is left alone. It writes through `ScopedProperty` (same header; it moved there from `SessionBounce.cpp`'s anonymous namespace), which uses a null undo manager and puts the old value back, or removes a property that was absent, when it goes.

**Make it before the `RenderTask` and destroy it after.** The task builds its graph when it is constructed. In a struct, declare the scope above the task, as `RenderInFlight` does, so the task dies first.

It works because `TrackOutput` recomputes its destination synchronously when that property changes (no `AsyncUpdater`); read at `4536d8a`.

**Rule: any new code that renders one track alone (a single track in `tracksToDo`) must wrap the render in `ScopedStraightOut`, or it renders nothing on a grouped track.**

## Who uses it

- **[Bounce to New Track](bounce-to-new-track.md)**, `RenderInFlight` in `SessionBounce.cpp`, on the source track of **every** job since nested groups (2026-10-27, commit `7965c72`): a plain track rendered before its fader, and a group's bus rendered with its fader on. On a bus that already plays to the main output it does nothing; on a nested group's bus, which feeds the outer bus, it is what makes the render produce audio at all ([Track groups](track-groups.md#nesting)). For a plain track only, it also holds a `ScopedProperty` setting the track's `VolumeAndPanPlugin` `enabled` to false, so the fader and pan stay out of the file; the graph builder passes a disabled plugin through (`createNodeForPlugin`, unless `includeBypassedPlugins`). The writes stay in place across the bounce's slices, safe only because nothing can edit meanwhile ([A long command runs in slices behind a modal screen](long-commands-run-in-slices.md)).
- **Ctrl+J merge**, `SessionMerge.cpp`, per track rendered ([Audio clip editor](audio-clip-editor.md#commands-that-reach-past-it)).
- **[Slice to New MIDI Track](slice-to-new-midi-track.md)**, `SessionSliceToMidi.cpp`.

The merge and slice renders therefore leave out the bus, its fader and its devices, which matches their rule that the track's own devices and fader stay out. Both block the message thread, so nothing edits while the scope is held.

## Tests

`ClipBounce.inc` checks that a member feeds its bus again after its render and that a bounced track's fader is switched back on, and that a nested group bounces at the same level while the outer bus's fader sits at -40 dB. `ClipMerge.inc` merges two clips on a grouped member, checks both are at full level in the file and that the track feeds its bus afterwards. `ClipSliceToMidi.inc` slices a grouped member's clip at its transients (six slices) and checks the same routing afterwards.

## Related

- [Track groups (bus tracks)](track-groups.md)
- [Offline renders that never return](renders-that-never-return.md), where a member naming a deleted bus hangs a render instead
- [A group is an ordinary bus track](group-is-a-bus-track.md)
