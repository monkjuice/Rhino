---
title: Displays draw from the DSP
type: convention
summary: A curve on screen is computed by the same functions the audio runs, never from a second set of formulas, and tests hold the two together.
tags: [both, ui, dsp, testing]
sources: []
updated: 2026-10-03
---

# Displays draw from the DSP

## The rule

A display is drawn from the arithmetic the engine actually runs, never from a picture of the effect in general. A response drawn from separate formulas is only a drawing of what someone believed the filter does. Drawn from the coefficients the samples go through, it can only be wrong by arithmetic. A merely plausible display is worse than none, "because it is believed" (`tests/ForgeTestsDisplays.cpp` in Forge). So when a display disagrees with what is heard, the display is not the thing that is wrong.

## Where it is applied

- **[Rhino EQ](rhino-eq.md).** `core/EqFilter.h` is a pure header that the curve and the audio share. The panel draws `EqEngine::responseDbAt` from the scaled bands (`EqEngine::scaledBand`); drawn from the stored bands, display and audio would part once Scale leaves 100%.
- **[Forge filter](forge-filter.md).** `core/ForgeFilter.h` holds `filterSample`, the audio, and `filterMagnitude`, the picture, written from the same coefficients a few lines apart.
- **Forge FX slots** (`ui/ForgeFxDisplay.h`). A distortion's curve is `fxShape` per pixel, an equaliser's response is the magnitude of the very biquads `setBand` builds, and a delay's repeats are placed by `fxDelaySeconds`.
- **Forge LFO graph.** It draws `lfoShapeTables` or the custom `LfoTable`, the tables the voice reads through `lfoValue`.
- **Forge oscillator display.** It draws the frames in `WavetableStore` as authored, warped as the voice warps them.
- **Forge modulation rings.** `modulationOffset` is the reading the loudest voice renders with, the same voice ENV 1's display follows.

## Honest exceptions

Where no true picture exists, draw the truest thing that can be said instead of inventing a shape. Forge's ring modulator and diffusor have no transfer function, so they draw unity labelled `PHASE ONLY`; the sample and hold draws a hold's own sinc. Warp modes that are no picture of a table (the filter, FM, PD, AM and RM families) leave the oscillator display, which draws only sync, alt and distortion modes (`warpShapesWaveform`). Rhino's arrangement draws a warped clip linearly, out by up to the drag distance once markers move, while the clip editor draws it exactly ([Time warp](time-warp.md)).

## How the rule is held

Tests compare picture and sound by different routes. `native/src/tests/EqTest.cpp` sweeps sines through `EqEngine`, measures what comes out, and requires it within 0.35 dB of `responseDbAt`. `ForgeTestsDisplays.cpp` checks each slot curve against the function the engine calls. `ForgeTestsEnvelope.cpp` requires the offset drawn on a knob to equal ENV 2's own display reading.

## How to apply

Put the transfer function in the DSP header as a free function and call it from the paint code. If the engine has nothing a display can call, add it to the engine, not to the UI.

## Related

- [Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)
- [Forge mixer and effects racks](forge-mixer-and-fx.md)
- [Forge modulation: matrix, envelopes, LFOs and macros](forge-modulation.md)
- [Forge warp](forge-warp.md)
