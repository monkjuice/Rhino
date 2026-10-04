---
title: Real-time audio rules
type: convention
summary: The audio thread never allocates, locks, or touches files or UI; memory is sized at prepare and state crosses threads via atomics and queues.
tags: [both, real-time, audio-thread]
sources: []
updated: 2026-10-03
---

# Real-time audio rules

## The rule

An audio callback does no allocation, takes no lock, touches no file and does no UI work. That covers a Rhino device's `applyToBuffer` and Forge's `Core::renderSample`. It is a hard gate for every instrument and milestone. One known breach as of 2026-10-03: `RhinoArpDevice::applyToBuffer` reserves a fresh `te::MidiMessageArray` on every call ([Hazards found while seeding the wiki](known-hazards.md)).

## Why

Each block has a deadline (about 2.7 ms at 48 kHz and 128 samples) that an allocator, a lock or a disk seek can blow. A filter type, FX slot or oscillator mode can change while audio runs, so the audio thread must never be what finds memory for it.

## The patterns

**Size everything at prepare.** Forge's `Core::initialise` builds the shape tables and measures each noise source's level (once per sample rate, cached). It sizes every rack's delay lines for every type a slot might become, gives every voice a comb's delay line whether or not a comb is selected, and allocates the roughly 3.5 MB spectral bank whether or not any oscillator is spectral. Rhino's devices read their samples in `initialise()` and survive the files being absent ([Content is files, never compiled in](content-is-files.md)).

**Hand data across without locks.**
- One pointer load per block plus a parity counter. The audio thread bumps a `seq_cst` guard entering and leaving each block, which tells `WavetableStore` and `SampleStore` when a replaced object can be freed ([Hold ids, not pointers](ids-not-pointers.md)).
- Atomics. `UtilityDevice` reads its gain atomically into a preallocated smoother; Forge's `Processor` publishes meter readings into `std::atomic` arrays for the panel.
- A single-producer, single-consumer queue to a timer. MIDI learn pushes bound messages into `MidiControlQueue` (256 slots, no allocation), and the `Processor`'s own 60 Hz timer applies them, because writing a parameter takes locks ([Forge MIDI learn](forge-midi-learn.md)).
- A split at the thread boundary. Rhino EQ's `SpectrumTap` only copies samples into a ring; `SpectrumReader` transforms them on the panel's timer.

**Poll what the engine does not broadcast.** Tracktion starts and stops recordings and raises slot overrides on the audio thread without notifying anyone. `ControlWindow` in `Main.cpp` polls them on its one 30 Hz timer, which also applies track automation during playback (`Session::applyTrackAutomationAt`).

**Extra device callbacks are lazy and come off first.** The browser preview and the count-in are extra `AudioIODeviceCallback`s on the engine's device manager, built on first use and removed in `releaseAudioDevice` and `~Session` before the device or transport they read goes away.

**Smooth or crossfade; never step.** Continuous parameters are smoothed inside the DSP. Discrete switches crossfade: an EQ band turning on or changing type, a Forge noise source (6 ms), and a finished Forge voice, faded over 15 ms rather than cut ([Forge engine (Core)](forge-engine.md)).

## Related

- [Adding a device to Rhino](adding-a-device.md)
- [Rhino EQ](rhino-eq.md)
- [Recording and the count-in](recording.md)
