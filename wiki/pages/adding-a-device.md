---
title: Adding a device to Rhino
type: guide
summary: The three edits that add a device, the shape every device follows, and the enum-era code a new instrument still meets.
tags: [rhino, devices, cmake, testing]
sources: []
updated: 2026-10-03
---

# Adding a device to Rhino

Long form: the *Adding a device* section of `native/README.md`.

## The three edits

1. The source under `native/src/devices/instruments/`, `audio/` or `midi/`.
2. A line in `native/src/devices/CMakeLists.txt`.
3. An entry in `native/src/devices/DeviceCatalog.cpp`, plus a `createBuiltInType<>()` line in `registerBuiltInTypes`.

Then run `cmake -S native -B native/build`: the build never reconfigures itself ([A new source file needs an explicit CMake configure](cmake-does-not-reconfigure.md)). The browser, drop targets, rack menu and `Session::addDevice` read the [Device catalog](device-catalog.md), and `--self-test` checks the device has a browser row and can be added. No enum. If an effect needs `Session` or the UI edited, that is the bug.

## The shape

Start from `audio/UtilityDevice.*` (one parameter) or `audio/RhinoSpaceDevice.*` (six).

- Derive from `te::Plugin` with `inline static const char* xmlTypeName = "rhino.<name>.v1"`; documents store it, so never rename it. Override `getName`, `getPluginType`, `getVendor`, `getSelectableDescription` and `getBusses`. An instrument adds `isSynth`, `takesMidiInput` and `producesAudioWhenNoAudioInput`; a MIDI effect declares no buses (`midi/RhinoArpDevice.h`).
- Per parameter: a `juce::CachedValue` with `referTo`, `addParam`, `attachToCurrentValue`; `notifyListenersOfDeletion()` then `detachFromCurrentValue()` in the destructor; `te::copyPropertiesToCachedValues` and `updateFromAttachedValue()` in `restorePluginStateFromValueTree`. About eight mentions per parameter; the helper `HANDOVER.md` proposes does not exist yet (checked 2026-10-03).
- Choosers and switches are state properties, not parameters, as in Rhino Tune and Rhino EQ.
- `applyToBuffer` never allocates, locks or touches files; read content in `initialise()` and survive its absence ([Real-time audio rules](real-time-audio-rules.md)). Report latency through `getLatencySeconds` and restart playback when it changes; the graph reads it only when built. A sidechain is extra input channel names ([Rhino Vocoder and sidechains](rhino-vocoder.md)).

## DSP and its test

Put DSP worth testing in `native/src/core/` as plain C++, with the device a thin shell, as Rhino Tune, Rhino EQ and Rhino Vocoder do. Measure rendered output in a new `native/src/tests/*Test.cpp` called from `runSelfTest`, which needs its own line in `native/CMakeLists.txt` ([Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)).

## A face

Optional; without one the rack shows twelve generic knobs. A face is a new translation unit of `DeviceEditorPanel` (a line in `native/CMakeLists.txt`) and a `Face` value chosen by type in `setTarget`. Draw and hit-test rectangles, keep view state on the device, and reach it through `Session::devicePlugin` ([Device rack and device editors](device-rack.md)).

## Instruments still meet older code

Effects need nothing more; a new instrument also meets code written for the four that predate the catalog:

- `activeTrackInstrument` answers 4OSC for it, so a MIDI clip moved or pasted off its track puts a 4OSC on the destination ([One instrument per track](one-instrument-per-track.md)).
- `restoreProject` re-applies the edit's `rhinoPatternInstrument` to the first track and knows only `wave`, `forge` and `drums`; any other value means 4OSC. Once that property is set, a new instrument on the first track is swapped out when the project reopens, until `restoreProject` learns its `patternKey`.
- The paused session view's slot menu lists instruments by hand, and `native/src/DeviceMacros.cpp` curates rack knobs only for 4OSC and Rhino Wave.

## Related

- [Device catalog](device-catalog.md)
- [Built-in devices](built-in-devices.md)
- [Rhino's build targets](rhino-build-targets.md)
- [Device rack and device editors](device-rack.md)
- [Writing Rhino tests](writing-rhino-tests.md)
