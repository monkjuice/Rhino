---
title: Recording and the count-in
type: component
summary: Arms tracks, not inputs, lets each take win the ground it lands on, and counts in with Rhino's own click while the playhead stands still.
tags: [rhino, recording, audio-io]
sources: []
updated: 2026-10-05
---

# Recording and the count-in

The model is `native/src/SessionRecording.cpp`, the count-in click `native/src/CountInClick.*`, the record button `Main.cpp` and the record dot on each card `ArrangementSync.cpp`.

## A track is armed, not an input

The arm flag is `rhinoArmed` on the track's state, so it is saved and follows the track when reordered (no undo transaction). What a track records follows its kind: `Session::trackRecordInput` answers MIDI for a MIDI track, audio for an audio track, nothing for a group bus or the main row ([Track kinds](track-kinds.md)).

`applyRecordArming` rebuilds the engine's input destinations from those flags, clearing all and re-adding rather than diffing, because a stale destination records into the wrong track. Two lessons live there:

- `InputDeviceInstance::setTarget` is called with its `move` flag false. With it true, each arm cleared every other destination and only the last armed track recorded.
- MIDI inputs replace rather than merge, so a take over a part is a new part.
- Never rebuild under a running take, which can cut the tracks capturing. Arming, a monitoring change or a MIDI device rescan during a take is kept (`armingDeferred`) and applied when the take finishes (commit `23de20c`). A count-in comes before the take, so a track armed during one is armed for it.

A track monitoring On is routed with `recordEnabled` false: heard, not written ([Track inputs and monitoring](inputs-and-monitoring.md)).

## The take

Recording lifts the loop, since a wrapped take would land on the one just played. The engine writes the clip only when the transport stops, on top of whatever was there, so `tidyRecordedClips` diffs clip ids against those present before and runs `makeRoomForClip` on each newcomer ([Clips never overlap](clip-placement.md)). It diffs ids, not pointers, because making room for one clip can delete another.

The engine broadcasts neither start nor end, so the shell polls `Session::recordingStopped` on its 30 Hz timer; `recordingStarted` covers the gap before the audio thread begins. A MIDI take is drawn while played, from the engine's per-track note fifo (`pollRecordingNotes`).

`RhinoEngineBehaviour` (`SessionInternal.h`) names take files `<track> Take <n>.wav`, in a `<project> Recordings` folder beside a saved project or `%APPDATA%\Rhino\Recordings` while untitled (the engine's default dropped untitled takes in the working directory), and mutes what a recording covers. A mono take plays from both speakers only because `UtilityDevice` answers `getNumOutputChannelsGivenInputs` with its input count.

## The count-in

Tracktion counts in by rolling the playhead backwards, which caps it at two bars and fails at bar one. `CountInClick` instead plays a decaying sine per beat through a second `AudioIODeviceCallback`, attached only while counting, for 1-4 bars (`rhinoCountInBars`), from a standstill only. When it ends the transport starts from the message thread, so the downbeat follows within about a block.

**Pitfalls:** JUCE hands a second callback a scratch buffer still holding its previous block, so the click clears it before mixing; otherwise it summed into a growing tone (commit `775b112`). And `Session::clickTrackGain` converts decibels to the engine's linear gain (clamped to 0.2-1.0); written raw, all three menu levels sounded alike.

Not supported: punch ranges, loop takes, comping.

## Related

- [Track inputs and monitoring](inputs-and-monitoring.md)
- [Computer MIDI keyboard](computer-keyboard.md)
- [Clips never overlap](clip-placement.md)
- [App shell and control bar](app-shell.md)
- [Real-time audio rules](real-time-audio-rules.md)
- [Transport, tempo and loop](transport.md)
