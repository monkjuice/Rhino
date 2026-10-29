---
title: Hosting Forge in Rhino
type: component
summary: How Rhino finds, loads and talks to the Forge VST3 and, since 2026-10-26, to any external entry of the catalog (Rhino Duck is the second), and what does and does not cross the plugin boundary.
tags: [both, vst3, devices, hosting]
sources: []
updated: 2026-10-29
---

# Hosting Forge in Rhino

Rhino plays [Rhino Forge](forge.md) as an ordinary VST3 through Tracktion's external-plugin wrapper; the reasons are in [Forge is an independent VST3](forge-is-an-independent-vst3.md). Rhino's side is `native/src/model/SessionExternalPlugins.cpp` and one catalog entry. Since 2026-10-26 the same unit serves every entry flagged `external`, and [Rhino Duck](rhino-duck.md), an audio effect, is the second: the entry carries the plugin's name, its bundle's file name, its development artefacts folder and the parameters its rack shows first ([Device catalog](device-catalog.md)), and nothing in the model names Forge but the `isForgePlugin` and `isForgeAvailable` shorthands a few callers keep.

## Discovery

`Session::initialiseExternalPlugins` runs from `Session`'s constructor, and again when an external device is asked for before it was found. For each external entry not yet found it takes the first of:

1. a VST3 in the engine's known-plugin list whose file still exists;
2. the development build, `<developmentArtefacts>/{Release,Debug}/VST3/<bundle>` under the repository root (`instruments/rhino-forge/build/RhinoForge_artefacts/.../Rhino Forge.vst3`, `effects/rhino-duck/build/RhinoDuck_artefacts/.../Rhino Duck.vst3`), located from the compile-time `RHINO_SOURCE_DIR`, so only a build made from this checkout finds it;
3. the bundle in the platform's default VST3 folders.

A description qualifies (`looksLike`) if its name contains the entry's `pluginName`, or its last word ("Forge", "Duck") with a manufacturer containing "Rhino" (both projects' `COMPANY_NAME`). The descriptions found are kept by catalog id in `Session::externalPlugins`, asked through `isExternalAvailable(id)` and `externalDescription(id)`. A loaded instance is recognised by its wrapped processor's name alone, since a processor reports no manufacturer: `externalDevice` in `SessionInternal.cpp` answers the first external entry, in catalog order, whose `pluginName` the name contains or whose last word it equals (an older Forge build called itself "Forge"). So one entry's `pluginName` must never be a substring of another's. That is how `deviceKind`, `deviceSlots`, `isInstrumentPlugin`, `carriedInstrument` and the rack's default knobs tell an external instrument from an external effect, and `isForgePlugin` is now `externalDevice(...)->id == "RhinoForge"`. The file chosen is written to `%APPDATA%\Rhino\rhino.log`.

## The catalog entry

`RhinoForge` is an instrument with `external = true` and no `typeName`, so `registerBuiltInTypes` skips it and no type-name lookup can match it. The rack's `+` menu greys an external device until `isExternalAvailable(id)`; a drop while it is missing retries discovery once (`addDevice`'s effect path and `addInstrumentDevice` both do) and then fails with "<bundle> was not found. Build or install the <name> VST3 first." Otherwise Forge follows [One instrument per track](one-instrument-per-track.md) like any instrument (`switchTrackInstrument` takes the entry's description and makes the plugin from it), and Duck is added where any audio effect is, before the channel strip or at the end of the main track's list, by `Session::addDevice`, which makes an external effect with `createNewPlugin(te::ExternalPlugin::xmlTypeName, description)` rather than from a type name. `addClipDevice` refuses an external effect ("goes on a track, not on a clip"), but no UI reaches it since commit `dd27e92`: Duck dropped on a clip goes on the clip's track ([Device chain order](device-chain-order.md#clip-effects)).

## What crosses the boundary

- Rhino names nine Forge parameters, by their names, in the catalog entry's `rackParameters` (in `SessionMappedParameters.cpp` until 2026-10-26): the rack shows Cutoff, Resonance, the three oscillator levels and Env 1's ADSR until something else is mapped onto it, and MAP in the face's name bar opens Forge's own window and puts every control touched there on the rack ([Mapped parameters](mapped-parameters.md), 2026-10-13; before it the generic face showed the first twelve parameters Tracktion listed). Duck's entry names nine the same way. The rack's Edit button opens the same window.
- Forge's host state is not versioned the way its presets are, so a `.rhinoedit` holding older Forge state is reconciled by `Processor::migrated` when it opens. That state includes any drawn wavetable or loaded spectral sample, so the project file carries them ([Forge presets and state](forge-presets-and-state.md)).
- Rhino's own automation lanes address a parameter by its place in the rack's list (`exposedParameterAt`), and that list holds Forge's own parameter ids, so Forge gaining or reordering parameters moves no lane. Renaming one of the nine default parameters loses it from a document that never mapped a knob, and changing a `paramID` string loses that knob from a document that did; `MappedParameters.inc` catches the first on any machine with a Forge build.
- Forge's knobs send gestures (`SliderAttachment`); its choosers, matrix depth and a few other controls write `setValueNotifyingHost` with none, which is why mapping takes a lone value change as a touch as well as a gesture.
- The [Computer MIDI keyboard](computer-keyboard.md) uses Forge's own key mapping and octave numbering, so a part lands in the same key in both.

## Pitfalls

- A running RhinoDAW with Forge loaded holds the `.vst3`, and Forge's build then fails to relink with LNK1104. Move the held binary aside and relink, or build the tests and the standalone and say the VST3 is stale ([LNK1104 means a running binary holds the file](locked-executable-lnk1104.md)). Doing only the second for a month left RhinoDAW hosting a VST3 dated 2026-09-24 until 2026-10-22.
- The suite covers an external plugin only where one can be found: `--self-test` skips external devices, and `native/src/tests/Pattern/scenarios/DeviceParameters.inc` runs its Forge block and its Duck block (added, listed as `RhinoDuck` of kind `AudioEffect`, wrapping a processor named "Rhino Duck", nine rack knobs with Mix first and Band solo last, two more on a MIDI lane) only when that plugin's discovery succeeded. Each block does require discovery when the development Release bundle exists, so a broken scan fails on a machine with the build and passes silently elsewhere. A green run on a machine without the builds proves nothing about hosting.
- A VST3 counts as shipped only after it has been loaded and automated in at least one external host.
- **Unloading Forge after its editor had painted froze RhinoDAW at shutdown**, leaving it running with no window, until commit `f2a972e` (2026-10-22). Forge's static Direct2D images were released at DLL detach under the loader lock, and NVIDIA's driver deadlocks there; they now live in JUCE's `ImageCache`, and closing RhinoDAW after Forge's window was open exited cleanly in 3 of 3 runs. The freeze on deleting Forge from the rack is believed, not proven, to be the same, and was not retested. The detach happens at the message thread's next `PeekMessage`, not at the host's `FreeLibrary` ([A JUCE plugin's DLL is freed at the host's next PeekMessage](plugin-dll-freed-at-next-peekmessage.md)), and pinning the module in Rhino would only move a freeze to exit ([A plugin's static Direct2D images deadlock NVIDIA's driver at unload](plugin-static-images-deadlock-on-unload.md)).

## Related

- [Forge is an independent VST3](forge-is-an-independent-vst3.md)
- [Rhino Forge](forge.md)
- [Device catalog](device-catalog.md)
- [Forge presets and state](forge-presets-and-state.md)
- [LNK1104 means a running binary holds the file](locked-executable-lnk1104.md)
- [Track automation](automation.md)
- [Mapped parameters, an external plugin's rack](mapped-parameters.md)
- [A plugin's static Direct2D images deadlock NVIDIA's driver at unload](plugin-static-images-deadlock-on-unload.md)
- [A JUCE plugin's DLL is freed at the host's next PeekMessage](plugin-dll-freed-at-next-peekmessage.md)
