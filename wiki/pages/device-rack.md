---
title: Device rack and device editors
type: component
summary: The Device View strip that shows a track's chain, its per-device faces, and the rebuild rule that shapes how a face is written.
tags: [rhino, devices, ui]
sources: []
updated: 2026-10-03
---

# Device rack and device editors

The Device View is `DeviceRack` (`native/src/DeviceRack.*`). It shares the lower pane with the note editor and the audio clip editor, and exactly one of the three is visible (`LowerPane` in `Main.cpp`); clicking a track card opens devices only while the pane is free; `native/README.md`'s "two independent panels" and `README.md`'s "opening a clip never closes the devices" are stale (checked 2026-10-03). Long form: *The order of a chain* in `native/README.md` and the face paragraph of each device's section there.

## Pieces

- `DeviceRack` shows `Session::deviceSlots(track)`, the plugin list minus the hidden channel strip, as one `DeviceEditorPanel` per device, with a `+` menu built from the [Device catalog](device-catalog.md) and Edit and Delete buttons.
- `DeviceEditorPanel` picks a face by plugin type. The generic face is a grid of up to twelve knobs from `Session::deviceParameters`; `native/src/DeviceMacros.cpp` chooses them for 4OSC and Rhino Wave. Rhino Space's layout is in `DeviceEditorPanel.cpp`; Rhino Tune, Rhino EQ and Rhino Vocoder are extra translation units of the same class (`DeviceEditorPanelAutoTune.cpp`, `DeviceEditorPanelEq.cpp`, `DeviceEditorPanelVocoder.cpp`).
- Edit opens `FloatingDeviceWindow`, still defined inline in `DeviceRack.cpp`: a VST3's own editor (Forge's), else Tracktion's plugin editor, else Rhino's fallback, which holds the large Rhino Wave editor. The previous window is destroyed first, because JUCE allows one active editor per processor.

## The rebuild rule

`DeviceRack::sync` runs on every `Session` change message and destroys and rebuilds every panel whenever the slot list differs: a device added, moved, bypassed or removed, or another track selected. Hence:

- Everything on a face that is not a knob is drawn and hit-tested by rectangle rather than made a child component.
- View state that must survive lives on the device; the EQ's selected band is a device property.
- A face that needs the device itself, not its parameter list, asks `Session::devicePlugin`. Tune, EQ and Vocoder all do; `native/README.md` still says Tune is the only one.

Faces that move on their own (Tune's meter, the vocoder bank, the EQ spectrum) run a 24 Hz timer; the rest ask for no frames.

## Gestures

- A device is reordered by its 23-pixel name bar: a press there arms the drag and four pixels of travel start it, carrying `rhino-device:<track>:<pluginIndex>` with the name bar as its image. A knob is never a drag. The rack tells this from a browser drop by its prefix, finds the nearest gap (`dropGapFor`) and calls `Session::moveDevice` ([Device chain order](device-chain-order.md)).
- Right-clicking a knob opens the automation menu: show, show on a new lane, hide, delete ([Track automation](automation.md)).

## Pitfall: the first track

Track 0 is still the old pattern track here ([The first track is still the pattern track](pattern-track.md)). `deviceSlots` marks its instrument not removable, so Delete is disabled for it, and `deleteDevice` refuses Utility, 4OSC and Drums there ("Core devices stay in the starter track chain").

Tests: `native/src/tests/Pattern/DeviceRackTest.cpp`.

## Related

- [Device chain order](device-chain-order.md)
- [Rhino EQ](rhino-eq.md)
- [Rhino Tune](rhino-tune.md)
- [Rhino Vocoder and sidechains](rhino-vocoder.md)
- [App shell and control bar](app-shell.md)
- [Keeping files small](keeping-files-small.md)
