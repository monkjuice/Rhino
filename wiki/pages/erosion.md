---
title: Erosion
type: component
summary: Live 12.4's Erosion (not Erosion Legacy) on the device SDK - a delay of a few samples swung at audio rate by a sine at Freq and by noise band-passed at Freq, which phase-modulates the input; at Amount 0 it is an exact integer delay, reported as latency (6 samples at 48 kHz).
tags: [rhino, devices, dsp, distortion]
sources: []
updated: 2026-10-27
---

# Erosion

Modelled on Live 12.4's new Erosion, not on Erosion Legacy (manual p. 580). Added on 2026-10-27 in commit `1bdd7dc`.
- **Catalog:** id `Erosion`, type `rhino.erosion.v1`, category Distortion.
- **DSP:** `native/src/core/dsp/ErosionEngine.*`.
- **Device:** `native/src/devices/audio/ErosionDevice.*`, on a generated face.
- **Controls:** Freq (30 Hz to 18 kHz), Amount, Noise Blend, Filter Width and Stereo Width.

## How it works

Erosion swings a short delay at audio rate. Moving a delay that fast phase-modulates whatever passes through it, so every component of the input grows sidebands.

- **The delay.** It is centred on `ceil(swing) + 2` samples. The swing is at most 4 samples at 48 kHz and scales with the rate, so the sound does not change with it.
- **The modulator.** Each sample, it blends a sine at Freq with white noise through a band-pass centred on Freq. Noise Blend sets the mix. The band-pass has a peak gain of one, and Filter Width runs its Q from 8 down to 0.4.
- **The noise level.** The band's output is scaled by its noise bandwidth (π/2 · f/Q, against the band up to Nyquist), so it swings the delay as far as the sine does at any width. That gain is capped at 60. The result is then tanh-limited to ±1, so however the noise peaks, the read never leaves the line.
- **Stereo Width.** It moves the right side's sine up to a quarter turn ahead of the left's. It also mixes an independent noise into the right side at constant power.
- **Smoothing.** Amount, Noise Blend and Stereo Width each glide over 20 ms.

## Latency

The read is a 4-point Hermite interpolation, which is exact at whole samples. At Amount 0 the device is therefore an exact integer delay, and it reports that delay through `latencySamples()` so delay compensation shifts the track back into time. The figure is 6 samples at 48 kHz and grows with the rate.

## Display

`ErosionEngine::modulationPicture` runs an engine of its own for 10 ms to let the band ring up, then hands the face 192 points of the left modulator. The picture is cached while the controls stand still.

## Tests

`DriveDevicesTest.cpp` checks:
- at Amount 0 the output is an exact delay;
- the phase-modulation sidebands appear at 1 kHz ± 3 kHz;
- the noise leaves a residue;
- Stereo Width makes the two sides differ.

## Related

- [Built-in devices](built-in-devices.md)
- [Redux](redux.md)
- [Saturator](saturator.md)
- [Displays draw from the DSP](displays-draw-from-the-dsp.md)
