---
title: Built-in devices
type: component
summary: Every device Rhino ships, what each one is and where its source lives, with Utility's details and the pitfalls the devices share.
tags: [rhino, devices, dsp]
sources: []
updated: 2026-10-06
---

# Built-in devices

Every device in the [Device catalog](device-catalog.md) on 2026-10-06. Rhino's own live under `native/src/devices/`; four come from Tracktion.

| Device | Kind | Source | What it is |
| --- | --- | --- | --- |
| 4OSC | instrument | Tracktion `te::FourOscPlugin` | Subtractive synth; the rack shows six macros |
| Rhino FM | instrument | `instruments/RhinoFmDevice.*` | [Rhino FM](rhino-fm.md) |
| Rhino Forge | instrument | external VST3 | [Hosting Forge in Rhino](forge-hosting.md) |
| Drum Rack | instrument | `instruments/DrumRackDevice.*` | [Drum Rack](drum-rack.md) |
| Rhino EQ | audio FX | `audio/RhinoEqDevice.*` | [Rhino EQ](rhino-eq.md) |
| Compressor, Reverb, Delay | audio FX | Tracktion built-ins | |
| Utility | audio FX | `audio/UtilityDevice.*` | Gain trim inside every track's chain |
| Rhino Space | audio FX | `audio/RhinoSpaceDevice.*` | Mix, size, smear, drive, width |
| Rhino Bloom | audio FX | `audio/RhinoBloomDevice.*` | Bloom, chorus, clouds, plate, colour |
| Rhino Tune | audio FX | `audio/AutoTuneDevice.*` | [Rhino Tune](rhino-tune.md) |
| Rhino Vocoder | audio FX | `audio/VocoderDevice.*` | [Rhino Vocoder and sidechains](rhino-vocoder.md) |
| Rhino Arp | MIDI FX | `midi/RhinoArpDevice.*` | [Rhino Arp](rhino-arp.md) |

Rhino Wave, the first built-in synth (a morphing wavetable instrument), was removed on 2026-10-05. Rhino FM replaced it the same day, as a catalog entry with no special case in `Session`.

Utility, Rhino Space, Rhino FM and the Drum Rack are written on the device SDK, and the rest are hand-written on `te::Plugin` ([The native device standard](native-device-standard.md)). Every device with a catalog factory is held to the same checks by `--device-test`.

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

The two pitfalls listed here on 2026-10-03 are gone: Utility is no longer browsable (`23de20c`), and Rhino Arp no longer builds a `te::MidiMessageArray` per call; it reuses two arrays reserved in `initialise()` ([Rhino Arp](rhino-arp.md)).

## Related

- [Device catalog](device-catalog.md)
- [Drum Rack](drum-rack.md)
- [Adding a device to Rhino](adding-a-device.md)
- [Device chain order](device-chain-order.md)
- [Content is files, never compiled in](content-is-files.md)
- [Real-time audio rules](real-time-audio-rules.md)
