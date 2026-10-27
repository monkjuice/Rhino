---
title: Saturator
type: component
summary: Live 12's Saturator on the device SDK - Live's eight curves and Post Clip in a pure header that both the audio and the face's curve read, colour filters applied before the curve and exactly undone after it, the whole thing at 4x with 37 samples of latency, and the Waveshaper curve's six controls of Rhino's own design.
tags: [rhino, devices, dsp, distortion]
sources: []
updated: 2026-10-27
---

# Saturator

Modelled on Live 12's Saturator (manual pp. 631-634). Added on 2026-10-27 (uncommitted at the time of writing).
- **Catalog:** id `Saturator`, type `rhino.saturator.v1`, category Distortion.
- **DSP:** the curves are `native/src/core/dsp/SaturatorCurves.h`, a pure header in namespace `rhino::saturator`. The rest is `SaturatorEngine.*` beside it.
- **Device:** `native/src/devices/audio/SaturatorDevice.*`, a thin shell on the SDK.

## The curves

The curves are Live's eight: Analog Clip, Soft Sine, Bass Shaper, Medium Curve, Hard Curve, Sinoid Fold, Digital Clip and Waveshaper. The Curve and Post Clip lists are stored by position, so they are append only ([Stored indices are append-only](append-only-stored-indices.md)).

Every curve is odd, so none makes DC. Every curve also has a slope of one through zero, so a quiet signal passes unchanged whichever curve is chosen. The curves differ in how they reach their ceiling:
- Analog Clip stays straight to a knee at 0.6, then bends.
- Bass Shaper bends at a knee set by its threshold, -50 to 0 dB. The control's id is `bassThreshold`, but its name is "Threshold" so the caption fits the generated face.
- Digital Clip stops dead at the ceiling.
- Sinoid Fold folds back on itself.

The exception is the Waveshaper with Depth or Damp turned up. Its ripple steepens the slope at zero, and Damp flattens whatever is near zero, which acts as a gate.

Live does not publish the Waveshaper's formula. Its six controls (Drive, Curve, Depth, Linear, Damp, Period) are therefore Rhino's own design, given Live's names.

Post Clip (Off, Soft, Hard) runs before Output, so Hard keeps the output at or below the level Output sets.

## The engine

- **Oversampling.** Everything runs at 4x through the [oversampler](oversampler.md). The device reports 37 samples of latency and delays its dry signal by the same amount for Dry/Wet, which mixes by weights, `dry * (1 - mix) + wet * mix`, so 100% is exactly the wet signal ([Amp](amp.md#controls) has the reason).
- **Smoothing.** Drive and Output glide per inner sample and Dry/Wet per outer sample, so the result does not depend on block size.
- **Colour filters.** A low shelf at 250 Hz (Amt Lo) and a bell (Amt Hi, Frequency, and Width from Q 4 to Q 0.3), each ±18 dB. They run after Drive and before the curve, and `inverseOf` undoes them after it. They change *what* saturates without changing the tone of what does not: where the curve is straight, the two filters cancel exactly.

## Face and display

The face is generated:
- the Curve and Post Clip choosers stand together in the first column;
- the Colour and Waveshaper sections share one tab group, "Expanded", which is Live's expanded view.

`describe` draws `SaturatorEngine::transferCurve`: inputs from -1 to 1 through Drive, the curve, Post Clip and Output, computed by the same `SaturatorCurves.h` functions the audio runs. Two things are left out of the picture:
- **The colour filters**, which would make the curve depend on frequency.
- **Output's level.** The generated face scales every trace to the loudest, so turning Output changes nothing you can see.

The drawn curve is cached while the controls stand still ([Displays draw from the DSP](displays-draw-from-the-dsp.md)).

## Tests

`DriveDevicesTest.cpp` checks:
- a quiet signal comes through unchanged on every curve;
- the curves make odd harmonics only;
- a 7 kHz tone's fifth harmonic does not fold back to 13 kHz;
- the end of the drawn curve equals the audio's peak;
- the colour filters cancel where the curve is straight, and turning Amt Lo down saturates the lows less;
- Post Clip Hard holds the output under Output;
- Dry/Wet stays in step.

## Related

- [Built-in devices](built-in-devices.md)
- [Amp](amp.md)
- [Redux](redux.md)
- [Erosion](erosion.md)
- [Oversampler and BiquadFilter](oversampler.md)
- [Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)
- [Device rack and device editors](device-rack.md)
