---
title: Hosting Forge in Rhino
type: component
summary: How Rhino finds, loads and talks to the Forge VST3, and what does and does not cross the plugin boundary.
tags: [both, vst3, devices, hosting]
sources: []
updated: 2026-10-03
---

# Hosting Forge in Rhino

Rhino plays [Rhino Forge](forge.md) as an ordinary VST3 through Tracktion's external-plugin wrapper; the reasons are in [Forge is an independent VST3](forge-is-an-independent-vst3.md). Rhino's side is `native/src/SessionExternalPlugins.cpp` and one catalog entry.

## Discovery

`Session::initialiseExternalPlugins` runs from `Session`'s constructor, and again when Forge is asked for before it was found. It takes the first of:

1. a VST3 in the engine's known-plugin list whose file still exists;
2. the development build, `instruments/rhino-forge/build/RhinoForge_artefacts/{Release,Debug}/VST3/Rhino Forge.vst3`, located from the compile-time `RHINO_SOURCE_DIR`, so only a build made from this checkout finds it;
3. `Rhino Forge.vst3` in the platform's default VST3 folders.

A description qualifies if its name contains "Rhino Forge", or "Forge" with a manufacturer containing "Rhino" (Forge's `COMPANY_NAME`). A loaded instance is recognised by its wrapped processor's name (`isForgePlugin`). The file chosen is written to `%APPDATA%\Rhino\rhino.log`.

## The catalog entry

`RhinoForge` is an instrument with `external = true` and no `typeName`, so `registerBuiltInTypes` skips it and no type-name lookup can match it. The rack's `+` menu greys it until `isForgeAvailable()`; a drop while it is missing fails with "Rhino Forge.vst3 was not found. Build or install the Forge VST3 first." Otherwise it follows [One instrument per track](one-instrument-per-track.md) like any instrument.

## What crosses the boundary

- Rhino never names a Forge parameter. The rack's Edit button opens Forge's own editor; the rack itself shows Forge with the generic face, the first twelve parameters Tracktion exposes.
- Forge's host state is not versioned the way its presets are, so a `.rhinoedit` holding older Forge state is reconciled by `Processor::migrated` when it opens. That state includes any drawn wavetable or loaded spectral sample, so the project file carries them ([Forge presets and state](forge-presets-and-state.md)).
- Rhino's own automation lanes address a parameter by slot and by position in that exposed list (`exposedParameterAt`), not by id, so reordering Forge's parameters would move such lanes onto other parameters.
- The [Computer MIDI keyboard](computer-keyboard.md) uses Forge's own key mapping and octave numbering, so a part lands in the same key in both.

## Pitfalls

- A running RhinoDAW with Forge loaded holds the `.vst3`, and Forge's build then fails to relink with LNK1104. Build the tests and the standalone instead and say the VST3 is stale ([LNK1104 means a running binary holds the file](locked-executable-lnk1104.md)).
- The suite covers Forge only where one can be found: `--self-test` skips external devices, and `native/src/tests/Pattern/scenarios/DeviceParameters.inc` runs its Forge checks only when discovery succeeded. A green run on a machine without a Forge build proves nothing about hosting.
- A VST3 counts as shipped only after it has been loaded and automated in at least one external host.

## Related

- [Forge is an independent VST3](forge-is-an-independent-vst3.md)
- [Rhino Forge](forge.md)
- [Device catalog](device-catalog.md)
- [Forge presets and state](forge-presets-and-state.md)
- [LNK1104 means a running binary holds the file](locked-executable-lnk1104.md)
- [Track automation](automation.md)
