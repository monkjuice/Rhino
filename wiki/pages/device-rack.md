---
title: Device rack and device editors
type: component
summary: The Device View strip that shows a track's chain and its per-device faces, the rebuild rule that shapes how a face is written, and the timer that follows automated knobs.
tags: [rhino, devices, ui]
sources: []
updated: 2026-10-07
---

# Device rack and device editors

The Device View is `DeviceRack` (`native/src/DeviceRack.*`). It shares the lower pane with the note editor and the audio clip editor, and exactly one of the three is visible (`LowerPane` in `Main.cpp`); clicking a track card opens devices only while the pane is free.

## Pieces

- `DeviceRack` shows `Session::deviceSlots(track)`, the plugin list minus the hidden channel strip, as one `DeviceEditorPanel` per device, with a `+` menu built from the [Device catalog](device-catalog.md) and Edit and Delete buttons.
- `DeviceEditorPanel` picks a face by the device's catalog id (`DeviceSlot::deviceId`). The generic face is a grid of up to twelve knobs from `Session::deviceParameters`; `native/src/DeviceMacros.cpp` chooses them for 4OSC. Rhino Tune, Rhino EQ, Rhino Vocoder and the Drum Rack are extra translation units of the same class (`DeviceEditorPanelAutoTune.cpp`, `DeviceEditorPanelEq.cpp`, `DeviceEditorPanelVocoder.cpp`, and six `DeviceEditorPanelDrum*.cpp` files).
- A device on the SDK with no face of its own gets a generated one (`DeviceEditorPanelGenerated.cpp`), built from what `Session::deviceParameters` reports: controls grouped under their declared sections, a knob for a continuous control (declared step and skew, double-click to its default), a chooser for a choice, a switch for a toggle, and the A button beside a chooser or switch rather than over its caption. A group fills columns two controls tall when the face has several sections or a long one, otherwise one row of larger knobs. A chooser or switch writes through `writeParameter`, a gesture of its own, so each change is one undo step; the parameter menu adds Reset to default. Sections that share a tab group stand in one place, one at a time under a row of tabs; the open tab is remembered per device for the session, because the rack rebuilds its panels whenever the chain or the track changes. A device that describes a display (`NativeDevice::describe`, `core/DeviceDisplay.h`) gets it after its first group, drawn by `DeviceEditorPanelDisplay.cpp`: a diagram laid out from its links, with outputs wired to a bus and every other block above what it feeds, and traces at one shared scale. A click on a tab or a diagram block opens that section, and both name their role in a tooltip. Everything is placed and stroked at layout, and a repaint that misses the display or a section's tabs skips them. Rhino FM's face is the voice, the display and its operators as tabs, 854 px; Rhino Space's is one row of six. `RHINO_FM_SNAPSHOT` writes the FM face to a PNG ([The native device standard](native-device-standard.md)).
- The [Drum Rack](drum-rack.md)'s face (`Face::DrumRack`, 852 px, `DeviceEditorPanel::drumFaceWidth`) spans six units of the class that share `DeviceEditorPanelDrumsInternal.h`: `DeviceEditorPanelDrums.cpp` (the map, the pads, the knobs, frames), `DeviceEditorPanelDrumGestures.cpp` (pointer handling: clicks, the pad drag, the wheel, tooltips), `DeviceEditorPanelDrumParts.cpp` (layout and drawn controls), `DeviceEditorPanelDrumMenus.cpp` (pad, choke, rename, save and kit menus), `DeviceEditorPanelDrumSample.cpp` (the selected pad's side and picture) and `DeviceEditorPanelDrumSampleControls.cpp` (the sample knobs and their drag protocol). Left to right it shows a map of all 128 notes, 4 columns by 32 rows of 6×4 px cells with the lowest at the bottom: the bank framed, filled notes lit, the selected pad in the accent colour and an arriving key flashing orange. Then the bank's sixteen pads, four by four with the lowest note bottom left, each with its name, a sample or synth dot, and M, play and S. Then the selected pad: its name and note, sound and choke choosers, CLASSIC, 1-SHOT and SLICE beside its picture, and a row of cells holding the six pad knobs (TUNE, DECAY, TONE, VEL, LEVEL, PAN), a divider and the sample's settings for its mode ([Drum Rack sample editor](drum-rack-sample-editor.md)). A click or drag on the map moves the bank so the row under the pointer becomes its second row; the wheel over the map or the pads moves it a row. The name bar's right end shows `KEY <note>` for the last key to arrive, bright then dim, so a controller on the wrong octave says so at once. The knobs are the face's only child components. The pad knobs follow the selected pad as the EQ's follow its band, resolving their parameter (`DrumRackDevice::parameterIndex`) when touched, and the face reads only that pad's six (`readDrumParameters`) rather than all 768. Everything else is drawn and hit-tested by rectangle. M and S are each an undo step through `editDeviceSettings`. Right-clicking a pad offers Play, Sample..., Synth, Choke group, Slice to pads, Rename..., Save as drum preset... and Clear pad. The name bar's menu lists kits and *Save kit...* instead of `.rnd` presets, saving through `askToSaveFile` in `DeviceEditorPanelPresets.cpp`, which device presets share. A 24 Hz timer flashes struck pads and arriving keys from the engine's counters, moves the playhead and collects released samples. `repaintDrums` repaints only the selected pad's side unless something the pads or the map show has changed ([What a change costs the interface](ui-cost-of-a-change.md)). `RHINO_DRUMS_SNAPSHOT` writes the face to a PNG.
- Right-clicking a device's name bar opens its presets menu (`DeviceEditorPanelPresets.cpp`): its factory and user presets to load, and *Save preset...*, which asks for a name, writes to the user folder, asks before replacing, and calls `presetsChanged` so the shell refreshes the browser. A preset dropped on a panel showing the same device loads into it; dropped elsewhere in the chain it adds the device, already set ([Device presets (.rnd)](device-presets.md)).
- Sounds dropped on a Drum Rack panel land on its pads. A browser sample or drum preset goes to the pad under the pointer, or to the selected pad when dropped between pads, and that pad lights while the drag hovers (`drumPanelAt`, `DeviceEditorPanel::drumPadAt`, `showDrumDropTarget`). A kit loads into the rack. Dropped elsewhere in the chain, a kit or drum preset brings a rack as it does on a lane. `DeviceRack` is also a `juce::FileDragAndDropTarget`, so sound files from the desktop fill pads upward from the pad dropped on, stopping at note 127, each its own undo step.
- Edit opens `FloatingDeviceWindow`, still defined inline in `DeviceRack.cpp`: a VST3's own editor (Forge's), else Tracktion's plugin editor, else Rhino's fallback, a window of up to six knobs, which reads only the six it shows: on a Drum Rack, with 768 controls and no editor of its own, the selected pad's. It finds the device through `Session::devicePlugin`, so a device on the main track opens its own editor too. The previous window is destroyed first, because JUCE allows one active editor per processor.
- The window holds its device alive (`te::Plugin::Ptr`), and a plugin must not outlive its edit, so the rack closes it when its device leaves the edit (checked on every change, hidden or not) and in `editWillChange`, before a document is replaced. Closing the window destroys it, editor and all, rather than hiding it (commit `bac4059`). No test opens the real top-level window; this was reviewed, not tested.

## The rebuild rule

`DeviceRack::sync` runs on every `Session` change message and destroys and rebuilds every panel whenever the slot list differs: a device added, moved, bypassed or removed, or another track selected. Hence:

- Everything on a face that is not a knob is drawn and hit-tested by rectangle rather than made a child component.
- View state that must survive lives on the device; the EQ's selected band is a device property, written without undo. So are the Drum Rack's selected pad and shown bank (`selPad`, `firstNote`); `Session::showDrumBank` announces a bank change, without undo, so the note editor's drum rows follow.
- A face that needs the device itself, not its parameter list, asks `Session::devicePlugin`. Tune, EQ and Vocoder all do.
- A setting a face writes onto its device (an EQ band's type, Tune's scale) goes through `Session::editDeviceSettings`, which makes it a named undo step of its own and announces it. Written directly, it joined whatever the user did before, so one Ctrl+Z took back both (commit `63bb3b2`).

Two exceptions keep a drag cheap (commit `e7c9554`; [What a change costs the interface](ui-cost-of-a-change.md)). During a knob drag the session announces only on `deviceParameterValues`, and the rack refreshes just the dragged device's panel (`refreshTouchedDevice`). While the shell hides the rack, a change only marks it stale, and it syncs when shown.

Faces that move on their own (Tune's meter, the vocoder bank, the EQ spectrum, the Drum Rack's pad and key flashes and playhead) run a 24 Hz timer that does nothing while the rack is hidden; the rest ask for no frames.

## Following automation

The engine moves automated knobs and tells nobody, so the rack reads them back on its own 30 Hz timer: only while the transport rolls (plus once as it stops, so knobs settle where the curve left them), only for faces where `followsAutomation()` finds a knob on a lane not taken over by hand, and not while hidden (commit `fe163c8`; [Track automation](automation.md)).

## Gestures

- A device is reordered by its 23-pixel name bar: a press there arms the drag and four pixels of travel start it, carrying `rhino-device:<track>:<pluginIndex>` with the name bar as its image. A knob is never a drag. The rack tells this from a browser drop by its prefix, finds the nearest gap (`dropGapFor`) and calls `Session::moveDevice` ([Device chain order](device-chain-order.md)).
- A filled Drum Rack pad is dragged by its body onto another note, to move it there or swap it with what is there (commit `6af25c2`; [Drum Rack](drum-rack.md)). The face draws and hit-tests this drag itself; it is not a JUCE drag-and-drop like the device reorder. A press arms it (`DrumDrag::pad`) and 4 px of travel start it, so a click that wavers stays a click. The target is a bank pad, else any note on the map (`drumNoteAt`). A pad target lights through `showDrumDropTarget`, which now repaints map cells too, and a map target is outlined in `palette::selection`. A ghost, the pad's name in a pad-coloured pill with an accent border, is drawn beside the pointer at (+10, +8), not under it, so the target stays visible (`drumGhostArea`); the cursor becomes `DraggingHandCursor`. On release the face calls `Session::moveDrumPad` and reports "Moved X to <note>" or "Swapped X and Y".
- Right-clicking a knob opens the automation menu: show, show on a new lane, hide, delete ([Track automation](automation.md)).
- A menu for one spot on a face opens there: `PopupMenu::Options().withMousePosition()`, as the Drum Rack's pad and choke-group menus do, or `withTargetScreenArea` for the area clicked, as Tune's and the Vocoder's choosers do. `withTargetComponent(this)` alone anchors a menu to the whole panel, away from what was clicked; Rhino EQ's band-type menu (`showEqTypeMenu`, `DeviceEditorPanelEq.cpp`) still opens that way (2026-10-06).
- Delete works on every device the rack shows, on any track. `deleteDevice` refuses only the hidden channel strip and takes the device's automation lanes with it in the same undo step. Until commit `a04b407` it refused track 0's Utility, 4OSC and Drums ([No track is special for being first](pattern-track.md)).

Tests: `native/src/tests/Pattern/DeviceRackTest.cpp`, where `runDrumRackFaceTest` covers the Drum Rack's face.

## Related

- [Device chain order](device-chain-order.md)
- [Drum Rack](drum-rack.md)
- [Drum Rack sample editor](drum-rack-sample-editor.md)
- [Rhino EQ](rhino-eq.md)
- [Rhino Tune](rhino-tune.md)
- [Rhino Vocoder and sidechains](rhino-vocoder.md)
- [App shell and control bar](app-shell.md)
- [Keeping files small](keeping-files-small.md)
- [Track automation](automation.md)
- [Hold ids, not pointers](ids-not-pointers.md)
