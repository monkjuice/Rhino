---
title: Session view (paused)
type: component
summary: A working clip launcher, built and tested but switched off in the shell, whose scene and slot model still runs in every project.
tags: [rhino, session-view, clips, launching]
sources: []
updated: 2026-10-03
---

# Session view (paused)

The session view is Rhino's clip launcher: tracks as columns, scenes as rows, every cell a clip slot. It is built and tested but unreachable: `static constexpr bool sessionViewEnabled = false;` in `native/src/Main.cpp` hides the Session/Arrange switch, the Tab shortcut and *Back to Arrangement*. `SESSION-VIEW.md` is the long form and the resume guide; read it before touching any of this.

## What still runs

Only the shell is switched off. `Session::ensureSceneSlots` gives every project at least eight scenes (`defaultScenes`) and every track a slot per scene, so documents carry slots whether or not anyone can see them, and the `SessionView` component is still constructed inside `ControlWindow`, just never shown. Model: `SessionSlots.cpp`. UI: `SessionView.cpp`, `SessionViewPainter.cpp`, `SessionViewGestures.cpp`. Tests: `SessionView.inc`, `SharedMixer.inc`, `ClipRoundTrip.inc`.

## How launching works

Launching is Tracktion's. `Session` only queues `LaunchHandle` play and stop from the message thread for the next quantised beat (`nextLaunchBeat`; immediate before a playback context exists). The audio thread decides when a slot starts and raises the track's `playSlotClips` flag, which makes the track ignore its timeline clips; `returnToArrangement` clears it. The engine sets that flag without broadcasting, so the shell polls `anyTrackPlayingSlots` on its 30 Hz timer. Two rules are Rhino's: a track plays one slot clip at a time, and launching a scene stops tracks whose slot in that row is empty.

## Two presentations, one project

Tracks, devices, the [Mixer](mixer.md) and the transport are shared because both views read one `Session`; selection, focus, scroll and zoom are per view. Clips are separate, as in Live, and the only routes across copy: `copySlotClipToArrangement` and `copyClipToSlot` re-reference a wave clip's file, clone a MIDI sequence, and set or drop the loop to suit the destination.

## Before turning it back on

- The note editor cannot open a slot clip: `findClip` searches track clip lists, not `ClipSlot`s (`SESSION-VIEW.md`, gap 2).
- The slot paths predate fixed track kinds. `insertDeviceClipInSlot` calls `switchTrackInstrument` directly, and `insertAudioFileInSlot` and `copySlotClipToArrangement` never ask `trackType`, so the refusals of [A track's kind is fixed when it is made](track-kind-fixed-at-creation.md) do not hold there (checked 2026-10-03).
- The arrangement's clip menu still offers *Copy to session slot* while the view is off, so a clip can be copied into a slot nobody can see.
- `SESSION-VIEW.md` is stale twice: its intermittent `native_arrangement_workflow` failure was a stale clip pointer, since fixed (`HANDOVER.md`); and the arrangement now renames and colours tracks, so that gap is the session view's alone.
- Still missing: per-track Back to Arrangement, dragging between views, Arrangement Record, meters, sends and returns, follow actions, recording into slots, scene rename.

## Related

- [Mixer](mixer.md)
- [Clips never overlap](clip-placement.md)
- [Arrangement view](arrangement-view.md)
- [One instrument per track](one-instrument-per-track.md)
- [Hold ids, not pointers](ids-not-pointers.md)
