---
title: The native device standard
type: decision
summary: Rhino's own devices are written on one SDK base and held to one conformance runner, and will save as .rnd device files with generated faces; VST3 stays the format for outside instruments.
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
- **A device file, `.rnd`.** It holds one device and its settings, as XML, so a variant needs no code: a patch, a kit, an arp feel. Not built yet.
- **Generated faces by default.** The rack builds a face from the declared controls. A device gets a hand-built face only when it has something true to draw from its DSP. Not built yet.
- **No compatibility for now.** Devices will change a lot in the short term, so device files, like `.rhinoedit` documents, are owed nothing ([No backward compatibility for .rhinoedit](no-rhinoedit-back-compat.md)). The files keep their version attributes, so the append-only rule can be switched on before a production release. Revisit it then.
- Choosers are automatable by default. A device file never embeds samples.

Rejected:

- A native-code plugin DLL with a Rhino ABI, which would be VST3 again without VST3's tooling.
- A package format, because a device's library folder already is one.

## State on 2026-10-05

The base, the factories and the runner exist. No shipping device is written on the base yet, and all eight devices with a factory pass the runner, so `pending` is empty. The plan, one step per commit:

1. Move Utility and Rhino Space onto the base.
2. Add the generated face.
3. Build a new synth on the base, with no `Session` special case.
4. Add `.rnd` files with factory presets.
5. Move the remaining devices.

## Consequences

- An engine parameter is reference counted and can outlive its device. A declared parameter therefore keeps its own copy of its declaration; a reference into the device read freed memory.
- `reset()` and `midiPanic()` may arrive from any thread, so the base only leaves a note that the audio thread acts on at its next block.
- A chooser or toggle is a `te::AutomatableParameter` subclass that reports itself discrete, with its labels. Code that knows only the engine type still sees it correctly.

## Related

- [Device catalog](device-catalog.md)
- [Adding a device to Rhino](adding-a-device.md)
- [Built-in devices](built-in-devices.md)
- [Writing Rhino tests](writing-rhino-tests.md)
- [Real-time audio rules](real-time-audio-rules.md)
- [Stored indices are append-only](append-only-stored-indices.md)
