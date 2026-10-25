---
title: Note editor velocity lane
type: component
summary: A lane under the note editor's rows, modelled on Live 12's velocity editor, with a marker for every note of the clip in MIDI 1-127, edited by drag, draw-mode stroke, marquee and keys, each gesture one undo step announced once at its end.
tags: [rhino, ui, notes, velocity]
sources: []
updated: 2026-10-25
---

# Note editor velocity lane

Since 2026-10-16 the [note editor](note-editor.md) shows and edits each note's velocity in a lane under its rows, modelled on Ableton Live 12's velocity editor (manual section 10.5.12, pp. 272-276; [Development environment and reference material](development-environment.md)). All six CTest cases pass, run twice, including the lane's three scenarios. The user has not tried it in the app yet.

## Files

- `ui/notes/VelocityLaneGeometry.h`, namespace `rhino::velocitylane`, a pure header with no JUCE and no `Session`. It holds the constants (divider 5 px, default height 64, minimum 32, maximum 136 because a taller lane adds no precision, value inset 5, mid-label height 44) and the arithmetic: `laidOutHeight`, `yForVelocity`/`valueAtY`/`velocityAtY`, `clampVelocity`, `clampGroupDelta`, `draggedVelocity`, the stroke helpers (`strokeValueAt`, `columnValue`, `strokeColumns`) and `pickMarker`. Its unit test is `tests/VelocityLaneTest.*`, run by the geometry runner in `tests/Arrangement/ClipGeometryTest.cpp` (CTest `native_arrangement_geometry`).
- Three `StepGrid` units, split from one of 807 lines. `StepGridVelocity.cpp` holds the layout, the footer's Velocity button, the `laneNotes` cache, the Info View tooltip and Ctrl+Up/Down. `StepGridVelocityGestures.cpp` holds the pointer, hover, drag, stroke, marquee and divider. `StepGridVelocityPainter.cpp` paints.
- Scenarios `VelocityLane.inc` ("velocity lane: markers, drags and keys"), `VelocityLaneDraw.inc` ("...drawing, the marquee and the pointer") and `VelocityLaneLayout.inc` ("...the gesture, the divider and short panes"), in `tests/Arrangement/scenarios/`. Each builds a document of its own, and all three are included after `NoteGestures.inc`. `--profile-ui` times "note editor velocity drag step" and "note editor velocity lane repaint".

## Layout

From top to bottom: header, rows, divider, lane, footer. The lane ends at the footer, and the playhead's damage rectangle and the profiler's sweep trim the footer only. Until commit `c257d42` a 14 px scrollbar sat between lane and footer, and the lane went above it so that neither of those changed when the lane came in ([Note editor](note-editor.md#drawing)). `rowAreaHeight()` is `timeAreaHeight()` minus the lane block, which keeps every row hit test out of the lane. The loop band, playhead and insert line run to `timeAreaBottom()`, through the lane.

The lane never leaves the rows less than `Session::pitches` × `StepGrid::octaveLabelRowHeight`, 128 px (`laidOutHeight`'s `rowsFloor`; 8 px is the height below which the keyboard stops naming octaves). Since the rows stopped stretching (commit `d872f0b`) that is a least room for rows, not a squeeze of sixteen: they keep their own height and the pane shows as many as reach into the room ([Note editor](note-editor.md#drawing)). With less than 32 px of room for itself the lane folds away and the Velocity button dims. Since commit `c257d42` only the pane's height decides that, so `resized()` updates the button. Before, a clip wider than the view brought in the scrollbar, which took 14 px from that room and could fold the lane, so `zoomAt` had to recount the rows and `rebuildVisibleNotes` had to settle the scrollbar before computing them.

The default pane is a fifth of the window, about 202 px at 1080p, which folds the lane. So the shell raises the pane to `StepGrid::openingHeight` (327 px since commit `c257d42`: sixteen rows of `StepGrid::baseRowHeight`, 13 px, the lane and the footer; 309 px with 11 px rows and a scrollbar from `d872f0b`, 293 px from `363af51` and 261 px before it, [Note editor](note-editor.md#drawing)) the first time the note editor appears in a run, unless the user has already dragged the pane to a height ([App shell and control bar](app-shell.md)). Dragging the divider resizes the lane, dragging it most of the way down closes it, and a double-click resets it. The lane's visibility and height are not saved.

## Units and the model

Velocities are MIDI 1-127 everywhere, as the user asked. That covers the lane, the footer readout (`selectedVelocity()`, which was `selectedVelocityPercent`) and hold-V, which steps by 1. Percent could not reach about 27 of the 127 values. `clampVelocity` never returns 0, which is a note-off.

`Session::setNoteVelocities` takes absolute values (`Session::NoteVelocity`) and replaces `adjustNoteVelocities`, which took percent deltas. A group drag works from the values the notes had at the press (`clampGroupDelta`). A group pushed into a rail equalises there, and dragging it back off the rail in the same drag restores its spacing.

Lane gestures and held V run inside `beginNoteVelocityGesture`/`endNoteVelocityGesture`. The whole gesture is one undo step, nothing is announced until its end, and the grid re-reads the model after each write (`refreshNoteVelocities`). Each write still marks the track modified, so a DJ deck playing the cell knows it changed. `Session::gestureHeld()` counts the bracket, because holding V presses no mouse button ([What a change costs the interface](ui-cost-of-a-change.md)). Rhino never calls `restartPlayback` for a velocity change: Tracktion's `Edit::TreeWatcher` already restarts playback, coalesced, when any NOTE property except colour changes.

## Gestures

The gestures follow `SelectionInput.h` ([Pointer and selection rules](pointer-and-selection.md)):

- A press on a marker takes its note, a click inside a group collapses onto that note on release, and Ctrl or Shift adds to the selection. A click is a press whose drag never armed. A group dragged against a rail moves nothing, but it was not a click, so it keeps its selection. `pickMarker` takes the nearest dot (within 6 px) or length bar (within 4 px). Only near-ties are ranked: selected first, then notes inside the pitch window, then the marker drawn on top. That way a selected marker cannot steal a press that lands squarely on another marker.
- A press on empty lane starts a marquee, which catches markers by their dot, out-of-window notes included.
- A drag arms after 3 px, and arming moves nothing. Holding Shift drags at a quarter of the speed.
- In draw mode (B) a stroke paints each step column, interpolating along the stroke so a fast stroke misses no column. With Alt it paints per note, and with a selection it paints only the selected notes. The stroke is held to the visible steps, so a pointer parked past the edge paints whatever autoscroll brings in. Without that clamp, the segment lay entirely off screen and nothing was painted.
- Lane gestures autoscroll horizontally only, never in pitch.
- A double-click and a press on the gutter do nothing, because a marker can stand for a note scrolled out of sight. Since commit `4783232` (2026-10-25) the right button opens a menu ([Note editor](note-editor.md#gestures)): on a marker its note's menu, taking the note as a press would; on empty lane the grid's, without Add Note; on the gutter or the divider none. It still erases and drags nothing (`VelocityLaneDraw.inc`).
- Ctrl+Up/Down changes the selection by 10, and Ctrl+Shift+Up/Down by 1.

## Holding the bracket safely

These came out of a multi-agent review on 2026-10-16. Each is fixed and tested.

- **A second press ends the held gesture first.** A second finger, or a pen used beside the mouse, used to overwrite the gesture. That stranded the counted bracket, so `gestureHeld()` stayed true and every later velocity edit went unannounced. `StepGrid::mouseDown` now ends any held gesture, through `mouseUp`, before it starts another.
- **The editor's own keys wait for the release.** While a marker drag or stroke holds the bracket, `keyPressed` consumes every key the editor handles: the arrows, Delete/Backspace, Escape, V, B and F, and Ctrl+Z/Y/L/A/C/X/E/V/D. So no keyboard edit can split the drag's single undo step or remove its note. Keys the editor does not handle, such as the transport's, still pass. The list is a local `const int` array, not a `static constexpr` one ([JUCE's key codes are not constant expressions](juce-keypress-codes-not-constexpr.md)).
- **A stroke stays aligned with the notes.** Its targets stay index-aligned with `laneNotes`, and are re-aligned by note state if a recording take or the Push changes the notes mid-stroke. A tick where neither the pointer nor the view has moved returns at once. Before that, the stroke cost O(N²) on every 60 Hz tick.
- **Hover follows markers that move under a still pointer.** It is recomputed in `rebuildLaneNotes` and at the end of `mouseUp`, so the lit marker is always the one a press would take.

## Drawing

Every note of the clip has a marker. Notes scrolled out of the pitch window are dimmed, their colour moved 0.55 of the way towards the lane's. Hovering a note in the grid lights its marker, which is Live's way of finding a marker inside a stack; the other way round, hovering a marker lights and names its note's key on the keyboard (commit `4783232`). The gutter shows the hovered or dragged value and its note name, the pad name on a drum clip; in a lane under 44 px it shows only the 127 and 1 labels and the value without the name. Markers are drawn with whole-pixel `fillRect` only: an octagonal dot built from five rectangles, plus a bar for the note's length. That keeps partial repaints identical to full ones ([JUCE's rasteriser is not clip-invariant](juce-rasteriser-not-clip-invariant.md)). The lane's colours are `palette` tokens ([Colours and typography](colours-and-typography.md)). Since commit `f2ae477` its columns alternate per beat, as the cells above do, with the darker rule (`velocityLaneBarRule`) on every beat; both used to alternate every 4 steps whatever the resolution ([Note editor](note-editor.md#drawing)). The markers do not take the grid's velocity shading, since their height already shows it.

## Not done yet

Live's Randomize, Ramp and Deviation controls, the Chance lane, typed values, Alt-drag in the grid (Alt now moves and resizes notes off the grid there, since commit `f2ae477`), and saving the lane's visibility and height. Note colour by velocity is done, in the grid (`f2ae477`).

## Related

- [Note editor (StepGrid)](note-editor.md)
- [Pointer and selection rules](pointer-and-selection.md)
- [What a change costs the interface](ui-cost-of-a-change.md)
- [App shell and control bar](app-shell.md)
- [Writing Rhino tests](writing-rhino-tests.md)
- [Keyboard velocity curves](keyboard-velocity-curves.md)
- [JUCE's key codes are not constant expressions](juce-keypress-codes-not-constexpr.md)
