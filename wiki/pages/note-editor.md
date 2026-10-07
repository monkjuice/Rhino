---
title: Note editor (StepGrid)
type: component
summary: Edits the notes of the open MIDI clip in steps, with a draw mode, a step region and caches that tie it closely to Session.
tags: [rhino, ui, notes]
sources: []
updated: 2026-10-07
---

# Note editor (StepGrid)

`StepGrid` edits the notes of the MIDI clip the editor is pointed at, `Session::pattern()`. A document with no MIDI track has no such clip: `Session::hasPatternClip()` says so, every note edit refuses with a message, and the grid draws an empty editor. It is one class across `StepGrid.cpp`, `StepGridPainter.cpp`, `StepGridGestures.cpp` (pointer) and `StepGridEditing.cpp` (selection, region, clipboard, modal tools), with private helpers in `StepGridInternal.h`. It opens in the shell's lower pane when a MIDI clip is double-clicked ([App shell and control bar](app-shell.md)).

## Gestures

The pointer follows the rule `SelectionInput.h` shares with the arrangement ([Pointer and selection rules](pointer-and-selection.md)):

- **Double-click** an empty cell to create a note; a plain drag sweeps a marquee; Ctrl toggles a note, Shift adds.
- **B** (or the footer button) toggles draw mode: the pointer becomes a pencil, a press paints on empty cells or erases on a note, and the drag keeps doing whichever it started.
- **Right-drag erases** in either mode. Each stroke is one undo step (`beginNoteGesture`/`endNoteGesture`).
- **Ctrl+E** on a sustained note divides it into 2-32 retriggers, chosen with Ctrl+arrows or the wheel; **holding V** with Up/Down or the wheel adjusts the selected notes' velocity; Up/Down transposes, Shift for an octave.
- **F** fills the selected notes to the clip's end, plain F only, so Ctrl+F gets past the focused editor to the browser's search (commit `bac4059`).

## The step region

Copy, cut, paste, duplicate and delete act on a span of steps: the arrangement's [region editing](region-editing.md) rule, measured in steps. Copy keeps the region's width, so rests at either end survive. A paste lands on the insert point and replaces the notes it covers, lengthening the clip if needed, rather than hunting for empty space. Only the insert line is drawn, and not even that for a region read off selected notes (`regionFromNotes`).

The marquee anchors in steps and pitch, not pixels: a drag past an edge scrolls the view from the gesture timer, and a pixel anchor would travel with it, dropping notes that had scrolled out.

## Drum rows

When the open clip's track plays drums (`Session::isPatternDrums`), the grid shows `Session::pitches` (16) rows at its default zoom and pins its lowest row at the first note of the bank the [Drum Rack](drum-rack.md)'s face shows (`Session::patternDrumLowestNote`, from `DrumRackDevice::firstShownNote`; C2 by default). So the rows are the sixteen pads on the face. `StepGrid::automaticLowestPitch` reads it, and every change clears a manual pitch scroll for drums; `Session::showDrumBank` announces, so the rows follow the face as it pages (since 2026-10-07; before, they were fixed at `Session::lowestNote`, 48). Each row takes its name from `Session::patternNoteName`, which asks the instrument's `hasNameForMidiNoteNumber`, and a row whose pad is empty reads as its note. Pad names change without any note changing, so the grid keeps the names it last drew (`drumRowNames`) and repaints when they differ.

## Pitfalls

- **Size.** Its row-addressed caches (512 steps × 48 pitch rows) make one `StepGrid` about 200 KB; in a test scenario create it with `std::make_unique` ([A CTest SegFault may be a stack overflow](stack-overflow-reports-as-segfault.md)).
- **It is welded to `Session`.** The four files reach into `Session` 101 times (counted 2026-10-03), and `Session.h` pulls in Tracktion. That is why Forge's planned arp pattern editor cannot simply reuse it: first extract a Session-free note-grid header, as `ClipGeometry.h` was.
- **The clip can vanish under it.** `Session::repairPatternClip` is the single place that re-points the editor when its clip goes: by id first, else to the first MIDI clip on the first MIDI track, else to a hidden starter clip it makes there, never onto an audio track ([No track is special for being first](pattern-track.md), [Hold ids, not pointers](ids-not-pointers.md)).
- **Hidden, it waits.** While another face holds the lower pane the grid marks itself stale instead of rebuilding on every change, and skips its playhead every display refresh ([What a change costs the interface](ui-cost-of-a-change.md)).
- **Octave names.** Middle C reads C3, matching Live, Forge and the Drum Rack's pads (`padNoteName` in `core/DrumKitFile.h`, which `DrumRackDevice::noteName` calls). Both of the painter's keyboards name rows through `pitchName` in `StepGridInternal.h` (until 2026-10-06, `drumLaneName`). Change `pitchName` and `padNoteName` together or the rows and the pads disagree.

## Related

- [Arrangement view](arrangement-view.md)
- [Region editing](region-editing.md)
- [Pointer and selection rules](pointer-and-selection.md)
- [Forge arpeggiator](forge-arpeggiator.md)
- [Writing Rhino tests](writing-rhino-tests.md)
- [No track is special for being first](pattern-track.md)
- [Drum Rack](drum-rack.md)
