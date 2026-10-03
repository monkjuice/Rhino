---
title: Note editor (StepGrid)
type: component
summary: Edits the notes of the open MIDI clip in steps, with a draw mode, a step region and caches that tie it closely to Session.
tags: [rhino, ui, notes]
sources: []
updated: 2026-10-03
---

# Note editor (StepGrid)

`StepGrid` edits the notes of the MIDI clip the editor is pointed at, `Session::pattern()`. It is one class across `StepGrid.cpp`, `StepGridPainter.cpp`, `StepGridGestures.cpp` (pointer) and `StepGridEditing.cpp` (selection, region, clipboard, modal tools), with private helpers in `StepGridInternal.h`. It opens in the shell's lower pane when a MIDI clip is double-clicked ([App shell and control bar](app-shell.md)).

## Gestures

The pointer follows the rule `SelectionInput.h` shares with the arrangement ([Pointer and selection rules](pointer-and-selection.md)):

- **Double-click** an empty cell to create a note; a plain drag sweeps a marquee; Ctrl toggles a note, Shift adds.
- **B** (or the footer button) toggles draw mode: the pointer becomes a pencil, a press paints on empty cells or erases on a note, and the drag keeps doing whichever it started.
- **Right-drag erases** in either mode. Each stroke is one undo step (`beginNoteGesture`/`endNoteGesture`).
- **Ctrl+E** on a sustained note divides it into 2-32 retriggers, chosen with Ctrl+arrows or the wheel; **holding V** with Up/Down or the wheel adjusts the selected notes' velocity; Up/Down transposes, Shift for an octave.

## The step region

Copy, cut, paste, duplicate and delete act on a span of steps: the arrangement's [region editing](region-editing.md) rule, measured in steps. Copy keeps the region's width, so rests at either end survive. A paste lands on the insert point and replaces the notes it covers, lengthening the clip if needed, rather than hunting for empty space. Only the insert line is drawn, and not even that for a region read off selected notes (`regionFromNotes`).

The marquee anchors in steps and pitch, not pixels: a drag past an edge scrolls the view from the gesture timer, and a pixel anchor would travel with it, dropping notes that had scrolled out.

## Pitfalls

- **Size.** Its row-addressed caches (512 steps × 48 pitch rows) make one `StepGrid` about 200 KB; in a test scenario create it with `std::make_unique` ([A CTest SegFault may be a stack overflow](stack-overflow-reports-as-segfault.md)).
- **It is welded to `Session`.** The four files reach into `Session` 101 times (counted 2026-10-03), and `Session.h` pulls in Tracktion. That is why Forge's arp pattern editor (`instruments/rhino-forge/PLAN.md` M13c, not started) cannot simply reuse it: the plan is first to extract a Session-free note-grid header, as `ClipGeometry.h` was.
- **The clip can vanish under it.** `Session::repairPatternClip` is the single place that re-points the editor when its clip is deleted ([Hold ids, not pointers](ids-not-pointers.md)).
- **Octave names.** Middle C reads C3 (`drumLaneName` in `StepGridInternal.h` and the painter), matching Live and Forge; the two must change together.

`README.md` ("Create A Pattern") still describes a span dragged out "with S held"; that held key was retired and a plain drag sweeps (checked 2026-10-03).

## Related

- [Arrangement view](arrangement-view.md)
- [Region editing](region-editing.md)
- [Pointer and selection rules](pointer-and-selection.md)
- [Forge arpeggiator](forge-arpeggiator.md)
- [Writing Rhino tests](writing-rhino-tests.md)
- [The first track is still the pattern track](pattern-track.md)
