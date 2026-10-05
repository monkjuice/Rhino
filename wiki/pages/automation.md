---
title: Track automation
type: component
summary: Per-track lanes stored as Rhino's own ValueTree children, naming their device by key rather than slot, and played by the engine from parameter curves the session mirrors after every change.
tags: [rhino, automation, arrangement, devices]
sources: []
updated: 2026-10-05
---

# Track automation

Automation belongs to a track and spans the whole timeline; it moved off clips in September 2026. The model is `native/src/SessionAutomation.cpp`; the arrangement's lane rows, hit testing and gestures are `ArrangementAutomation.cpp`, the drawing `ArrangementAutomationPaint.cpp`.

## Storage

A lane is a `rhinoTrackAutomation` child of its track's state (the edit's state for the main track), with `device`, `parameter` (the rack's exposed-parameter index) and `ownLane`, holding `point` children with `time` in **seconds** and `value`. It rides the project snapshot. A lane keeps no copy of its track index, because deleting a track renumbers every track below.

**A lane names its device by key, not by slot** (commit `cf85e3f`). `device` holds the plugin's `rhinoDeviceKey`, a UUID that `ensureDeviceKey` (`SessionInternal.cpp`) writes onto the plugin's state the first time a lane needs one. It is written without undo, because naming a device is not an edit to it, and it is saved and restored with the device. The slot is worked out from the key whenever lanes are read (`slotOfDevice`), so a lane follows its device through a drag along the chain, a device added in front of it or one deleted beside it. A stored slot used to leave the lane driving whichever device moved into that place. Deleting a device, or replacing a track's instrument, deletes its lanes in the same undo step (`removeDeviceLanes`). A document saved with slot-indexed lanes loads without them ([No backward compatibility for .rhinoedit](no-rhinoedit-back-compat.md)).

A lane with fewer than two points is revealed but inert: a dotted line at the knob's resting value that drives nothing. That is what makes *Show automation* safe.

## Reaching it

Right-click a knob in the Device View (`DeviceEditorPanel::showParameterMenu`): *Show automation* draws the lane over the track, *Show automation on new lane* stacks a dimmed copy of the track underneath, *Hide* removes the lane, *Delete* clears its points. On a lane, a click adds a node and starts dragging it; a double-click removes one.

## How it plays

**The engine plays the lanes** (commit `fe163c8`). `Session::mirrorAutomationToEngine` writes every drawn lane that nobody has taken over by hand onto its parameter's own `te::AutomatableParameter` curve, which the audio graph reads every block, live and in a render alike. The shell's 30 Hz timer used to set each parameter from the message thread instead: values moved in frame-sized steps and stalled whenever that thread was busy, every step rebuilt the interface, and a plain render heard no lanes.

- **When it runs.** `markModified` marks the lanes stale, and `AutomationMirror`, a listener on the session's own announcement, mirrors them after any document change, leaving unchanged curves alone. It also runs at once when a knob touch takes a parameter from its lane (inside a drag nothing is announced until release) and when an override is toggled. The WAV export calls it before rendering.
- **It owns every curve in the edit.** A curve no lane drives is cleared rather than played unseen, whether it came from a document saved with its curves, a device restored by undo or one moved to another track. The edit is searched for strays only when its set of plugins changes, compared by `EditItemID`. Curves are never written through the undo manager.
- **Touching an automated knob overrides it**, as in Live: the knob's **A** button turns amber and the lane stops driving until it is clicked. The override is kept by device key, so it survives a reorder. A lane that stops driving hands its parameter back to the value last set by hand rather than freezing on its final point, unless the user has taken that knob over.
- **The rack follows.** The engine tells nobody when it moves a knob, so the Device View reads moving knobs back on its own 30 Hz timer, only while the transport rolls, only for faces with a knob on a lane, and not while hidden ([Device rack and device editors](device-rack.md)).
- **Tempo changes rescale every point** by old over new BPM in `Session::setTempo`: points are seconds, and the engine moves clips by beats.

## Limits and traps

- **The mirror holds plugins alive.** `mirroredCurves` keeps a `te::Plugin::Ptr` per curve, so `restoreProject` and `newProject` clear it before the outgoing edit is destroyed. A plugin outliving its edit corrupted the heap (`0xc0000374`) the first time a document was reopened ([Hold ids, not pointers](ids-not-pointers.md)).
- **A parameter is still named by its index** within the device (`parameter`), so a device's parameter order stays persistent ([Stored indices are append-only](append-only-stored-indices.md)).
- **The main row has no lanes yet.** `showTrackAutomation` refuses a main-track target because the pinned row has nowhere to stack one, although storage (on the edit's own state) and playback already treat it like any track.

## Related

- [Arrangement view](arrangement-view.md)
- [Device rack and device editors](device-rack.md)
- [Device chain order](device-chain-order.md)
- [The main track](main-track.md)
- [Session, the model](session-model.md)
- [Hold ids, not pointers](ids-not-pointers.md)
- [Transport, tempo and loop](transport.md)
