---
title: Real-time audio rules
type: convention
summary: The audio thread never allocates, locks, or touches files or UI; memory is sized at prepare and state crosses threads via atomics and queues.
tags: [both, real-time, audio-thread]
sources: []
updated: 2026-10-10
---

# Real-time audio rules

## The rule

An audio callback does no allocation, takes no lock, touches no file and does no UI work. That covers a Rhino device's `applyToBuffer` and Forge's `Processor::processBlock` and `Core::renderSample`. It is a hard gate for every instrument and milestone. The breach recorded here on 2026-10-03, Rhino Arp building a `te::MidiMessageArray` on every call, is gone: the redesigned arp reuses arrays reserved in `initialise()` ([Hazards found while seeding the wiki](known-hazards.md)).

## Why

Each block has a deadline (about 2.7 ms at 48 kHz and 128 samples) that an allocator, a lock or a disk seek can blow. A filter type, FX slot or oscillator mode can change while audio runs, so the audio thread must never be what finds memory for it.

## The patterns

**Size everything at prepare.** Forge's `Core::initialise` builds the shape tables and measures each noise source's level (once per sample rate, cached). It sizes every rack's delay lines for every type a slot might become, gives every voice a comb's delay line whether or not a comb is selected, and allocates the roughly 3.5 MB spectral bank whether or not any oscillator is spectral. The Drum Rack reads a pad's sample on the message thread whenever the pad changes, never on the audio thread, and survives the file being absent ([Content is files, never compiled in](content-is-files.md)).

- **A block can be bigger than `initialise` was told.** The engine allows it. Work through such a block in pieces of the prepared size, as Rhino Bloom, Rhino Space and the Vocoder do (commit `492cc8b`); growing a buffer there allocated on the audio thread, and the Vocoder's re-prepare also cleared every band mid-note.
- **Resolve names at construction, not per block.** A `juce::String` id is a heap allocation plus a lookup. Forge's processor spelled about 430 of them a block to build its `Patch` and `Modulation`, some 2,100 allocations, until the constructor paired each parameter with the field it fills (commit `c9a37a6`).
- **Refill the host's buffers; never swap your own in.** Forge's MIDI-learn filter swapped a fresh `MidiBuffer` in, which freed the host's storage on the audio thread. It now gathers survivors into `midiKept`, sized in `prepareToPlay`, and refills the host's buffer only when something was taken out.

**Check it, do not read it.** What allocates is usually a few calls down. Forge's `realtimeSuite` (area `engine`) counts `operator new` across 64 blocks with notes and a bound knob moving, and `--profile-audio` prints allocations per block. The counter cannot see `juce::HeapBlock`, which calls `malloc` directly, so a growing `MidiBuffer` or `AudioBuffer` needs its own check: the suite also requires the host's MIDI storage pointer to survive the filter.

**Hand data across without locks.**
- One pointer load per block plus a parity counter. The audio thread bumps a `seq_cst` guard entering and leaving each block, which tells `WavetableStore` and `SampleStore` when a replaced object can be freed ([Hold ids, not pointers](ids-not-pointers.md)).
- Counted stretches plus a reader count, when a reader holds on across blocks. A Drum Rack voice reads its sample for as long as it rings, long after its pad has taken another, so a per-block guard is not enough ([Drum Rack](drum-rack.md)).
  - `DrumRackEngine` publishes each pad's sample as an atomic pointer, stored before the pad's source, so a strike never finds a sample pad with no sample.
  - Each stretch of audio-thread work that can pick a sample up (`noteOn`, `render`) counts itself in and out on two atomics, `stretchesBegun` and `stretchesEnded`.
  - Each voice counts itself onto its sample (`DrumSample::playing`).
  - A replaced sample is retired with the number of stretches begun at that moment. `collect()`, on the message thread, frees it once that many stretches have ended and no voice plays it.
  - Every pad edit and the face's 24 Hz timer collect, and `prepare()`, when nothing renders, frees everything. `checkSampleHandOff` in `DrumRackTest.cpp` covers it.
- Counted blocks for a whole-track swap. `DjEngine` hands a deck its material by pointer and retires the old one with the number of blocks begun; `collect()`, on the message thread, frees it once as many have ended, which is enough because a deck reads nothing across blocks. Its commands cross in a single-producer queue of 256 applied at the start of a block, and every knob is an atomic read once a block and smoothed ([DJ view and the booth](dj-view.md)).
- **One judged exception: the DJ preview's notes** (2026-10-10). `Session::DjBooth` is the engine's `DjLiveSink`, and `DjEngine::sequence` calls its `noteOn`/`noteOff` from the booth's audio callback. They write into the `keyboardState` of the "All MIDI Ins" and "Computer Keyboard" input devices (`SessionDj.cpp`; the devices are atomics, `previewInputs`, set when a preview begins), the entry the typing keyboard takes from the message thread ([Computer MIDI keyboard](computer-keyboard.md)). That takes `juce::MidiKeyboardState`'s own CriticalSection, briefly, and queues into Tracktion's MIDI input, which is built to be fed from a driver thread, which the booth's callback is to it. Accepted as a risk judged small so that a held knob is heard at once, not as a proven lock-free path; the sink's contract in `DjEngine.h` is to take no lock it could wait on and to allocate nothing, and any other `DjLiveSink` must keep to it ([DJ view and the booth](dj-view.md)).
- Field by field, where a torn read is harmless. A Drum Rack pad's playback is a dozen atomics written one at a time, so a strike landing mid-write plays a mix of old and new for one note, which is accepted ([Drum Rack sample editor](drum-rack-sample-editor.md)). Strikes from the face cross as a 128-bit mask of atomics, and a solo count spares each voice a scan of all 128 pads.
- Atomics. `UtilityDevice` reads its gain atomically into a preallocated smoother; Forge's `Processor` publishes meter readings into `std::atomic` arrays for the panel.
- A single-producer, single-consumer queue to a timer. MIDI learn pushes bound messages into `MidiControlQueue` (256 slots, no allocation), and the `Processor`'s own 60 Hz timer applies them, because writing a parameter takes locks ([Forge MIDI learn](forge-midi-learn.md)).
- A split at the thread boundary. Rhino EQ's `SpectrumTap` only copies samples into a ring; `SpectrumReader` transforms them on the panel's timer.

**Poll what the engine does not broadcast.** Tracktion starts and stops recordings and raises slot overrides on the audio thread without notifying anyone. `ControlWindow` in `Main.cpp` polls them on its one 30 Hz timer, with `Session::djPoll` for the DJ booth's finished loads and stale bounces, which a worker and a quiet period decide. Track automation is no longer applied from there: the engine plays it from parameter curves, and the rack polls the knobs it moves ([Track automation](automation.md)).

**Extra device callbacks are lazy and come off first.** The browser preview and the count-in are extra `AudioIODeviceCallback`s on the engine's device manager, built on first use and removed in `releaseAudioDevice` and `~Session` before the device or transport they read goes away.

**Smooth or crossfade; never step.** Continuous parameters are smoothed inside the DSP. Discrete switches crossfade: an EQ band turning on or changing type, a Forge noise source (6 ms), and a finished Forge voice, faded over 15 ms rather than cut ([Forge engine (Core)](forge-engine.md)). Smoothing state lives across blocks: the Vocoder gate's 3 ms edge restarted every block until `492cc8b`, so an edge near a block boundary stepped at the next one. A delay's time is a parameter too: the DJ send unit (`DjSendFx`, `core/DjMixer.cpp`) glides its read offset a thousandth of the way each sample, so a turn of the time knob bends the repeats as tape would instead of clicking, and clears its lines on a type change rather than replaying another effect's tail; a test of it waits for the glide ([Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)).

**Flush what decays toward zero.** An envelope multiplied down every sample reaches the denormal range long before a drum ends, and x86 is far slower on denormals. So `fall()` in `core/DrumSynth.cpp` zeroes an envelope below 1e-9, and a Drum Rack sample voice's low-pass zeroes its state below 1e-20. In the strike costs `--self-test` logs, in ns a sample, the Kick went from 127 to 32, the Tom from 92 to 21 and the Rim from 97 to 33 (2026-10-06). `juce::ScopedNoDenormals` in a device's render call (the Drum Rack's and Rhino FM's `process`, Rhino EQ's `applyToBuffer`) covers the audio thread only. Pictures and previews render through `renderStrike` on the message thread, where only the explicit flushes help.

## Related

- [Adding a device to Rhino](adding-a-device.md)
- [Rhino EQ](rhino-eq.md)
- [Drum Rack](drum-rack.md)
- [Recording and the count-in](recording.md)
