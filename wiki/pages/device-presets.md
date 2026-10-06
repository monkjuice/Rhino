---
title: Device presets (.rnd)
type: component
summary: A device's settings saved as a .rnd file by parameter id, filed under the device in the browser, dragged to add the device already set, and loaded or saved from its name bar.
tags: [rhino, devices, presets, library, browser]
sources: []
updated: 2026-10-05
---

# Device presets (.rnd)

A `.rnd` file is a preset of one device: Rhino Arp's `Super Creative Arpeggiation.rnd` is a preset of Rhino Arp. The device itself has no file and stays code, kept apart from the core. The user settled it this way on 2026-10-05, correcting an earlier plan in which `.rnd` was the device ([The native device standard](native-device-standard.md)).

## The file

`native/src/core/DevicePreset.*`, in `RhinoCore`, so it is tested without a `Session`:

```xml
<RHINO_PRESET format="1" device="RhinoArp">
  <PARAM id="style" value="3"/>
</RHINO_PRESET>
```

- **`device` is the catalog id.** Each value is stored under its control's engine parameter id, in the control's own units. The ids are what documents store and are never renamed, so every built-in device has presets, whether or not it is written on the SDK; the Arp is not.
- **The name is the file name**, so renaming the file renames the preset.
- **Values round-trip exactly.** They are written with `std::to_chars` and read with `std::from_chars`; JUCE's decimal formatting does not promise that.
- **Format 1 is all that is read.** Anything else is refused with a message, because no compatibility is owed before a production release. The version is written so that it can be owed then.

## Where presets live

`ContentLibrary::presets()` returns the factory set under `library/Presets/<device id>/`, then the person's own under `ContentLibrary::userPresets()`. That is `Documents/Rhino/Presets/<device id>/`, or `RHINO_USER_PRESETS_DIR`. The folder names the device, so the browser files a preset without opening it. Presets are read from disk on every call, since there are few and one just saved must show. The factory set on 2026-10-05:
- Rhino FM: Glass Bells, Hard Bass, Brass Section and Drawbar Organ;
- Rhino Space: Small Room, Vast Hall and Grit Plate;
- Rhino Arp: Super Creative Arpeggiation, Straight Sixteenths and Triplet Cascade.

They are generated so that each lists every control, and the scratch script that wrote them is not kept.

## Applying one

`SessionDevicePresets.cpp`, not `SessionPresets.cpp`, which holds the pattern presets:
- **`saveDevicePreset(track, slot, file)`** stores every active control's base value: what was set, not where a lane has moved it.
- **`loadDevicePreset(track, slot, file)`** is one undo step. It refuses a preset for another device, naming the device it is for.
- **`addDeviceFromPreset(file, track)`** is `addDevice(id, track, &preset)`. The values are set inside the add's own transaction, so a single undo removes the device and its settings together. An instrument the track already runs takes the preset instead, as a second copy would replace it.
- **A control the preset leaves out goes to its default**, so a preset sounds the same whatever was there before. Values are snapped to each control's range, and unchanged values are not written, so they add nothing to undo.

Only non-external, non-infrastructure catalog devices have presets. A VST3 keeps its sound in state that a list of parameters does not capture.

## In the interface

- **Browser:** a device's row holds its presets, the way Live files them. Dragging the device row adds the device at its defaults. Dragging a preset carries `rhino-browser:device-preset:<path>` (`BrowserIds.h`), and a search that finds a preset shows its device row, open ([Browser and library preview](browser.md)).
- **Arrangement:** a preset drop adds the device. A drop below the last lane makes the lane that device needs, and the main track takes only an effect's preset.
- **Device rack:** a preset dropped on a panel showing the same device loads into it; dropped anywhere else in the chain, it adds the device. Right-clicking a device's name bar lists its presets to load and offers *Save preset...*. That asks for a name, saves to the user folder, asks before replacing, and refreshes the browser through `presetsChanged` ([Device rack and device editors](device-rack.md)).

## Tests

`native/src/tests/PresetTest.cpp`, run by `--device-test`, covers:
- the format's round trip and the refusals;
- every factory preset, which must be filed under its device and name only controls the device has, at values it can hold;
- adding, saving, loading, defaults, one undo step, refusal by the wrong device, an instrument already present, an effect, the Arp, and the scan of a user folder.

Moving the preset out of the add's transaction fails *one undo takes away the device and its preset together*. `--self-test` checks that every preset is a browser row carrying its file. `BrowserDrops.inc` and the rack test drop presets. The menu's modal save and replace dialogs are untested, and so is how the opened browser tree looks: a headless snapshot shows an opened folder with no children ([Seeing the UI without taking the screen](headless-ui-snapshots.md)).

## Related

- [The native device standard](native-device-standard.md)
- [Content library](content-library.md)
- [Browser and library preview](browser.md)
- [Device rack and device editors](device-rack.md)
- [Device catalog](device-catalog.md)
- [Pattern presets](pattern-presets.md)
