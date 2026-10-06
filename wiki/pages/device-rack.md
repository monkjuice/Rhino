---
title: Device rack and device editors
type: component
summary: The Device View strip that shows a track's chain and its per-device faces, the rebuild rule that shapes how a face is written, and the timer that follows automated knobs.
tags: [rhino, devices, ui]
sources: []
updated: 2026-10-05
---

# Device rack and device editors

The Device View is `DeviceRack` (`native/src/DeviceRack.*`). It shares the lower pane with the note editor and the audio clip editor, and exactly one of the three is visible (`LowerPane` in `Main.cpp`); clicking a track card opens devices only while the pane is free.

## Pieces

- `DeviceRack` shows `Session::deviceSlots(track)`, the plugin list minus the hidden channel strip, as one `DeviceEditorPanel` per device, with a `+` menu built from the [Device catalog](device-catalog.md) and Edit and Delete buttons.
- `DeviceEditorPanel` picks a face by the device's catalog id (`DeviceSlot::deviceId`). The generic face is a grid of up to twelve knobs from `Session::deviceParameters`; `native/src/DeviceMacros.cpp` chooses them for 4OSC. Rhino Tune, Rhino EQ and Rhino Vocoder are extra translation units of the same class (`DeviceEditorPanelAutoTune.cpp`, `DeviceEditorPanelEq.cpp`, `DeviceEditorPanelVocoder.cpp`).
- A device on the SDK with no face of its own gets a generated one (`DeviceEditorPanelGenerated.cpp`), built from what `Session::deviceParameters` reports: controls grouped under their declared sections, a knob for a continuous control (declared step and skew, double-click to its default), a chooser for a choice, a switch for a toggle, and the A button beside a chooser or switch rather than over its caption. A group fills columns two controls tall when the face has several sections or a long one, otherwise one row of larger knobs. A chooser or switch writes through `writeParameter`, a gesture of its own, so each change is one undo step; the parameter menu adds Reset to default. Rhino FM's face is 35 controls in five sections, about 1200 px wide; Rhino Space's is one row of six. `RHINO_FM_SNAPSHOT` writes the FM face to a PNG ([The native device standard](native-device-standard.md)).
- Edit opens `FloatingDeviceWindow`, still defined inline in `DeviceRack.cpp`: a VST3's own editor (Forge's), else Tracktion's plugin editor, else Rhino's fallback, a window of up to six knobs. It finds the device through `Session::devicePlugin`, so a device on the main track opens its own editor too. The previous window is destroyed first, because JUCE allows one active editor per processor.
- The window holds its device alive (`te::Plugin::Ptr`), and a plugin must not outlive its edit, so the rack closes it when its device leaves the edit (checked on every change, hidden or not) and in `editWillChange`, before a document is replaced. Closing the window destroys it, editor and all, rather than hiding it (commit `bac4059`). No test opens the real top-level window; this was reviewed, not tested.

## The rebuild rule

`DeviceRack::sync` runs on every `Session` change message and destroys and rebuilds every panel whenever the slot list differs: a device added, moved, bypassed or removed, or another track selected. Hence:

- Everything on a face that is not a knob is drawn and hit-tested by rectangle rather than made a child component.
- View state that must survive lives on the device; the EQ's selected band is a device property, written without undo.
- A face that needs the device itself, not its parameter list, asks `Session::devicePlugin`. Tune, EQ and Vocoder all do.
- A setting a face writes onto its device (an EQ band's type, Tune's scale) goes through `Session::editDeviceSettings`, which makes it a named undo step of its own and announces it. Written directly, it joined whatever the user did before, so one Ctrl+Z took back both (commit `63bb3b2`).

Two exceptions keep a drag cheap (commit `e7c9554`; [What a change costs the interface](ui-cost-of-a-change.md)). During a knob drag the session announces only on `deviceParameterValues`, and the rack refreshes just the dragged device's panel (`refreshTouchedDevice`). While the shell hides the rack, a change only marks it stale, and it syncs when shown.

Faces that move on their own (Tune's meter, the vocoder bank, the EQ spectrum) run a 24 Hz timer that does nothing while the rack is hidden; the rest ask for no frames.

## Following automation

The engine moves automated knobs and tells nobody, so the rack reads them back on its own 30 Hz timer: only while the transport rolls (plus once as it stops, so knobs settle where the curve left them), only for faces where `followsAutomation()` finds a knob on a lane not taken over by hand, and not while hidden (commit `fe163c8`; [Track automation](automation.md)).

## Gestures

- A device is reordered by its 23-pixel name bar: a press there arms the drag and four pixels of travel start it, carrying `rhino-device:<track>:<pluginIndex>` with the name bar as its image. A knob is never a drag. The rack tells this from a browser drop by its prefix, finds the nearest gap (`dropGapFor`) and calls `Session::moveDevice` ([Device chain order](device-chain-order.md)).
- Right-clicking a knob opens the automation menu: show, show on a new lane, hide, delete ([Track automation](automation.md)).
- Delete works on every device the rack shows, on any track. `deleteDevice` refuses only the hidden channel strip and takes the device's automation lanes with it in the same undo step. Until commit `a04b407` it refused track 0's Utility, 4OSC and Drums ([No track is special for being first](pattern-track.md)).

Tests: `native/src/tests/Pattern/DeviceRackTest.cpp`.

## Related

- [Device chain order](device-chain-order.md)
- [Rhino EQ](rhino-eq.md)
- [Rhino Tune](rhino-tune.md)
- [Rhino Vocoder and sidechains](rhino-vocoder.md)
- [App shell and control bar](app-shell.md)
- [Keeping files small](keeping-files-small.md)
- [Track automation](automation.md)
- [Hold ids, not pointers](ids-not-pointers.md)
