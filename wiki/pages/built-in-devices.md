---
title: Built-in devices
type: component
summary: Every device Rhino ships, what each one is and where its source lives, with the details of Utility and the drum rack.
tags: [rhino, devices, dsp]
sources: []
updated: 2026-10-05
---

# Built-in devices

Every device in the [Device catalog](device-catalog.md) on 2026-10-05. Rhino's own live under `native/src/devices/`; four come from Tracktion.

| Device | Kind | Source | What it is |
| --- | --- | --- | --- |
| 4OSC | instrument | Tracktion `te::FourOscPlugin` | Subtractive synth; the rack shows six macros |
| Rhino Forge | instrument | external VST3 | [Hosting Forge in Rhino](forge-hosting.md) |
| Rhino Drums | instrument | `instruments/DrumDevice.*` | Sample drum rack, offered as five kits |
| Rhino EQ | audio FX | `audio/RhinoEqDevice.*` | [Rhino EQ](rhino-eq.md) |
| Compressor, Reverb, Delay | audio FX | Tracktion built-ins | |
| Utility | audio FX | `audio/UtilityDevice.*` | Gain trim inside every track's chain |
| Rhino Space | audio FX | `audio/RhinoSpaceDevice.*` | Mix, size, smear, drive, width |
| Rhino Bloom | audio FX | `audio/RhinoBloomDevice.*` | Bloom, chorus, clouds, plate, colour |
| Rhino Tune | audio FX | `audio/AutoTuneDevice.*` | [Rhino Tune](rhino-tune.md) |
| Rhino Vocoder | audio FX | `audio/VocoderDevice.*` | [Rhino Vocoder and sidechains](rhino-vocoder.md) |
| Rhino Arp | MIDI FX | `midi/RhinoArpDevice.*` | [Rhino Arp](rhino-arp.md) |

Rhino Wave, the first built-in synth (a morphing wavetable instrument), was removed on 2026-10-05. Its replacement is to be a new catalog-only synth that needs no special case in `Session`.

## Utility

`rhino.utility.v1` has one parameter, `gainDb` (-60 to +6 dB), behind a 5 ms smoother. Every ordinary track gets one when it is made (a group bus does not), and the catalog flags it `infrastructure`, so the Device View hides it. It is deliberately not the track fader; that is the engine's Volume & Pan ([Mixer](mixer.md)).

Declaring `singlePassThrough` was not enough: it also overrides `getNumOutputChannelsGivenInputs` to return what it was given. The engine's default answer is two, which widened a mono take to stereo with the second channel never written, so a one-input recording played from the left speaker only ([Recording and the count-in](recording.md)).

## Rhino Drums

`rhino.drums.v1` has one parameter, `kit`. The five kits (`DrumKit` in `DeviceCatalog.h`) are one sample set reshaped per voice by rate, decay and level; the Clap kit also moves the snare note onto the clap pad. Pads sit on notes 48-59 (kick 48, snare 53, clap 56, hats 58 and 59), and a closed hat chokes an open one.

Samples come from the [Content library](content-library.md) in `initialise()`, never the audio callback. A missing file is logged and its pad falls silent; at most one second of each sample is loaded. A pad ignores a second strike within a millisecond, because the engine can deliver one written note twice when a clip starts just before a block boundary, and the two summed into an accent ([Reproducing a live timing bug offline](reproducing-live-timing-offline.md)).

## Pitfalls

Both read from the code on 2026-10-03:

- Utility is also browsable (the "Utility gain" row), but the rack hides infrastructure by type, so a Utility dropped from the browser joins the chain and never appears in the Device View.
- Rhino Arp builds a fresh `te::MidiMessageArray` and reserves room in it on every `applyToBuffer` call, a heap allocation on the audio thread ([Real-time audio rules](real-time-audio-rules.md)).

## Related

- [Device catalog](device-catalog.md)
- [Adding a device to Rhino](adding-a-device.md)
- [Device chain order](device-chain-order.md)
- [Content is files, never compiled in](content-is-files.md)
- [Real-time audio rules](real-time-audio-rules.md)
