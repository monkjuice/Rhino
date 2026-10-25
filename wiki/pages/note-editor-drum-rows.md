---
title: Note editor drum rows
type: component
summary: On a drum clip the note editor's rows are its Drum Rack's assigned keys plus the keys its notes are on, mapped through lines, moved row to row, held still under a gesture, opened on the rack's bank and scrolled like a piano clip's.
tags: [rhino, ui, notes, drums]
sources: []
updated: 2026-10-25
---

# Note editor drum rows

When the open clip's track plays drums (`Session::isPatternDrums`), the [note editor](note-editor.md)'s rows are pads, not a piano's keys. Since commit `3418c1b` (2026-10-25) they are only the keys the [Drum Rack](drum-rack.md) has assigned, the pads holding a sound, plus the keys the clip's notes are on. The user asked to "only display the assigned midi keys if we are on drum editor".

## Which rows

- `StepGrid::shownPitches`, lowest first, holds the rows' pitches; empty means every pitch is a row, as on a piano clip. `updateShownPitches()` builds it from `Session::patternNamedNotes()` plus the pitches of the clip's notes inside the grid's steps.
- `Session::patternNamedNotes()` (`SessionPatternPresets.cpp`) lists every note the open clip's instrument names in one pass: one `patternInstrument` lookup and 128 `hasNameForMidiNoteNumber` calls, rather than the grid asking `patternNoteName` 128 times on every change. A Drum Rack names a pad when it holds a sound.
- A rack that names nothing, a blank one, leaves `shownPitches` empty, so every pitch is a row, notes or not.
- An empty pad has no row unless notes are on it, and that row reads as its pitch (`rowName` falls back to `pitchName`, e.g. D#2). So emptying a pad under notes keeps their row, as the user asked.

**Why the union.** With no history of assignments, a key that lost its pad and a key that never had one are the same state, so "assigned, or holding notes" is the rule that keeps the row of a pad emptied under notes. It also means no note is ever without a row. Live's Fold, rows only for the keys with notes, is a different control the user wants separately ("fold would be a different CTA"). It is not built.

## Lines

The grid maps rows through lines. A line is a place in the order the rows go: the pitch itself while every pitch is a row, otherwise an index into `shownPitches`. `lowestVisiblePitch` stays a pitch, the bottom row's.

- `lineCount`, `lineOfPitch`, `pitchOfLine`, `bottomLine`, `pitchOfRow`, `rowOfPitch` and `setBottomLine` replaced every `lowestVisiblePitch + rows - 1 - x`.
- The pitch versions were renamed: `pitchAtY`/`yForPitch` to `lineAtY`/`yForLine`, `selectionAnchorPitch` to `selectionAnchorLine`, and `panStartPitch` to `panStartLine`.
- `computePitchRowCount` is capped by `lineCount()`. `bottomLine` takes the next key up when `lowestVisiblePitch` names a key that has just lost its row.

New code that turns a row into a pitch, or back, goes through these helpers. The old arithmetic is right only while every pitch is a row.

## Moving notes

- Scrolling goes by rows, a pad at a time: the wheel, the keys, the middle button and a drag's autoscroll.
- Vertical moves go row to row. Given the rows' pitches as `lanes`, `Session::moveNotes(states, stepDelta, pitchDelta, lanes)` counts `pitchDelta` in lanes, so Up/Down and drags never land a note on a key with no row. A selection holding a note on no lane keeps its pitches and moves along the clip only.
- A drag's delta is worked out in lines (`moveDraggedNotesAt`). On a piano clip that is exactly the old semitone arithmetic.
- The note menu's Transpose offers Up a Pad and Down a Pad, and no octave items. Shift+Up/Down still asks for 12, which counts twelve rows there.

## Rows hold still under a gesture

`changeListenerCallback` reads `shownPitches` again only while no gesture runs, and `mouseUp` calls `updateShownPitches()`, rebuilding if it changed. Otherwise dragging the last note off a key with no pad would remove that row mid-drag and shift every row under the pointer.

## Layout, bank and scroll

- A drum clip's few rows usually all fit, and rows that all fit hang from the header with the spare room below ([Note editor](note-editor.md#drawing)). A note dragged into that room lands on the lowest row.
- The rows open on the bank the rack's face shows (`automaticLowestPitch`). The bottom row is the first assigned key from `Session::patternDrumLowestNote` up (from `DrumRackDevice::firstShownNote`, C2 by default), or that note itself on a blank rack. They follow the face as it pages, because `Session::showDrumBank` announces (since 2026-10-07; before, they were fixed at `Session::lowestNote`, 48).
- Since commit `d872f0b` they keep the editor's one row height and scroll as a piano clip's do. A scroll holds until the face pages to another bank: `shownDrumBank` keeps the bank the rows were last fitted to (-1 for a piano clip), and `changeListenerCallback` clears `manualPitchScroll` when it differs, which also happens when the editor moves between a drum and a piano clip. Before that, the grid showed exactly the sixteen pads, stretched to the pane, and refused every pitch scroll.
- Each row takes its name from `Session::patternNoteName`, which asks the instrument's `hasNameForMidiNoteNumber`, and falls back to the pitch. Pad names change without any note changing, so the grid keeps the names it last drew (`drumRowNames`) and repaints when they differ.

## Tests

`NoteDrumRows.inc` (commit `3418c1b`) runs on the 808 Kit and checks that:

- ten assigned keys and a note on the empty D#2 make 11 rows;
- Up/Down skip the empty A2;
- drags go row to row and into the room below;
- the rows hold still mid-drag and update on release;
- scrolling goes by rows in a 150 px pane;
- a pad given a sound gains a row;
- clearing the kick's pad keeps its row, named C2.

`RHINO_NOTE_DRUMS_SNAPSHOT=<png>` writes the result ([Seeing the UI without taking the screen](headless-ui-snapshots.md)). `VelocityLaneLayout.inc` checks, on a blank rack, a row for every key, the bank on the bottom row, the wheel, a scroll surviving an edit and the follow on a page.

## Related

- [Note editor (StepGrid)](note-editor.md)
- [Drum Rack](drum-rack.md)
- [Pointer and selection rules](pointer-and-selection.md)
- [Writing Rhino tests](writing-rhino-tests.md)
