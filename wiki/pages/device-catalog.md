---
title: Device catalog
type: component
summary: The one table of Rhino's devices that the browser, drop targets, rack menu, engine registration and instrument rules all read.
tags: [rhino, devices, catalog]
sources: []
updated: 2026-10-05
---

# Device catalog

`native/src/devices/DeviceCatalog.h/.cpp` is the one list of Rhino's devices. Before it (commit `db5b174`, 2026-09-19) a device's metadata lived in six places: an enum in `Session.h`, switches in `SessionDevices.cpp` and `SessionInternal.cpp`, and rows in `BrowserPanel.cpp`, `BrowserIds.h` and `DeviceRack.cpp`. Miss one and the device half-worked.

## An entry

A `DeviceDescriptor` holds a stable `id`; the engine `typeName` (empty for an external plugin); `displayName` and an optional `browserLabel` ("4OSC synth" in the browser, "4OSC" on the track); `kind`; the browser `category` and `description`; an ARGB `colour` kept as a number so the library needs no `juce_graphics`; a `patternKey`, which nothing has read since the edit's `rhinoPatternInstrument` property was retired in commit `a04b407`; and three flags: `browsable`, `external` (found by scanning, not created by name) and `infrastructure` (a channel-strip facility the rack hides). Entry order is browser order.

As of 2026-10-05 it lists 4OSC, Rhino Forge (external), Rhino Drums (not browsable; the browser offers its five kits), Rhino EQ, Compressor, Utility (infrastructure), Reverb, Delay, Rhino Space, Rhino Bloom, Rhino Tune, Rhino Vocoder and Rhino Arp ([Built-in devices](built-in-devices.md)). Rhino Wave was removed that day.

## Readers

The browser's device rows; the drop targets (`deviceFromId` in `native/src/BrowserIds.h`, `deviceFromBrowserDrop` in `native/src/DeviceRack.cpp`); the rack's `+` menu, which leaves out infrastructure and greys an external device until it is found; `Session::addDevice`, which dispatches on `kind`; the instrument rules (`isInstrumentPlugin`, the MIDI-effect skip in `switchTrackInstrument`); and `registerBuiltInTypes`, called once from `Session`'s constructor to teach the engine Rhino's eight own types. Tracktion's built-ins need no registration and Forge arrives as a VST3 ([Hosting Forge in Rhino](forge-hosting.md)).

## Rules

- **Ids and type names never change.** Browser drags carry the id (`rhino-browser:<kind>:<id>`), and presets and `Session::AudioEffect` name devices by it, which is why Rhino EQ kept `Equaliser`, the id of the Tracktion EQ it replaced. Saved documents store the `typeName` (`rhino.<name>.v1`).
- The `Instrument`, `AudioEffect` and `MidiEffect` enums survive as shorthand because presets and about a hundred call sites name devices with them; `native/src/DeviceIds.h` is the only place they become ids. New devices get no enum.
- `byTypeName("")` answers null on purpose, so nothing can match Forge by accident.
- Never register a device from a static initialiser: the linker drops unreferenced objects of a static library and the registration vanishes without an error.
- `--self-test` asserts that every browsable entry has a browser row, every device row names a real entry, every non-external entry can be added, and an unknown id is refused.

## Not yet the only list

Effects are fully catalog-driven; instruments are not. The paused session view's slot menu lists three instruments by hand (4OSC, Drums and Forge), and `native/src/DeviceMacros.cpp` curates rack knobs for 4OSC only ([Adding a device to Rhino](adding-a-device.md)). A MIDI clip moved or pasted to another track carries its instrument by catalog entry (`carriedInstrument`), so that path already knows every instrument.

## Related

- [Built-in devices](built-in-devices.md)
- [Adding a device to Rhino](adding-a-device.md)
- [Device rack and device editors](device-rack.md)
- [Browser and library preview](browser.md)
- [Stored indices are append-only](append-only-stored-indices.md)
- [Rhino's build targets](rhino-build-targets.md)
