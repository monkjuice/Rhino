---
title: Built-in devices
type: component
summary: Every device Rhino ships, what each one is and where its source lives, with Utility's details and the pitfalls the devices share.
tags: [rhino, devices, dsp]
sources: []
updated: 2026-10-28
---

# Built-in devices

Every device in the [Device catalog](device-catalog.md) on 2026-10-28. Rhino's own live under `native/src/devices/`, with their DSP under `native/src/core/dsp/`; three come from Tracktion as they are, and the Compressor is Tracktion's with a meter added.

| Device | Kind | Source | What it is |
| --- | --- | --- | --- |
| 4OSC | instrument | Tracktion `te::FourOscPlugin` | Subtractive synth; the rack shows six macros |
| Rhino FM | instrument | `instruments/RhinoFmDevice.*` | [Rhino FM](rhino-fm.md) |
| Rhino Forge | instrument | external VST3 | [Hosting Forge in Rhino](forge-hosting.md) |
| Drum Rack | instrument | `instruments/DrumRackDevice.*` | [Drum Rack](drum-rack.md) |
| Rhino EQ | audio FX | `audio/RhinoEqDevice.*` | [Rhino EQ](rhino-eq.md) |
| Compressor | audio FX | `audio/CompressorDevice.*` over Tracktion's `te::CompressorPlugin` | [Compressor](compressor.md): Tracktion's sound plus a gain reduction meter, since 2026-10-28; takes a sidechain from the rack's strip, which also switches on its `useSidechainTrigger` ([Sidechains](sidechains.md)) |
| Reverb, Delay | audio FX | Tracktion built-ins | |
| Rhino Duck | audio FX | external VST3, `effects/rhino-duck/` | [Rhino Duck](rhino-duck.md), the second external entry, since 2026-10-26 |
| Utility | audio FX | `audio/UtilityDevice.*` | Gain trim inside every track's chain |
| Amp | audio FX | `audio/AmpDevice.*` | [Amp](amp.md): Live's seven guitar amps on one valve circuit, at 4x |
| Saturator | audio FX | `audio/SaturatorDevice.*` | [Saturator](saturator.md): Live's eight curves, colour filters, at 4x |
| Redux | audio FX | `audio/ReduxDevice.*` | [Redux](redux.md): downsampler and bit reducer |
| Erosion | audio FX | `audio/ErosionDevice.*` | [Erosion](erosion.md): a delay swung at audio rate by a sine and noise |
| Rhino Space | audio FX | `audio/RhinoSpaceDevice.*` | Mix, size, smear, drive, width |
| Rhino Bloom | audio FX | `audio/RhinoBloomDevice.*` | Bloom, chorus, clouds, plate, colour |
| Rhino Tune | audio FX | `audio/AutoTuneDevice.*` | [Rhino Tune](rhino-tune.md) |
| Rhino Vocoder | audio FX | `audio/VocoderDevice.*` | [Rhino Vocoder](rhino-vocoder.md) |
| Tuner | audio FX | `audio/TunerDevice.*` | [Tuner](tuner.md): reads pitch, never changes the audio |
| Rhino Arp | MIDI FX | `midi/RhinoArpDevice.*` | [Rhino Arp](rhino-arp.md) |
| DJ Input, DJ Output | hidden | `infra/DjTrackPortDevices.*` | The DJ booth's two ends of every track, since 2026-10-12: [DJ decks play live through the track's chain](dj-decks-play-live-through-the-track.md) |
| Clip Envelope, Clip Pitch | hidden | `infra/ClipEnvelopeDevice.*`, `infra/ClipPitchDevice.*` | On an audio clip's own plugin list, only while it has an envelope for them, since `a1c9607`: [Clip envelopes](clip-envelopes.md) |

The two DJ ends are hand-written on `te::Plugin`, flagged `infrastructure` like Utility and never browsable; they pass the block through until a deck binds them, and `--device-test` holds both to the standard as pass-through effects. Like Utility, the DJ Input answers `getNumOutputChannelsGivenInputs` with its input count, and two only at a chain's head with none, so a mono chain stays mono; its first version answered at least two, the trap that once made a mono take play from the left speaker only, until the review of 2026-10-12 (`AudioChannels.inc`).

Rhino Wave, the first built-in synth (a morphing wavetable instrument), was removed on 2026-10-05. Rhino FM replaced it the same day, as a catalog entry with no special case in `Session`.

Utility, Rhino Space, Rhino FM, the Drum Rack and, since 2026-10-27, Amp, Saturator, Redux, Erosion and Tuner are written on the device SDK, the Compressor subclasses `te::CompressorPlugin`, and the rest are hand-written on `te::Plugin` ([The native device standard](native-device-standard.md)). Every device with a catalog factory is held to the same checks by `--device-test`, the five new ones with nothing pending, and since 2026-10-28 the Compressor too.

The five of 2026-10-27 are Live 12's devices of the same names, built from the Live 12 manual at the user's request. Their DSP is plain C++ in RhinoCore, and nothing in `Session` or the UI names any of them except the Tuner's hand-built face. Amp and Saturator oversample through the shared [Oversampler](oversampler.md) and report 37 samples of latency, Erosion reports 6 at 48 kHz, and Redux and Tuner report none. Adding them pushed track 0's chain in `--self-test` past Tracktion's default of sixteen plugins per track ([Tracktion refuses a track's seventeenth plugin without a word](tracktion-plugin-limit-per-track.md)).

## Utility

`rhino.utility.v1` has one parameter, `gainDb` (-60 to +6 dB), behind a 5 ms smoother. Every ordinary track gets one when it is made (a group bus does not), and the catalog flags it `infrastructure`, so the Device View hides it. Since commit `23de20c` it is not `browsable` either: a dropped Utility found the track's own, added nothing visible and appeared to do nothing. It is deliberately not the track fader; that is the engine's Volume & Pan ([Mixer](mixer.md)).

On the SDK it overrides `getBusses` with `singlePassThrough`, and the gain is smoothed as a gain rather than in decibels, so a block costs one conversion rather than one per sample. Declaring `singlePassThrough` was not enough: it also overrides `getNumOutputChannelsGivenInputs` to return what it was given. The engine's default answer is two, which widened a mono take to stereo with the second channel never written, so a one-input recording played from the left speaker only ([Recording and the count-in](recording.md)).

## Rhino Drums, replaced

Rhino Drums (`rhino.drums.v1`, `DrumDevice`) had one parameter, `kit`, choosing among five hard-coded kits (`DrumKit` in `DeviceCatalog.h`). The Drum Rack replaced it on 2026-10-06 under the same catalog id, `Drums` ([Drum Rack](drum-rack.md)). The five kits became `.rdk` files under `library/Drums/Kits/`. Three of its rules carried over:
- each sample is read whole, up to a 10 s ceiling that ends in a 5 ms fade, because reading one second cut the 1.5 s 808 kick off mid-decay (commit `492cc8b`);
- a file that is missing or will not decode is logged and its pad falls silent;
- a pad ignores a second strike within a millisecond, because the engine can deliver one written note twice when a clip starts just before a block boundary ([Reproducing a live timing bug offline](reproducing-live-timing-offline.md)).

## Pitfalls

From commit `492cc8b` (2026-10-05):

- **A block can be bigger than `initialise` was told.** Rhino Bloom and Rhino Space grew their dry buffers on the audio thread when that happened, and the Vocoder re-prepared, which also cleared every band mid-note. All three now work through such a block in pieces of the prepared size ([Real-time audio rules](real-time-audio-rules.md)).
- **Do not clamp a device's output.** The chain is floating point. Bloom and Space clamped their whole output, dry signal included, to ±1.0 and Drums to ±0.95, so a hot track was squared off before its fader could bring it down. Only the feedback-line limits inside Bloom and Space remain.
- **Report a tail.** Bloom and Space answer `getTailLength` with the longest echo's repeats until they are 60 dB down plus the reverb, capped at 30 s.
- **A subclass of a Tracktion built-in needs a type name of its own** (2026-10-28). The engine registers its built-ins first and silently refuses a second type under a taken name, so the Compressor is `rhino.compressor.v1` ([Tracktion refuses a second built-in type under a name it already has](tracktion-refuses-duplicate-built-in-type.md)).

The two pitfalls listed here on 2026-10-03 are gone: Utility is no longer browsable (`23de20c`), and Rhino Arp no longer builds a `te::MidiMessageArray` per call; it reuses two arrays reserved in `initialise()` ([Rhino Arp](rhino-arp.md)).

## Related

- [Device catalog](device-catalog.md)
- [Drum Rack](drum-rack.md)
- [Adding a device to Rhino](adding-a-device.md)
- [Device chain order](device-chain-order.md)
- [Content is files, never compiled in](content-is-files.md)
- [Real-time audio rules](real-time-audio-rules.md)
- [Amp](amp.md), [Saturator](saturator.md), [Redux](redux.md), [Erosion](erosion.md), [Tuner](tuner.md), [Compressor](compressor.md)
- [Tracktion refuses a track's seventeenth plugin without a word](tracktion-plugin-limit-per-track.md)
