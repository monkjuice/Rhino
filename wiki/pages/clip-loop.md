---
title: Clip Loop switch
type: component
summary: The note editor's footer Loop switch, Live's clip Loop - Tracktion plays a MIDI clip's loop range end to end for as long as the clip is dragged out; switching it on loops what plays now, switching it off keeps the clip's place and length, the editor shows and edits only the loop, and copies, pastes and DJ cells keep it.
tags: [rhino, notes, clips, loop, tracktion]
sources: []
updated: 2026-10-27
---

# Clip Loop switch

Commit `4a6a17b` (2026-10-27). The user asked for "a loop button which toggles the midi arrangement looper, i.e. if you drag and expand the midi clip, the pattern will be looped". The **Loop** toggle in the [note editor](note-editor.md#the-loop-switch)'s footer (`StepGrid::clipLoopButton`) loops the open MIDI clip, as Live's clip Loop switch does. Drag the clip longer in the arrangement and its loop repeats. The model is `native/src/model/SessionClipLoop.cpp`: `Session::isPatternLooping`, `setPatternLooping(bool)` and `patternStepAt(seconds)`. New clips still do not loop, so no existing document or path changed.

**Two loops now sit side by side.** The ⟳ button over the keys, Ctrl+L and the menus' "Loop Clip" set the *song's* loop over the clip ([Transport, tempo and loop](transport.md)). Its tooltip now reads "Loop playback over the entire edited clip". The menus were not renamed (`NoteMenu.inc` checks the "Loop Clip" text), so the two meanings can be confused.

## The engine does the repeating

Rhino only sets a range, and nothing in Rhino repeats a note. These facts were read at the pinned Tracktion `4536d8a`:

- `te::MidiClip::setLoopRangeBeats` gives the clip a loop, a stretch of its sequence in beats. The clip then plays that stretch end to end for its whole length (`getSequenceLooped`, `createLoopRangeDefinesAllRepetitionsSequence`). Arrangement playback builds its node from the looped sequence (`createNodeForMidiClip`).
- **On a looped clip the offset counts into the loop**, from the loop's start, not into the sequence. An offset past one pass still plays (`extraLoops`).
- **`MidiClip::cloneFrom` does not copy the loop range.** Whole-track copies keep the loop because they copy state. Any path that builds a clip through `cloneFrom` has to set the range again.
- `MidiClip::disableLooping` cuts the clip to one pass and carries a "TODO: is this correct?", so Rhino does not call it.

## On and off

- **On:** the loop becomes the window the clip plays now, `[offset, offset + length)` in sequence beats, and the offset becomes 0. Nothing you hear changes until the clip is dragged longer.
- **Off:** the clip keeps its place and length, as in Live. Its offset becomes `loopStart + (offset mod loopLength)`, so it plays the sequence once from wherever the loop had reached, and is silent past the notes.
- Each switch is one undo step ("Loop clip" or "Stop looping clip") and marks the track with `ModelChange::notes`. While the transport rolls, it also panics the track and restarts playback. The button follows the model in `changeListenerCallback` and is disabled when no clip is open. It reports through `StepGrid::status`.

## The editor shows the loop

On a looped clip, `patternOffsetBeats()` returns the loop's start and `patternLengthBeats()` its length (`SessionNotes.cpp`). The step count, every note edit, fill to end, the chop and the tools therefore act inside the loop, and every edit is heard on every pass. A paste past the end (`ensurePatternLengthSteps`) grows the loop, and grows the clip only when the clip is shorter than one pass of the longer loop. `patternStepAt` wraps a time into the loop, phased by the offset. The playhead, the ruler and `removeNotes`' "was it sounding" check all go through it ([Note editor](note-editor.md#the-playhead-and-the-ruler)).

## Elsewhere

- **Arrangement:** `rebuildTrackClips` (`ArrangementSync.cpp`) reads `getSequenceLooped()`, so a stretched clip draws its repeats from the same origin ([Arrangement view](arrangement-view.md)). There are no loop-boundary notches, which Live draws.
- **Copies:** `Session::ClipSnapshot` carries `loopStartBeats` and `loopLengthBeats`. `pasteClipRegion` sets the range before the position, so copy, cut, paste, duplicate and selection moves all keep the loop ([Region editing](region-editing.md#why-snapshots-not-ids)). A split of a looped clip keeps an offset past one pass, and that plays the same.
- **DJ cells:** a looped clip copied into a cell becomes one pass of its loop, looping the same range ([Clip slots](clip-slots.md)).

## Limits

- The only ways to change the loop's length are to switch Loop off, resize the clip and switch it on again, or to paste past the loop's end. The editor has no loop brace.
- A looped clip cannot be extended leftwards past offset 0.
- Notes outside the loop stay in the sequence, and the editor does not show them while the clip loops.

## Tests

`tests/Arrangement/scenarios/ClipLoop.inc` renders, so it is included right after "bounce to new track", before `GesturesAndPersistence.inc` ([Offline renders that never return](renders-that-never-return.md)). It checks:

- switching on and off keeps the clip's length and place;
- a one-bar loop stretched to two bars still shows 16 steps, and the arrangement draws the second pass;
- a `renderTimelineWav` through 4OSC hears the note again at bar two. With the loop off the second bar's RMS was 0, against 0.157 for the first pass, which shows the check can fail;
- the playhead in the second pass;
- an edit heard on both passes;
- a paste that grows the loop, and its undo;
- a region copy and a DJ cell copy keep the loop;
- a left trim's phase, and undo.

## Related

- [Note editor (StepGrid)](note-editor.md)
- [Region editing](region-editing.md)
- [Clip slots, the DJ consoles' cells](clip-slots.md)
- [Clips never overlap](clip-placement.md)
- [Transport, tempo and loop](transport.md)
- [Arrangement view](arrangement-view.md)
