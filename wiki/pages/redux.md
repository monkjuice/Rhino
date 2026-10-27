---
title: Redux
type: component
summary: Live 12's Redux on the device SDK - a sample-and-hold downsampler whose aliasing is the sound, with Jitter, Pre and Post filters, and a bit reducer with mu-law Shape and a DC Shift that leaves no DC behind; no oversampling and no latency.
tags: [rhino, devices, dsp, distortion]
sources: []
updated: 2026-10-27
---

# Redux

Modelled on Live 12's Redux (manual pp. 615-616). Added on 2026-10-27 (uncommitted at the time of writing).
- **Catalog:** id `Redux`, type `rhino.redux.v1`, category Distortion.
- **DSP:** `native/src/core/dsp/ReduxEngine.*`.
- **Device:** `native/src/devices/audio/ReduxDevice.*`, on a generated face with three sections: Downsample, Bit Reduction and Mix.

Redux neither oversamples nor reports latency, because the aliasing is wanted. Its Dry/Wet mixes by weights, `dry * (1 - mix) + wet * mix`, so 100% is exactly the wet signal ([Amp](amp.md#controls) has the reason).

## Downsample

- **Rate** (50 Hz to 40 kHz) holds each sample for one period of the chosen rate. Nothing stops what that folds down.
- **Jitter** shakes the length of each hold period by up to ±90%. Each channel has its own xorshift seed, so the two sides are sampled at different moments, which widens the sound.
- **Pre Filter** is a fourth-order Butterworth low-pass at 0.45 x Rate. It is on by default.
- **Post Filter** smooths the steps with a fourth-order Butterworth at half the rate x 2^octave. The octave is set from -3 to +3 by the control named "Post Octave", whose id is `postOctave`; the name is kept short so its caption fits the generated face.

Both filters run every sample even while switched off, so switching one in starts from the signal rather than from a step. Their cutoffs are held below 0.45 of the host rate.

## Bit reduction

`ReduxEngine::quantise` is a pure static function:
- **Bits** (1 to 16, continuous) rounds to 2^(bits-1) steps either side of zero.
- **Shape** compands first, as mu-law does. Its mu runs from 0 to 255, so quiet detail keeps finer steps than loud.
- **DC Shift** moves the steps by half a step, so zero falls between two of them. A quiet signal then crunches between the steps instead of vanishing into a dead band. The offset is taken off again after quantising, so the device leaves no DC to push the next one off centre.

## Display

`ReduxEngine::picture` plays two cycles of a 375 Hz sine at 0.9 through an engine of its own. That engine runs as long again first to settle, so the filters' start-up is not in the picture. The face draws the result as two traces, "Redux" over "Input", at the rate the device was prepared at, and keeps them while the controls stand still ([Displays draw from the DSP](displays-draw-from-the-dsp.md)).

## Tests

`DriveDevicesTest.cpp` covers:
- the quantiser's steps, DC Shift and Shape;
- folding;
- the Pre and Post filters;
- Jitter.

## Related

- [Built-in devices](built-in-devices.md)
- [Saturator](saturator.md)
- [Erosion](erosion.md)
- [Oversampler and BiquadFilter](oversampler.md)
