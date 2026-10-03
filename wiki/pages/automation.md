---
title: Track automation
type: component
summary: Per-track lanes stored as Rhino's own ValueTree children, applied by a 30 Hz UI timer and mirrored into engine curves for renders.
tags: [rhino, automation, arrangement, devices]
sources: []
updated: 2026-10-03
---

# Track automation

Automation belongs to a track and spans the whole timeline; it moved off clips in September 2026 (`HANDOVER.md`, *Automation moved from clips to tracks*). The model is `native/src/SessionAutomation.cpp`; the arrangement's lane rows, hit testing and gestures are `ArrangementAutomation.cpp`, the drawing `ArrangementAutomationPaint.cpp`.

## Storage

A lane is a `rhinoTrackAutomation` child of the track's own state with `slot` (plugin-list index), `parameter` (the rack's exposed-parameter index) and `ownLane`, holding `point` children with `time` in **seconds** and `value`. It rides the project snapshot. A lane keeps no copy of its track index: it is found where it is stored, because deleting a track renumbers every track below.

A lane with fewer than two points is revealed but inert: a dotted line at the knob's resting value that drives nothing. That is what makes *Show automation* safe.

## Reaching it

Right-click a knob in the Device View (`DeviceEditorPanel::showParameterMenu`): *Show automation* draws the lane over the track, *Show automation on new lane* stacks a dimmed copy of the track underneath, *Hide* removes the lane, *Delete* clears its points. On a lane, a click adds a node and starts dragging it; a double-click removes one.

## How it plays

- **Live playback is driven by the UI, not the engine.** While the transport plays, the 30 Hz timer in `Main.cpp` calls `Session::applyTrackAutomationAt(playheadTime)`, which sets each parameter from its curve, so a live sweep moves in timer-rate steps.
- **An export has no timer.** `ProjectFiles::renderWav` calls `beginOfflineAutomation` to mirror each active lane into its parameter's engine curve, which the render reads per sub-block, and `endOfflineAutomation` afterwards; without it every automated parameter would be written frozen. Live and exported automation therefore move at different rates. A lane overridden by hand drives neither.
- **Touching an automated knob overrides it**, as in Live: the knob's **A** button turns amber and the lane stops driving until it is clicked. A lane that stops driving hands the parameter back to the last hand-set value rather than freezing on its final point.
- **Tempo changes rescale every point** by old over new BPM in `Session::setTempo`: points are seconds, and the engine moves clips by beats.

## Limits and traps

- **Targets are positional.** Swapping an instrument, moving a device or deleting one leaves a lane pointing at whatever now sits at that plugin index; nothing re-targets it.
- **The main row has no lanes yet.** `showTrackAutomation` refuses a main-track target because the pinned row has nowhere to stack one, although storage (on the edit's own state) and playback already treat it like any track.

## Related

- [Arrangement view](arrangement-view.md)
- [Device rack and device editors](device-rack.md)
- [The main track](main-track.md)
- [Session, the model](session-model.md)
- [Hold ids, not pointers](ids-not-pointers.md)
- [Transport, tempo and loop](transport.md)
