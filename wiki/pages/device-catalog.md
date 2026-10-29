---
title: Device catalog
type: component
summary: The one table of Rhino's devices that the browser, drop targets, rack menu, engine registration and instrument rules all read.
tags: [rhino, devices, catalog]
sources: []
updated: 2026-10-28
---

# Device catalog

`native/src/devices/DeviceCatalog.h/.cpp` is the one list of Rhino's devices. Before it (commit `db5b174`, 2026-09-19) a device's metadata lived in six places: an enum in `Session.h`, switches in `SessionDevices.cpp` and `SessionInternal.cpp`, and rows in `BrowserPanel.cpp`, `BrowserIds.h` and `DeviceRack.cpp`. Miss one and the device half-worked.

## An entry

A `DeviceDescriptor` holds a stable `id`; the engine `typeName` (empty for an external plugin); `displayName` and an optional `browserLabel` ("4OSC synth" in the browser, "4OSC" on the track); `kind`; the browser `category` and `description`; an ARGB `colour` kept as a number so the library needs no `juce_graphics`; three flags: `browsable`, `external` (found by scanning, not created by name) and `infrastructure` (a channel-strip facility the rack hides); and `create`, the factory that makes one of Rhino's own devices, which Tracktion's built-ins and an external plugin leave empty. Entries are written with designated initialisers, so each field is named where it is set. Entry order is browser order. The `patternKey` field went in commit `71842c2`: nothing had read it since `a04b407` retired the edit's `rhinoPatternInstrument` property.

Since 2026-10-26 an external entry also says what its plugin is: `pluginName`, what the wrapped processor calls itself, by which a loaded instance is recognised (`externalDevice` in `SessionInternal.cpp`) and a scan's description matched; `bundle`, the `.vst3` file name looked for in the platform's VST3 folders; `developmentArtefacts`, the development build's artefacts folder relative to the repository root, searched first; and `rackParameters`, the parameter names its rack shows before anyone maps a knob ([Mapped parameters](mapped-parameters.md)). Until then every one of those facts about Forge was spelled in the model, and a second external device meant a second set of special cases ([Hosting Forge in Rhino](forge-hosting.md)).

As of 2026-10-06 it lists 4OSC, Rhino FM, Rhino Forge (external), Drum Rack, Rhino EQ, Compressor, Utility (infrastructure, and not browsable since commit `23de20c`, because every track already carries one), Reverb, Delay, Rhino Space, Rhino Bloom, Rhino Tune, Rhino Vocoder and Rhino Arp ([Built-in devices](built-in-devices.md)). Rhino Wave was removed on 2026-10-05. On 2026-10-06 the Drum Rack replaced Rhino Drums, which was not browsable because the browser offered its five kits instead. On 2026-10-12 the DJ booth's two hidden track ends joined, `DjTrackInput` and `DjTrackOutput`: infrastructure, not browsable, each with a factory, made with every track by `Session` rather than added by anyone, and saved with it ([DJ decks play live through the track's chain](dj-decks-play-live-through-the-track.md)). On 2026-10-26 [Rhino Duck](rhino-duck.md) joined as the second external entry, an audio effect under Dynamics beside Compressor. On 2026-10-27 five of Rhino's own audio effects joined, each with a factory:
- [Amp](amp.md), [Saturator](saturator.md), [Redux](redux.md) and [Erosion](erosion.md) under a new Distortion category, placed after Utility and before Reverb;
- [Tuner](tuner.md) under a new Utilities category, after Rhino Vocoder. Utility itself stays under Dynamics.

On 2026-10-28 the Compressor, until then Tracktion's own, gained a factory (`CompressorDevice`) and the type name `rhino.compressor.v1`, keeping its id ([Compressor](compressor.md)).

## Readers

The browser's device rows; the drop targets (`deviceFromId` in `native/src/ui/browser/BrowserIds.h`, `deviceFromBrowserDrop` in `native/src/ui/rack/DeviceRack.cpp`); the rack's `+` menu, which leaves out infrastructure and greys an external device until it is found; `Session::addDevice`, which dispatches on `kind`; the instrument rules (`isInstrumentPlugin`, the MIDI-effect skip in `switchTrackInstrument`); `Session::initialiseExternalPlugins`, which scans for every entry flagged `external` by its `pluginName`, `bundle` and `developmentArtefacts`; `defaultMappedIds` in `SessionMappedParameters.cpp`, which reads `rackParameters`; and `registerBuiltInTypes`, called once per engine from `Session`'s constructor, which registers every entry that carries a factory: nine on 2026-10-06, eleven on 2026-10-12, eighteen on 2026-10-27 and nineteen on 2026-10-28. It used to hold a `createBuiltInType<T>()` line per device, a second list to keep in step with the entries (until commit `71842c2`). It runs after Tracktion has registered its own built-ins, which then need no registration and whose names cannot be taken over ([Tracktion refuses a second built-in type under a name it already has](tracktion-refuses-duplicate-built-in-type.md)) and Forge arrives as a VST3 ([Hosting Forge in Rhino](forge-hosting.md)).

## Rules

- **Ids and type names never change.** Browser drags carry the id (`rhino-browser:<kind>:<id>`), and presets and `Session::AudioEffect` name devices by it, which is why Rhino EQ kept `Equaliser`, the id of the Tracktion EQ it replaced. The Drum Rack likewise kept `Drums`, so `Session::Instrument::Drums` and the drum patterns still name it. Saved documents store the `typeName` (`rhino.<name>.v1`). A replacement with a different state gets a new one: the Drum Rack is `rhino.drumrack.v1`, and nothing reads `rhino.drums.v1` ([Drum Rack](drum-rack.md)). The Compressor's changed from `compressor` to `rhino.compressor.v1` on 2026-10-28 because the engine keeps that name for its own; a document saved before opens it as the engine's plain plugin, with no catalog id.
- The `Instrument`, `AudioEffect` and `MidiEffect` enums survive as shorthand because presets and about a hundred call sites name devices with them; `native/src/model/DeviceIds.h` is the only place they become ids. New devices get no enum.
- `byTypeName("")` answers null on purpose, so nothing can match Forge by accident.
- Never register a device from a static initialiser: the linker drops unreferenced objects of a static library and the registration vanishes without an error.
- `--self-test` asserts that every browsable entry has a browser row, every device row names a real entry, every non-external entry can be added, and an unknown id is refused.
- `--device-test` renders every entry with a factory and holds it to [the native device standard](native-device-standard.md).

## Not yet the only list

Effects are fully catalog-driven; instruments nearly so. `native/src/model/SessionDeviceMacros.cpp` (`DeviceMacros.cpp` until 2026-10-13) curates rack knobs for 4OSC only ([Adding a device to Rhino](adding-a-device.md)); the session view's slot menu, which listed three instruments by hand, went with that view on 2026-10-11. A MIDI clip moved or pasted to another track carries its instrument by catalog entry (`carriedInstrument`), so that path already knows every instrument.

## Related

- [Built-in devices](built-in-devices.md)
- [Drum Rack](drum-rack.md)
- [Adding a device to Rhino](adding-a-device.md)
- [Device rack and device editors](device-rack.md)
- [Browser and library preview](browser.md)
- [Stored indices are append-only](append-only-stored-indices.md)
- [Rhino's build targets](rhino-build-targets.md)
- [The native device standard](native-device-standard.md)
- [Compressor](compressor.md)
