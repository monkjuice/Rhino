---
title: Oversampler and BiquadFilter
type: component
summary: The drive devices' shared DSP in RhinoCore - a 2x or 4x linear-phase half-band FIR oversampler designed at prepare(), whose latency (31 or 37 samples) is padded to whole outer samples so a device's delayed dry signal mixes in phase, and a double-precision biquad with an exact inverse; written by hand because RhinoCore links juce_core only.
tags: [rhino, dsp, oversampling, devices]
sources: []
updated: 2026-10-27
---

# Oversampler and BiquadFilter

Added on 2026-10-27 with the drive devices (uncommitted at the time of writing). Both live in `native/src/core/dsp/` as plain C++. [Saturator](saturator.md) and [Amp](amp.md) run their nonlinear parts at 4x through `Oversampler`, and all four drive devices filter through `BiquadFilter`.

## Why hand-written

RhinoCore links `juce::juce_core` alone (`native/src/core/CMakeLists.txt`), so `juce_dsp`'s oversampler is out of reach ([Rhino's build targets](rhino-build-targets.md)). Being plain C++ also lets a test sweep it with sines.

## Oversampler

- **Design.** Each doubling is a linear-phase half-band FIR, made at `prepare()` by Kaiser-windowing a sinc for an 80 dB stopband, with the side taps rescaled so DC passes at exactly one. No coefficient table is copied in from anywhere.
- **First doubling.** 16 side-tap pairs, 63 taps. At 48 kHz it passes 20 kHz and stops from 28 kHz, where the image of 20 kHz lands.
- **Second doubling.** 6 pairs, 23 taps, because it only has to reject the band the first one already emptied.
- **Running it.** The polyphase loops skip the half-band's zero taps. The history rings are written twice, so no loop wraps.
- **Use.** `up(channel, input, count)` returns `count * factor()` inner samples to process in place, and `down(channel, output, count)` brings them back.

**The decimator takes the filter's value at the even input's moment.** That is where the upsampler put `x[m]`, at inner position 2m. With E the even inputs, O the odd ones and s_j the side taps:

```
y[m] = 0.5 O[m-K] + sum_j s_j (E[m-K+1+j] + E[m-K-j])
```

So the side taps meet only even inputs and the centre tap only odd ones. The even ring holds 2K samples and the odd ring K + 1.

The first version had those roles swapped. It took the value at the odd input's moment, so a round trip came out 30.5 outer samples rather than 31: an impulse peaked equally at 30 and 31, and a 1 kHz round trip was off by 0.03. The dry signal then no longer lined up with the wet. The oversampler's own test caught it. After the fix the round trip is symmetric about exactly 31 or 37 samples, and the 1 kHz error is 2e-6.

**Latency is whole outer samples.** A half-band stage of K pairs delays 2K - 1 inner samples. At 2x the round trip is 31 outer samples, already whole. At 4x the second stage's round trip comes out half an outer sample short, so `down` adds 2 samples of delay at 4x, giving 37 samples.

A device delays its dry signal by `latencySamples()` before Dry/Wet mixes it back, and the two meet exactly in phase. It also reports the same figure through the SDK's `latencySamples()`, so Saturator and Amp both report 37.
## BiquadFilter.h

- **`BiquadFilter`** runs one channel of `EqFilter.h`'s `Biquad` in transposed direct form II, in double precision. At 192 kHz a 20 Hz corner sits so close to the unit circle that float coefficients and state lose it.
- **`inverseOf(biquad)`** swaps numerator and denominator. That is the exact inverse of a minimum-phase filter, which the shelves and bells in `EqFilter.h` are. Saturator's colour filters use it to undo, after the curve, what they did before it.

## Tests

`checkDriveDevices` in `native/src/tests/DriveDevicesTest.cpp` checks three things:
- the round trip is a pure whole-sample delay, which is the check that caught the half-sample decimator;
- 18 kHz passes within 0.1 dB;
- images sit 75 dB down.

## Related

- [Saturator](saturator.md)
- [Amp](amp.md)
- [Redux](redux.md)
- [Erosion](erosion.md)
- [Rhino EQ](rhino-eq.md)
- [Real-time audio rules](real-time-audio-rules.md)
