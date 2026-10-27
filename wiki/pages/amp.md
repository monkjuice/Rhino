---
title: Amp
type: component
summary: Live 12's Amp on the device SDK - seven models voiced on one valve circuit (triode stages, Yeh and Smith's Bassman tone stack with each model's parts, Presence, a sagging push-pull power stage, a transformer band), all at 4x with 37 samples of latency, Mono or Dual, on a generated face with no display.
tags: [rhino, devices, dsp, distortion]
sources: []
updated: 2026-10-27
---

# Amp

Modelled on Live 12's Amp (manual p. 523). Added on 2026-10-27 (uncommitted at the time of writing).
- **Catalog:** id `Amp`, type `rhino.amp.v1`, category Distortion.
- **Device:** `native/src/devices/audio/AmpDevice.*` declares the controls on the SDK.
- **DSP:** `AmpEngine`, in `native/src/core/dsp/AmpEngine.*`.

It is not Softube's physical modelling. It is an honest approximation: each model is a voicing of one circuit, in the `voicings` table in `AmpEngine.cpp`.

## Controls

- **Amp Type**: Clean, Boost, Blues, Rock, Lead, Heavy or Bass. The list is stored by position, so it is append only.
- **Output**: Mono or Dual. Mono plays the average of both sides through one amp onto both. Dual gives each side an amp of its own. Both amps share one glide of the knob gains, so the sides never drift apart.
- **Gain, Volume and Dry/Wet**, then a Tone section of **Bass, Middle, Treble and Presence**. The amp knobs read 0 to 10, as Live's do.
- **Dry/Wet mixes by weights**, `dry * (1 - mix) + wet * mix`, as Saturator's and Redux's do. The interpolated form, `dry + (wet - dry) * mix`, is not exactly the wet signal at 100% in floating point. Mono's two sides carry the same wet signal over different dry ones, so with that form they differed in the last bit.

## The circuit

The signal runs through it in the order a valve amp is built:

1. **The input.** An input coupling capacitor, and a bright cap (a high shelf at 2.5 kHz) whose lift fades as Gain comes up.
2. **One to four triode stages**, depending on the model. Each stage is an asymmetric soft clipper that inverts. The clipper is a Padé approximant of tanh, with a slope of one through zero, so a quiet signal comes out undistorted. Each stage has a coupling high-pass and a Miller low-pass. After an odd number of stages the output is turned back the right way up, so Dry/Wet adds to the dry signal rather than cancelling it.
3. **The passive Treble/Middle/Bass stack.** This is Yeh and Smith's analog transfer function for the '59 Bassman tone stack (DAFx 2006): third order, bilinear-transformed, and using each model's own component values. Those are the values commonly cited for the amps the manual alludes to; the British amps' stack values are stand-ins. The Bass pot has a log taper, `exp((b - 1) * 3.4)`. The knobs interact, as they do in the real network.
4. **Make-up gain.** The stack's loss with all knobs centred is made up per model. It is worked out once, at `prepare()` (`stackMakeups`), not on the audio thread.
5. **Presence**, a high shelf at 3.5 kHz.
6. **The power stage**, a push-pull stage driven by Volume. Its supply sags under load, dropping in 8 ms and recovering over 150 ms. A per-model "hardness" blend is what makes the Bass model fuzz.
7. **The output transformer's band**: one high-pass and two low-passes, then +3 dB on every model, so a saturated power stage peaks near 0 dBFS.

Everything runs at 4x through the [oversampler](oversampler.md). The device therefore reports 37 samples of latency and delays its dry signal by the same amount. Coefficients are recomputed only when a setting changes.

## Voicing by measurement

The levels in `voicings` were set by measuring rendered audio, not by reasoning about the circuit ([Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)).

The power stage's drive is set by two numbers per model:
- `powerDb`, the drive with Volume at 5;
- `volumeSpanDb`, how far Volume's travel from 0 to 10 moves the drive, centred on 5.

The span is 36 dB for Clean, Boost, Rock and Lead. It is 48 dB for Blues, Heavy and Bass, the three the manual says break up with Volume.

Gain's ranges reach low enough that Gain 0 is near clean even on the high-gain models:

| Model | Gain range |
| --- | --- |
| Clean | -6 to +20 dB |
| Boost | 0 to +26 dB |
| Blues | 0 to +26 dB |
| Rock | -4 to +30 dB |
| Lead | -30 to +12 dB |
| Heavy | -16 to +24 dB |
| Bass | -6 to +22 dB |

Measured on 2026-10-27:
- **Defaults.** With a 110 Hz saw at 0.25, every model's defaults land within 1.2 dB of the input level.
- **Volume.** Volume 10 is about +14 to +18 dB RMS, with peaks of 1.3 to 2.2.
- **Flat out.** With a full-scale 55 Hz saw, peaks stay under 2.7.
- **Gain.** The THD of a 220 Hz sine rises 20 to 50 dB from Gain 0 to 10 on every model; Lead goes from -31 dB to -10 dB.

The first voicing used a low and a high drive per model. Measured, it peaked about +10 dBFS at Volume 10, the Heavy and Bass defaults sat 8 to 10 dB hot, and Lead was already saturated at Gain 0.

## No display

Amp does not override `describe`. The generated face draws traces centred on zero and scaled to the loudest, which suits a waveform but not a response in dB ([Displays draw from the DSP](displays-draw-from-the-dsp.md)). `AmpEngine::toneStackDb` computes the stack's response from the same coefficients the audio uses; for now only the tests call it.

## Tests

`native/src/tests/AmpTest.cpp` (`checkAmp`, called from `runSelfTest`) checks every model:
- Gain distorts.
- Treble, Bass and Middle, measured through the whole amp at a level that does not distort, agree with `toneStackDb` within 0.5 dB.
- Volume is louder turned up.
- The defaults keep the level within 3 dB of the input.
- Flat out, the output stays finite and bounded.

It also checks the amp as a whole:
- Mono puts one amp on both sides, and Dual's amps are the Mono amp.
- Dry/Wet at 0 is the input delayed by the latency, sample for sample.

All six CTest cases passed on the final tree, `--device-test` with nothing pending.

## Related

- [Built-in devices](built-in-devices.md)
- [Saturator](saturator.md)
- [Oversampler and BiquadFilter](oversampler.md)
- [The native device standard](native-device-standard.md)
