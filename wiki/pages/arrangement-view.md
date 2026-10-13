---
title: Arrangement view
type: component
summary: The timeline UI, one class across eleven files, that previews every drag locally and leaves panel decisions to the shell.
tags: [rhino, ui, arrangement]
sources: []
updated: 2026-10-10
---

# Arrangement view

`Arrangement` is the timeline: track cards on the left, clip lanes on the right, the pinned [main track](main-track.md) row and the time ruler at the foot. It is one class defined across eleven translation units (as of 2026-10-03): `Arrangement.cpp` (keys, zoom, split, playhead), `ArrangementGeometry.cpp` (lanes, time to x, snapping, hit zones), `ArrangementPainter.cpp`, `ArrangementSync.cpp` (rebuilding views and card controls from the edit), `ArrangementGestures.cpp` (pointer, card drags, menus, autoscroll, middle-drag pan), `ArrangementSelection.cpp` (region and clipboard), `ArrangementAutomation.cpp` and `ArrangementAutomationPaint.cpp`, `ArrangementGroups.cpp`, `ArrangementRename.cpp` and `ArrangementDrops.cpp`. Shared helpers are in `ArrangementInternal.h`; the clip-edit arithmetic is the pure header `ClipGeometry.h`, unit-tested without a `Session`.

## How it stays responsive and correct

- **Preview, then one commit.** A drag edits a local `preview`; `Session::editClip`, or `Session::moveClips` for several clips, and its undo transaction run once, on release. Mid-drag, a change notification only repaints. Arrow nudges move the whole selection the same way (commit `b5d17e0`), and Left and Right are taken only when there is a clip to move, otherwise reaching the window (commit `d20faa4`).
- **Clip views are a cache.** `sync()` rebuilds the `ClipView` list on each change notification, never per paint; waveforms share one thumbnail per source file. A `Session` call announces synchronously, so the list can be rebuilt before the call returns: copy a `ClipView` before calling `Session` if you read it afterwards (commit `2df76f8`). The sync also reads each track's facts once (`TrackFacts`), and the painter skips whatever a repaint does not reach ([What a change costs the interface](ui-cost-of-a-change.md)).
- **Ids, not pointers.** Selection stores `te::EditItemID`s, because a `te::Clip*` dies on undo or project replacement ([Hold ids, not pointers](ids-not-pointers.md)).
- **`rows` is the vertical authority.** Revealed automation lanes stack extra rows under a track and collapsed group members get zero-height rows, so `lane(track)` looks the track up in `rows` rather than multiplying.

## Rules worth knowing

- **A clip is two rows.** The top strip (`clipHeaderHeight`: half the clip, clamped to 8-22 px) selects and carries it; the body behaves like empty lane, so a press there sets the selection line and a drag sweeps a region. Edges trim at any height.
- **The selection line is where playback starts.** `moveTransportToSelectionStart` always calls `Session::setPlaybackStart`, but moves the transport only while stopped and not counting in.
- **Two snapping rules.** Clip edges round to the nearest line (`snapped`); a pointer position takes the cell it is inside (`snappedDown`/`snappedUp`). The grid is adaptive by default (`ArrangementGrid.h`).
- **The bar ruler labels counts too.** Bar numbers step by powers of two bars, 44 px apart (`barLabelStep`, `barNumberMinimumPixels`). Once they are one bar apart, `paintBarNumbers` also labels counts as `bar.count` with short marks between, like Live. A count is the signature's denominator note (`timeSignature().numerator` per bar, so 6/8 reads 1.2 to 1.6 in eighths), not the engine's quarter-note beat (`beatsPerBar()`). Every span comes from one chain in `ArrangementGrid.h`, `finerRulerSpan`, where each span divides the previous: 4/4 runs 4, 2, 1, 1/2, 1/4; 6/8 runs 6, 3, 1; 7/8 runs 7, 1. That chain is what makes every mark land on a division of the labels either side. `rulerLabelSpan` keeps labels 44 px apart and never goes finer than a count. `rulerTickSpan` keeps marks 10 px apart and stops at a quarter count. The ruler ignores the snap grid, and the time ruler's rule (every labelled time sits on a numbered bar) is unchanged. Unit-tested in `tests/Arrangement/ClipGeometryTest.cpp`.
- **`Focus`** records what the last click selected (clip, track, automation lane, region or nothing), and Delete dispatches on it.
- **Autoscroll runs on the frame clock** (`VBlankAttachment`), because a pointer held still past the edge sends no drag events.
- **It reports, the shell decides.** `clipSelected`, `clipOpened`, `trackSelected` and `trackFocused` are callbacks; which panel opens is the [app shell](app-shell.md)'s call.
- **Cards.** A card's bottom edge resizes its row and its body carries the track through the stack (`Session::moveTrack`), both previewed until release. Row height (`rhinoLaneHeight`, 0 means fit the panel) is a view setting written without undo; colour and name are undoable. Renaming puts one `TextEditor` over the painted name.

## Clips that leave the panel

A clip dragged up out of the panel by more than the band where the lanes scroll (24 px above the top) stops being a move (one clip leaves, the primary selection, whatever else is selected): `Arrangement::mouseDrag` cancels the gesture, so the clip stays put, and starts a window-wide drag carrying `rhino-clip:<id>` (`ClipDrag.h`) with a small tile for a picture. The DJ toggle in the control bar shows the consoles while that drag hovers it, and a console's clips grid takes a copy. The lanes take the reverse: a console's cell dropped on a lane is copied there at the drop point, held to the lane's kind (`ArrangementDrops.cpp`, `rhino-slot:`). See [DJ view and the booth](dj-view.md).

## Related

- [Pointer and selection rules](pointer-and-selection.md)
- [Region editing](region-editing.md)
- [Clips never overlap](clip-placement.md)
- [Track automation](automation.md)
- [Playhead rendering](playhead.md)
- [Track and clip colours](track-and-clip-colours.md)
