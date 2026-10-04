---
title: Rhino Vocoder and sidechains
type: component
summary: A channel vocoder whose carrier arrives as a sidechain from another track, and how Rhino routes a sidechain at all.
tags: [rhino, devices, dsp, sidechain]
sources: []
updated: 2026-10-03
---

# Rhino Vocoder and sidechains

A channel vocoder in the one mode Live calls External: the voice is the track's own audio, the carrier another track's output. `native/src/core/VocoderEngine.*` is the bank and followers, `native/src/devices/audio/VocoderDevice.*` (`rhino.vocoder.v1`) hands it parameters, and the face is `native/src/DeviceEditorPanelVocoder.cpp`.

## The sidechain

**Declaring four input channels is the whole mechanism.** `VocoderDevice::getChannelNames` reports Left, Right, Carrier L and Carrier R in and two channels out, and `canSidechain` answers true. When the plugin names a source track, Tracktion's graph builder puts a send on that track and a return in front of the plugin: channels 0-1 are the voice, 2-3 the carrier.

`native/src/SessionSidechain.cpp` is Rhino's whole part. It writes the source track's id onto the plugin, re-guesses the wires and restarts playback at once: the id alone would take effect only at the next transport start, and the point is to sing into an armed track with the transport stopped. A track cannot be its own carrier, and removing a track clears its id from every plugin naming it. Nothing here is vocoder-specific, so a compressor ducking to a kick would need no new model code.

**The send sits before the source track's mute.** That is the engine's doing, and it is why muting the synth, so you do not hear it twice, leaves the carrier running.

## DSP choices

- The carrier is normalised to a fixed RMS, so the output follows the voice rather than the synth's level; **Enhance** also divides each band by its own energy, so a synth with a strong fundamental still speaks.
- The modulator is analysed in mono; the carrier is filtered per channel, keeping a stereo synth's image.
- The bank (4-40 bands, default 20) is walked band-outer, sample-inner.
- **Depth** compares each band with the bank average from the previous block: a block of lag on the reference, never on the modulation.
- **Unvoiced** adds noise to the carrier while the voice is sibilant (energy above 3.5 kHz), since an `s` has no pitch to play.

**Audio From** is the only control on any face that edits routing rather than a parameter, so it is drawn rather than a knob ([Device rack and device editors](device-rack.md)). Set to None, the voice passes through dry and the face says what to do.

## Tests

`native/src/tests/VocoderTest.cpp` builds a carrier of four tones (250 Hz, 1, 2 and 4 kHz), feeds one as the modulator and measures which comes out; the formant check repeats it with the bank an octave up. They span four octaves. `native/src/tests/Arrangement/scenarios/VocoderCarrier.inc` checks the routing: four wires, saved with the project, cleared with the carrier track.

## Related

- [Device chain order](device-chain-order.md)
- [Rhino Tune](rhino-tune.md)
- [Track inputs and monitoring](inputs-and-monitoring.md)
- [Recording and the count-in](recording.md)
- [Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)
