---
title: The native device standard
type: decision
summary: Rhino's own devices are written on one SDK base, held to one conformance runner and given generated faces, with their presets as .rnd files; VST3 stays the format for outside instruments.
tags: [rhino, devices, sdk, testing]
sources: []
updated: 2026-10-05
---

# The native device standard

## Context

Rhino hosts two kinds of device. An external plugin is a VST3 found by scanning, as Forge is ([Forge is an independent VST3](forge-is-an-independent-vst3.md)). A native device is Rhino's own `te::Plugin`, made from its [Device catalog](device-catalog.md) entry. Until 2026-10-05 each native device hand-wrote the same Tracktion boilerplate, and the same mistakes recurred across them: MIDI applied at block start, the engine's all-notes-off ignored, buffers grown on the audio thread, output clamped, tails missing, and each control named in up to eight places. Rhino Wave, the first synth, had most of them and was removed rather than fixed ([Built-in devices](built-in-devices.md)).

## Decision

Agreed on 2026-10-05:

- **One base.** A native device derives from `NativeInstrument` or `NativeAudioEffect` (`native/src/devices/sdk/NativeDevice.h`), declares each control once as a `Param` member, and implements `prepare`, `clear` and `process`. The base owns the rest:
  - identity, from the catalog entry;
  - saving and restoring controls by id;
  - splitting a block at every MIDI event and at the prepared size;
  - all-notes-off and panic;
  - the non-finite guard;
  - latency and tail.
- **One factory per entry.** A catalog entry carries `.create = factory<T>()`, and that is the device's whole registration (commit `71842c2`).
- **One conformance runner.** `native_device_conformance` (`--device-test`, `native/src/tests/DeviceConformance.cpp`) renders every device that has a factory, offline. It checks:
  - identity and parameters;
  - a state round trip;
  - finite, bounded output at 25 random settings;
  - block-size invariance;
  - sample-accurate MIDI and all-notes-off, for an instrument;
  - the tail, for an effect, as the time to fall 60 dB.

  A device that cannot pass a check yet goes in the runner's `pending` table with the reason. A line that starts passing fails the run until it is deleted (commit `0806c97`).
- **Presets are `.rnd` files; devices are not files.** A device stays code, kept apart from the core. A `.rnd` is a preset of one device: XML holding the device's id and its settings, so a new patch, kit or arp feel needs no code. In the browser each device is a folder of its presets, such as Rhino Arp > `superCreativeArpeggiation.rnd`. Dragging the folder adds the device at its defaults, and dragging a preset adds it with that preset's settings. Not built yet. This was first written down as a "device file" and corrected the same day.
- **What decides between options:** top performance, testability and maintainability.
- **Generated faces by default.** The rack builds a face from the declared controls. A device gets a hand-built face only when it has something true to draw from its DSP. Not built yet.
- **No compatibility for now.** Devices will change a lot in the short term, so device files, like `.rhinoedit` documents, are owed nothing ([No backward compatibility for .rhinoedit](no-rhinoedit-back-compat.md)). The files keep their version attributes, so the append-only rule can be switched on before a production release. Revisit it then.
- Choosers are automatable by default. A device file never embeds samples.

Rejected:

- A native-code plugin DLL with a Rhino ABI, which would be VST3 again without VST3's tooling.
- A package format, because a device's library folder already is one.

## State on 2026-10-05

The base, the factories and the runner exist, and all eight devices with a factory pass the runner, so `pending` is empty.

Utility and Rhino Space are written on the base. Each device's output was dumped for one fixed input at its defaults and at three random settings, in normal and in oversized blocks, and compared byte for byte with the hand-written versions: all 16 renders were identical. Per 512-sample block, Utility costs about 1.45 µs against 1.53 µs before, guard and meter included, and Space about 55.8 µs against 57.2 µs.

Rhino FM, the new synth, is the first instrument on the base: a catalog entry and two files, with no line in `Session` or the UI. It passes every conformance check with nothing pending ([Rhino FM](rhino-fm.md)).

Still to do, one step per commit:

1. Add the generated face.
2. Add `.rnd` presets, with each device as a folder in the browser.
3. Move the remaining devices.

## Consequences

- An engine parameter is reference counted and can outlive its device. A declared parameter therefore keeps its own copy of its declaration; a reference into the device read freed memory.
- `reset()` and `midiPanic()` may arrive from any thread, so the base only leaves a note that the audio thread acts on at its next block.
- A chooser or toggle is a `te::AutomatableParameter` subclass that reports itself discrete, with its labels. Code that knows only the engine type still sees it correctly.

## Related

- [Device catalog](device-catalog.md)
- [Adding a device to Rhino](adding-a-device.md)
- [Built-in devices](built-in-devices.md)
- [Rhino FM](rhino-fm.md)
- [Writing Rhino tests](writing-rhino-tests.md)
- [Real-time audio rules](real-time-audio-rules.md)
- [Stored indices are append-only](append-only-stored-indices.md)
