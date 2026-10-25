---
title: Note editor (StepGrid)
type: component
summary: Edits the notes of the open MIDI clip in steps, with a draw mode, a step region, a velocity lane under the rows and caches that tie it closely to Session.
tags: [rhino, ui, notes]
sources: []
updated: 2026-10-24
---

# Note editor (StepGrid)

`StepGrid` edits the notes of the MIDI clip the editor is pointed at, `Session::pattern()`. A document with no MIDI track has no such clip: `Session::hasPatternClip()` says so, every note edit refuses with a message (a velocity edit included), and the grid draws an empty editor. It is one class across seven units in `native/src/ui/notes/`, its folder since 2026-10-13: `StepGrid.cpp`, `StepGridPainter.cpp`, `StepGridGestures.cpp` (pointer), `StepGridEditing.cpp` (selection, region, clipboard, modal tools) and, since 2026-10-16, the velocity lane's `StepGridVelocity.cpp`, `StepGridVelocityGestures.cpp` and `StepGridVelocityPainter.cpp`. Private helpers are in `StepGridInternal.h`, and the lane's arithmetic is in `VelocityLaneGeometry.h`, a pure header. It opens in the shell's lower pane when a MIDI clip is double-clicked, and its first appearance in a run raises the pane to at least `StepGrid::openingHeight`, 261 px ([App shell and control bar](app-shell.md)).

## Gestures

The pointer follows the rule `SelectionInput.h` shares with the arrangement ([Pointer and selection rules](pointer-and-selection.md)):

- **Double-click** an empty cell to create a note; a plain drag sweeps a marquee; Ctrl toggles a note, Shift adds.
- **B** (or the footer button) toggles draw mode: the pointer becomes a pencil, a press paints on empty cells or erases on a note, and the drag keeps doing whichever it started.
- **Right-drag erases** in either mode. Each stroke is one undo step (`beginNoteGesture`/`endNoteGesture`).
- **Ctrl+E** on a sustained note divides it into 2-32 retriggers, chosen with Ctrl+arrows or the wheel. **Holding V** with Up/Down or the wheel changes the selected notes' velocity by 1 MIDI step (it worked in percent until 2026-10-16). **Ctrl+Up/Down** changes it by 10, and Ctrl+Shift+Up/Down by 1. Up/Down alone transposes, with Shift for an octave.
- **A note dragged below the rows** is held to the lowest row (`moveDraggedNotesAt`), so it keeps following the pointer while the pitch scrolls; until 2026-10-16 it froze where the pointer left the rows.
- **F** fills the selected notes to the clip's end, plain F only, so Ctrl+F gets past the focused editor to the browser's search (commit `bac4059`).
- **The middle button pans** (commit `0725daa`, as the arrangement does): `Gesture::pan` and `panView` in `StepGridGestures.cpp` carry the steps by the pixels travelled (`stepScroll`, clamped to the clip) and the rows by whole rows (`lowestVisiblePitch`, setting `manualPitchScroll`). The anchors are absolute, so a pan that hits the clip's start comes back exactly. Drum rows pan only sideways, pinned to the bank as for the wheel. The pan answers first in `mouseDown`: before, a middle press fell through to the left-button paths and carried a note or resized the keys. It edits nothing. It opens no note gesture (`mouseUp` returns early, the destructor skips `endNoteGesture`) and runs before `dragPosition` is recorded, so the gesture timer never autoscrolls it. `NotePan.inc` holds it.

## Velocity lane

Below the rows, a lane shows a marker for every note of the clip, including notes scrolled out of the pitch window, in MIDI 1-127. It is modelled on Live 12's velocity editor. A marker can be dragged, a group dragged with it, values painted in draw mode or swept up with a marquee, and every gesture is one undo step announced once. The lane folds away rather than push the rows below 8 px each. Hovering a grid note lights its marker. Layout, model and gestures are in [Note editor velocity lane](note-velocity-lane.md).

## The step region

Copy, cut, paste, duplicate and delete act on a span of steps: the arrangement's [region editing](region-editing.md) rule, measured in steps. Copy keeps the region's width, so rests at either end survive. A paste lands on the insert point and replaces the notes it covers, lengthening the clip if needed, rather than hunting for empty space. Only the insert line is drawn, and not even that for a region read off selected notes (`regionFromNotes`).

Copy, cut and `effectiveStepRegion` read the selected notes through `laneNoteFor`, which covers every note in the clip, not only the visible rows. Until 2026-10-16, a selected note outside the pitch window was deleted by cut but left out of the clipboard. Ctrl+A could already reach that case, and the velocity lane made it a one-click case. A cut-and-paste round-trip test now holds it.

The marquee anchors in steps and pitch, not pixels: a drag past an edge scrolls the view from the gesture timer, and a pixel anchor would travel with it, dropping notes that had scrolled out.

## Showing a swept span

Since commit `4bd0f45` (2026-10-24) a sweep across part of the open MIDI clip in the arrangement is shown here, as Live's Clip View shows the part of a clip selected and zooms to a time selection. The arrangement names the clip primary and reports the span ([Region editing](region-editing.md#one-selection)); the shell calls `StepGrid::showTimeSpan(start, end)` (`StepGridEditing.cpp`) when the clip is the pattern clip, whether the editor is showing or not, so the Clip toggle then opens it on that part.

- **Frame.** Seconds become the clip's steps through the tempo map, held to the clip's own end, which can fall short of the grid's last step. `stepZoom = clamp(steps / span, 1, max(8, steps / 16))`: the upper bound is the deepest view the editor otherwise shows, since a long clip opens at steps/16, deeper than `zoomAt`'s 8. The scroll frames the span exactly when the zoom reaches it and centres it when it is narrower than the deepest view.
- **Select.** Every note overlapping the span, at any pitch, and the step region set to the exact span (`setStepSelection`, so `regionFromNotes` is false and the insert line draws at its start).
- **Order.** The selection is set last, because a rebuild (`changeListenerCallback` when the scroll changes the row count) reads the region off the notes again. A hidden editor whose step count is stale is rebuilt first: otherwise the catch-up rebuild when it is shown sees a new step count and resets the zoom to the new length's default.
- **Pitch is left alone.** The automatic pitch window already fits the clip's notes; scrolling to the part would set `manualPitchScroll`, which turns the automatic fit off for later clips too.

`RegionMove.inc` checks the framed span, a one-step span centred, and a grid framed while hidden inside a parent component that keeps its frame once shown.

## A note that began before the window

A MIDI clip plays a window into its sequence, set by its offset; cuts and trims move the window, never the notes ([Clips never overlap](clip-placement.md)). Tracktion plays a note that began before the window and ends inside it, from the clip's start: `MidiList::createDefaultPlaybackMidiSequence` keeps any note ending after the window's start, and `addToSequence` emits its note-on at `std::max (0.0, downTime)` (checked upstream at `4536d8a`). The arrangement drew it, but the grid and the velocity lane skip any note starting before step 0 (`rebuildVisibleNotes`, `rebuildLaneNotes`), so until commit `1818c5b` (2026-10-24) such a note sounded with nothing in the editor. It was found on a clip whose window opened 3/64 of a beat (23 ms) after a note began; how the clip got that window was not recovered, since moves are not logged.

`Session::editorNotes` (`SessionNotes.cpp`, helper `shownStartStep`) now reports a note crossing the window's start as starting at step 0, with the length it sounds for. No UI file changed: the grid, the lane and the note clipboard all read `editorNotes`. Edits act on what is shown:

- `resizeNote` and `fillNoteToClipEnd` measure the length from the shown start and keep the hidden part before the window.
- `redistributeNotes` (Ctrl+E) divides from the window's start and drops the hidden part, so no retrigger lands where the clip cannot play it.
- Moving the note left is refused by `moveNotes`' existing clamp. Moving it right lands it under the pointer, because `moveDraggedNotesAt` asks for the distance from where the note is shown.
- A note wholly before the window plays nothing, keeps its negative start and stays off the grid.

Showing what plays follows [Displays draw from the DSP](displays-draw-from-the-dsp.md). Live's manual does not say what Live does here. Silencing the note was the alternative, and it was not taken. `tests/Arrangement/scenarios/ClipWindow.inc` holds it: the note is shown at step 0 for its sounding length, in both `visibleNotes` and `laneNotes`, a resize keeps its true start, and a divide into two gives halves at steps 0 and 2.

## Drum rows

When the open clip's track plays drums (`Session::isPatternDrums`), the grid shows `Session::pitches` (16) rows at its default zoom and pins its lowest row at the first note of the bank the [Drum Rack](drum-rack.md)'s face shows (`Session::patternDrumLowestNote`, from `DrumRackDevice::firstShownNote`; C2 by default). So the rows are the sixteen pads on the face. `StepGrid::automaticLowestPitch` reads it, and every change clears a manual pitch scroll for drums; `Session::showDrumBank` announces, so the rows follow the face as it pages (since 2026-10-07; before, they were fixed at `Session::lowestNote`, 48). Each row takes its name from `Session::patternNoteName`, which asks the instrument's `hasNameForMidiNoteNumber`, and a row whose pad is empty reads as its note. Pad names change without any note changing, so the grid keeps the names it last drew (`drumRowNames`) and repaints when they differ.

## Pitfalls

- **Size.** Its row-addressed caches (512 steps × 48 pitch rows) make one `StepGrid` about 200 KB; in a test scenario create it with `std::make_unique` ([A CTest SegFault may be a stack overflow](stack-overflow-reports-as-segfault.md)).
- **One timer serves every gesture.** Drags, marquees, strokes and the modal tools (Ctrl+E, held V) share the grid's gesture timer. Ending a modal tool stops the timer only when no other gesture is running (`finishSubdivision`, `finishVelocityAdjustment`, since 2026-10-16), so a stroke keeps autoscrolling after one ends. In the other direction, `mouseUp` leaves the timer running while hold-V or a subdivision is active, because the timer is what notices V being released.
- **A selection change repaints what it changes** (`setSelectedStates`, since 2026-10-16): the notes it recolours, the lane, and the insert line's column when the change shows or hides that line. Before that, clicking an unselected note in a grid that already had focus repainted only the footer.
- **It is welded to `Session`.** Its four files of the time reached into `Session` 101 times (counted 2026-10-03), and `Session.h` pulls in Tracktion. That is why Forge's planned arp pattern editor cannot simply reuse it: first extract a Session-free note-grid header, as `ClipGeometry.h` was.
- **The clip can vanish under it.** `Session::repairPatternClip` is the single place that re-points the editor when its clip goes: by id first, else to the first MIDI clip on the first MIDI track, else to a hidden starter clip it makes there, never onto an audio track ([No track is special for being first](pattern-track.md), [Hold ids, not pointers](ids-not-pointers.md)).
- **The step region does not survive a session change.** Every rebuild (`rebuildVisibleNotes`, run on every session change) calls `setSelectedStates`, which calls `setStepRegionFromSelection`, so a region from a marquee, a click's insert point or `showTimeSpan` gives way to the selected notes' span rounded out to whole steps, or to nothing when no note is selected. Zoom, scroll and the note selection survive. Read from the code on 2026-10-24, not reproduced in the app.
- **Hidden, it waits.** While another face holds the lower pane the grid marks itself stale instead of rebuilding on every change, and skips its playhead every display refresh ([What a change costs the interface](ui-cost-of-a-change.md)).
- **Octave names.** Middle C reads C3, matching Live, Forge and the Drum Rack's pads (`padNoteName` in `core/content/DrumKitFile.h`, which `DrumRackDevice::noteName` calls). Both of the painter's keyboards name rows through `pitchName` in `StepGridInternal.h` (until 2026-10-06, `drumLaneName`). Change `pitchName` and `padNoteName` together or the rows and the pads disagree.

## Related

- [Note editor velocity lane](note-velocity-lane.md)
- [Arrangement view](arrangement-view.md)
- [Region editing](region-editing.md)
- [Pointer and selection rules](pointer-and-selection.md)
- [Forge arpeggiator](forge-arpeggiator.md)
- [Writing Rhino tests](writing-rhino-tests.md)
- [No track is special for being first](pattern-track.md)
- [Drum Rack](drum-rack.md)
