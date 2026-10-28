---
title: Transport, tempo and loop
type: component
summary: Play, pause and stop (a pause returns to the arrangement's selection or the loop's start, Stop to the top of the song), the loop that is always on, the single tempo and the time signature, and what Rhino rescales by hand when the tempo moves.
tags: [rhino, transport, tempo]
sources: []
updated: 2026-10-27
---

# Transport, tempo and loop

`native/src/model/SessionTransport.cpp` holds the transport commands, the tempo, the time signature, the metronome settings
and the loop. Tracktion's `TransportControl` does the playing; the control bar's drag fields are `ControlBarFields.*`
and its readout is `TransportDisplay.*` ([App shell and control bar](app-shell.md)).

## Pause returns to the passage, Stop to the top

The user's rules, since commit `38677ca` (2026-10-27):

- **Pause** (Play while rolling, Space, the Push's Play) is `Session::togglePlayback`. It stops, then puts the
  transport at `playbackStartSeconds` if set, else at `loopRange().getStart()`: the start of the span dragged on the
  ruler, or 0 under the automatic loop. `playbackStartSeconds` is a `std::optional<double>`. The arrangement sets it
  while it has a selection of any kind (a clicked clip, a swept span, the insert point a lane or ruler click puts
  down) and clears it when it has none (Escape, a click on empty header space, a sync that drops it); see
  [Arrangement view](arrangement-view.md#rules-worth-knowing). `newProject` and `restoreProject` reset it. Until then a
  pause left the playhead where it stopped.
- **Stop** (the Stop button, the Push's Shift+Play, opening the DJ view while rolling) is `Session::stop`, which
  always goes to 0, whatever is selected or looped. Until then Stop went back to the selection line, and only a
  double-click on Stop or the rewind glyph went to 0 (`Session::returnToStart`, deleted with the shell's
  `StopButton`). The rewind glyph then did exactly what Stop does, and the user had it removed the same day. The
  Push's callback is still named `PushController::returnToStart`; it calls `Session::stop`.
- Both send a MIDI panic to every plugin list (`releasePlayingNotes`) and finish a recording that was running. Ending a
  take with the record button or F9 (`stopRecording`) still leaves the playhead where the take ended. Play during a
  count-in calls the count-in off.
- **Decided with the user:** only the arrangement's selection counts, not the note editor's, whose ruler click only
  moves the transport. The loop span marks only where a pause returns: it is not a punch-in/out range, and recording
  is unchanged.
- `RegionClipboard.inc` checks stop → 0 with the selection kept, pause → the selection's start, pause with the
  selection cleared → the loop's start (2.0 under a 2-6 loop), then 0 with the loop cleared.

## The loop is always on

`refreshLoop` sets `looping = true` every time it runs. The range (`loopRange()`) is the span dragged on the ruler's
loop band when there is one — `setLoopRange` refuses anything under 0.02 s, and a right-click on the band clears it —
and otherwise everything up to the end of the last clip, or one bar in an empty document. The readout asks the same
function as the transport, and the clip-changing paths in `Session*.cpp` call `refreshLoop` after they edit. Recording
lifts the loop for its duration, so a take cannot wrap over itself ([Recording and the count-in](recording.md)). With
nothing selected in the arrangement, a pause returns to the range's start; the range bounds nothing for recording.

The dragged span lives on `Session`, not in the document: it is not saved with the project, and both `newProject` and
(since commit `a04b407`) `restoreProject` clear it, so a span dragged in one document no longer stays in force after
opening another.

**Ctrl+L in the arrangement** (2026-10-27) is Live's Loop Selection (Live 12 manual, 6.6, pp. 168-169):
`Arrangement::loopSelection` (`ArrangementSelection.cpp`) sets this span to the selection's start and end, whatever
tracks it covers, so on a clicked clip it loops the clip. Live's second press switches the loop off; Rhino's cannot be
off, so a press when the span already equals the selection calls `clearManualLoopRange` instead, as the band's
right-click does. An insert point or no selection is refused with a status. The shell handles Ctrl+L too
(`app/MainComponentCommands.cpp`, like Ctrl+E, J and B), so it works with the keyboard on the audio editor or the
browser, and in the DJ view it says the command is the arrangement's. The note editor's own Ctrl+L
(`StepGrid::loopEditedClip`: loop the open clip, no toggle) answers first while it has focus. Tested in
`AudioClipEditing.inc`.

## One tempo, and what follows it by hand

The edit has a single tempo entry, clamped to 40-240 BPM (`minimumTempo`, `maximumTempo` in `Session.h`); there is no
tempo map. The engine anchors clips to beats and moves them itself when the tempo changes. Rhino's own timeline state
is not part of that, so `setTempo` rescales it by the same factor: automation points, which are stored in seconds
([Track automation](automation.md)), and the dragged loop span. A warped clip's envelope points are rescaled too,
because they are seconds of content that stretches with the song ([Clip envelopes](clip-envelopes.md)). Repitched warp clips get a new speed ratio from
`updateRepitchedClips` ([Time warp](time-warp.md)). Tracks pasted from a document at another tempo are put back on
their beats by the same rules, the engine's clip rule restated by hand because no engine call remaps only some clips
([Track clipboard](track-clipboard.md#into-another-document), commit `c790287`).

A drag on the tempo or signature field is one undo step. `beginTempoGesture` and `endTempoGesture` count their
nesting, so one caller cannot close a bracket another opened. A tempo drag step costs
42-43 ms on 135 tracks and 103,072 notes (2026-10-23; a knob drag step costs 0.08 ms), because a tempo change bumps
every track's revision, so the arrangement reads every track's clips again, and the engine remaps every clip. It is
still open ([The 120-track stress audit](stress-audit-2026-10.md)).

## Time signature and metronome

Any numerator from 1 to 99 over 1, 2, 4, 8 or 16. `beatsPerBar()` is numerator × 4 ÷ denominator, and it is what "one
bar" means everywhere — a new clip, a pattern, the loop of an empty document. Playback restarts after a signature
change. The arrangement's bar ruler subdivides a bar into the numerator's counts (eighths in 6/8), not into
`beatsPerBar()` quarter notes ([Arrangement view](arrangement-view.md)). So, since commit `4416222`, do the control
bar's position and loop readings and the bar a paste reports: `musicalPosition` (`ArrangementGrid.h`) turns the engine's
quarter-note beats into bar, count and sixteenth, each from one, so 6/8 reads six eighths of two sixteenths and 7/8
seven equal eighths. A position a hair short of a bar line, as a tempo-map round trip leaves one, reads as that bar.
Since commit `f2ae477` the note editor's ruler and shading count the same beat, `StepGrid::stepsPerBeat` being the
step resolution over the denominator, from the clip's own start ([Note editor](note-editor.md#drawing)).

The click's on/off, bar emphasis and level are the edit's own `clickTrack*` properties. `clickTrackGain` converts:
Rhino speaks decibels, the engine stores a linear gain and clamps it to 0.2-1.0, and writing decibels straight in put
all three of the menu's levels below that floor, so they sounded the same. The count-in is Rhino's own, not the
engine's ([Recording and the count-in](recording.md)).

## What the views read

A view or the shell never opens the engine's transport; `Session` answers (2026-10-23, [Dependency direction](dependency-direction.md#the-views-ask-session-not-the-engine)). `isPlaying()`, `playheadSeconds()` (the audible position while playing, read on every display refresh; the transport's own while standing, [Playhead rendering](playhead.md)), `locate(seconds)`, `songLengthSeconds()`, `loopRange()`, and the tempo map as `secondsToBeats()` and `beatsToSeconds()`. `locate` moves the transport, rolling or not, and nothing else: since 2026-10-27 it no longer sets where a pause returns (it did from 2026-10-23, so a click on the note editor's ruler did too), which only `setPlaybackStart` and `clearPlaybackStart` change ([above](#pause-returns-to-the-passage-stop-to-the-top)). The shell hears the transport start and stop, and the undo history change, through the relayed `Session::transportState` and `undoHistory`, which do not wake the panels listening to the session ([Session, the model](session-model.md#reading-the-document)).

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
- [Track clipboard (cards)](track-clipboard.md)
