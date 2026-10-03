---
title: Transport, tempo and loop
type: component
summary: Play and stop, the loop that is always on, the single tempo and the time signature, and what Rhino rescales by hand when the tempo moves.
tags: [rhino, transport, tempo]
sources: []
updated: 2026-10-03
---

# Transport, tempo and loop

`native/src/SessionTransport.cpp` holds the transport commands, the tempo, the time signature, the metronome settings
and the loop. Tracktion's `TransportControl` does the playing; the control bar's drag fields are `ControlBarFields.*`
and its readout is `TransportDisplay.*` ([App shell and control bar](app-shell.md)).

## Stop goes back to the line, not to the top

`Session::stop` returns the transport to `playbackStartSeconds`, the selection line the arrangement last put down
(`setPlaybackStart`, see [Arrangement view](arrangement-view.md)), so stopping and playing again repeats the passage
being worked on. `returnToStart` — a double-click on Stop, or the rewind glyph — goes to zero and keeps it there until
the next click in the arrangement. Stopping also sends a MIDI panic to every plugin list (`releasePlayingNotes`) and
finishes a recording that was running. Pressing Play during a count-in calls the count-in off.

## The loop is always on

`refreshLoop` sets `looping = true` every time it runs. The range (`loopRange()`) is the span dragged on the ruler's
loop band when there is one — `setLoopRange` refuses anything under 0.02 s, and a right-click on the band clears it —
and otherwise everything up to the end of the last clip, or one bar in an empty document. The readout asks the same
function as the transport, and the clip-changing paths in `Session*.cpp` call `refreshLoop` after they edit. Recording
lifts the loop for its duration, so a take cannot wrap over itself ([Recording and the count-in](recording.md)).

The dragged span lives on `Session`, not in the document: it is not saved with the project, and as of 2026-10-03
`restoreProject` does not clear it, so a span dragged in one document stays in force after opening another.
`newProject` does clear it.

## One tempo, and what follows it by hand

The edit has a single tempo entry, clamped to 40-240 BPM (`minimumTempo`, `maximumTempo` in `Session.h`); there is no
tempo map. The engine anchors clips to beats and moves them itself when the tempo changes. Rhino's own timeline state
is not part of that, so `setTempo` rescales it by the same factor: automation points, which are stored in seconds
([Track automation](automation.md)), and the dragged loop span. Repitched warp clips get a new speed ratio from
`updateRepitchedClips` ([Time warp](time-warp.md)).

A drag on the tempo or signature field is one undo step. `beginTempoGesture` and `endTempoGesture` count their
nesting, so one caller cannot close a bracket another opened.

## Time signature and metronome

Any numerator from 1 to 99 over 1, 2, 4, 8 or 16. `beatsPerBar()` is numerator × 4 ÷ denominator, and it is what "one
bar" means everywhere — a new clip, a pattern, the loop of an empty document. Playback restarts after a signature
change. The arrangement's bar ruler subdivides a bar into the numerator's counts (eighths in 6/8), not into
`beatsPerBar()` quarter notes ([Arrangement view](arrangement-view.md)).

The click's on/off, bar emphasis and level are the edit's own `clickTrack*` properties. `clickTrackGain` converts:
Rhino speaks decibels, the engine stores a linear gain and clamps it to 0.2-1.0, and writing decibels straight in put
all three of the menu's levels below that floor, so they sounded the same. The count-in is Rhino's own, not the
engine's ([Recording and the count-in](recording.md)).

## Releasing the device

`releaseAudioDevice` takes the count-in and the library preview off the device before closing it, because both are
audio callbacks on that device. The test runners call it, and no offline render finishes after it
([Offline renders that never return](renders-that-never-return.md)).

## Related

- [App shell and control bar](app-shell.md)
- [Arrangement view](arrangement-view.md)
- [Recording and the count-in](recording.md)
- [Time warp](time-warp.md)
- [Track automation](automation.md)
