---
title: Adding a device to Rhino
type: guide
summary: The three edits that add a device, the SDK base a new one is written on, the shape the older devices follow, and the enum-era code a new instrument still meets.
tags: [rhino, devices, cmake, testing]
sources: []
updated: 2026-10-05
---

# Adding a device to Rhino

## The three edits

1. The source under `native/src/devices/instruments/`, `audio/` or `midi/`.
2. A line in `native/src/devices/CMakeLists.txt`.
3. An entry in `native/src/devices/DeviceCatalog.cpp` with `.create = factory<YourDevice>()`, which is the device's whole registration.

Then run `cmake -S native -B native/build`: the build never reconfigures itself ([A new source file needs an explicit CMake configure](cmake-does-not-reconfigure.md)). The browser, drop targets, rack menu and `Session::addDevice` read the [Device catalog](device-catalog.md). `--self-test` checks the device has a browser row and can be added, and `--device-test` renders it against [the native device standard](native-device-standard.md). No enum. If an effect needs `Session` or the UI edited, that is the bug.

## On the SDK

Write a new device on the base in `native/src/devices/sdk/`: derive from `NativeInstrument` or `NativeAudioEffect`, declare each control once as a `Param` member (`Param mix = param("mix", "Mix").range(0.0f, 1.0f).unit(ParamUnit::percent);`), and implement `prepare`, `clear` and `process`, plus the note calls for an instrument and `tailSeconds` for an effect that rings on. The base handles saving, MIDI timing, oversized blocks, all-notes-off, the non-finite guard, latency and tail reporting, which the list below has to do by hand. Start from `audio/UtilityDevice.*` (one control) or `audio/RhinoSpaceDevice.*` (six, with a tail); the probe devices in `native/src/tests/DeviceConformance.cpp` show an instrument's note calls.

## The older shape

The other devices are still hand-written on `te::Plugin`, and this is what changing one involves. Rhino Bloom (`audio/RhinoBloomDevice.*`) is the closest to Space.

- Derive from `te::Plugin` with `inline static const char* xmlTypeName = "rhino.<name>.v1"`; documents store it, so never rename it. Override `getName`, `getPluginType`, `getVendor`, `getSelectableDescription` and `getBusses`. An instrument adds `isSynth`, `takesMidiInput` and `producesAudioWhenNoAudioInput`; a MIDI effect declares no buses (`midi/RhinoArpDevice.h`).
- Per parameter: a `juce::CachedValue` with `referTo`, `addParam`, `attachToCurrentValue`; `notifyListenersOfDeletion()` then `detachFromCurrentValue()` in the destructor; `te::copyPropertiesToCachedValues` and `updateFromAttachedValue()` in `restorePluginStateFromValueTree`. This is about eight mentions per parameter; no helper abstracts it (checked 2026-10-03).
- Choosers and switches are state properties, not parameters, as in Rhino Tune and Rhino EQ.
- `applyToBuffer` never allocates, locks or touches files; read content in `initialise()` and survive its absence ([Real-time audio rules](real-time-audio-rules.md)). The engine may hand it a block bigger than `initialise` was told: work through it in pieces of the prepared size, as `audio/RhinoSpaceDevice.cpp` does.
- Leave the output unclamped, since the chain is floating point; limit only inside feedback lines. A device that rings on after its input stops reports `getTailLength`, as Rhino Bloom and Rhino Space do ([Built-in devices](built-in-devices.md)).
- Report latency through `getLatencySeconds`, computed from the device's current settings rather than read back from what the audio thread last applied, and restart playback when it changes; the graph reads it only when built ([Rhino Tune](rhino-tune.md)). A sidechain is extra input channel names ([Rhino Vocoder and sidechains](rhino-vocoder.md)).

## DSP and its test

Put DSP worth testing in `native/src/core/` as plain C++, with the device a thin shell, as Rhino Tune, Rhino EQ and Rhino Vocoder do. Measure rendered output in a new `native/src/tests/*Test.cpp` called from `runSelfTest`, which needs its own line in `native/CMakeLists.txt` ([Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)).

## A face

Optional. A device on the SDK without one gets a face generated from its declarations (`DeviceEditorPanelGenerated.cpp`): controls grouped under the sections they declare, a knob for a continuous control that steps, skews and double-click-resets as declared, a chooser for a choice and a switch for a toggle. A section-named caption drops the section's name ("Op 1 Ratio" reads "Ratio" under OP 1), so name a control in full for its automation lane. Rhino FM and Rhino Space use it. Anything not on the SDK gets twelve generic knobs. A hand-built face is a new translation unit of `DeviceEditorPanel` (a line in `native/CMakeLists.txt`) and a `Face` value chosen by type in `setTarget`. Draw and hit-test rectangles, keep view state on the device, and reach it through `Session::devicePlugin`. Write the device's own settings through `Session::editDeviceSettings`, so each is an undo step of its own and the session hears of it ([Device rack and device editors](device-rack.md)).

## Instruments still meet older code

Effects need nothing more; a new instrument also meets code written for the instruments that predate the catalog:

- The paused session view's slot menu lists instruments by hand.
- `native/src/DeviceMacros.cpp` curates rack knobs only for 4OSC; any other device shows its active parameters in order.

Two older traps are gone as of 2026-10-05: a moved or pasted MIDI clip carries its instrument by catalog entry ([One instrument per track](one-instrument-per-track.md)), and reopening a project no longer re-applies an instrument to the first track (commit `a04b407`).

## Related

- [Device catalog](device-catalog.md)
- [Built-in devices](built-in-devices.md)
- [Rhino's build targets](rhino-build-targets.md)
- [Device rack and device editors](device-rack.md)
- [Writing Rhino tests](writing-rhino-tests.md)
- [The native device standard](native-device-standard.md)
