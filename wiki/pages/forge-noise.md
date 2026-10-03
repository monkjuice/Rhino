---
title: Forge noise module
type: component
summary: A per-voice noise oscillator with nineteen generated sources in six families, each a colour through a character stage.
tags: [forge, noise, dsp]
sources: []
updated: 2026-10-03
---

# Forge noise module

NOISE is a small per-voice oscillator, not a hiss knob (PLAN.md M14, M14b). Its controls are SOURCE, TONE, STEREO and LEVEL. PAN, routing and the sends belong to the mixer's NOISE strip.

The DSP is in `core/ForgeNoise.h`. It depends on nothing else in Forge, so the engine renders from it and `tests/ForgeTestsNoise.cpp` measures it directly.

## Sources

There are 19 sources (`noiseSourceCount`) in six families:

- **COLOUR:** WHITE, PINK, BROWN, BLUE, VIOLET, GREY
- **ANALOG:** POLY, POLY HP, MONO, TAPE, HUM
- **DIGITAL:** BRIGHT, BIT, ALPHA
- **INHARMONIC:** METAL
- **ORGANIC:** VINYL, WIND
- **TRANSIENT:** GEIGER, CRACKLE

All are generated rather than recorded. The analog ones are named for their sound, not for the hardware whose character they borrow.

There are two orders. The stored order is the append-only `NoiseSource` enum, which is why GEIGER is index 3, among the colours, in presets and host lanes. The shown order groups by family. The two meet only at `noiseSourceAt` and `noiseSourcePosition` ([Stored indices are append-only](append-only-stored-indices.md)).

## A colour through a character

- **Colour.** White, pink and brown are the slow part (brown's pole sits at 8 Hz). All three run every sample, whatever source is selected.
- **Character.** Each source adds a fast stage on top of a colour. Only the live character stage runs, plus the one fading out.

So the cost is two stages however long the list grows. M14 ran every source all the time, which could not scale to 19. A stage is reset as it comes in, which is free because its input never went cold.

Source changes and the power switch cross over in 6 ms (`noiseFadeSeconds`).

## Level, tone, width

- **Level.** Each source's gain relative to white is measured, not derived: 3 s of every source are rendered at `initialise`. The result is cached per sample rate in a table shared across the process.
- **TONE** tilts the spectrum about 1 kHz. At 12 o'clock it returns its input exactly. The level change caused by the tilt is trimmed back out.
- **STEREO** is decorrelation, not width. There are two generators per voice: the left channel uses one directly, and the right is an equal-power mix of the two. At zero, the module sums to mono without cancelling.

## Determinism

Each voice's generators are seeded from a note counter that resets with the engine. Two notes therefore differ, but rendering a phrase twice gives the same file both times.

The LFO sample-and-hold and the warp NOISE source do not use this module. They use Core's own white generator ([Forge warp](forge-warp.md)).

## Lessons

CRACKLE runs at 1200 events per second. At 320 it overlapped as much as GEIGER's longer clicks and was just as peaky. The test counts events, because crest factor ranked the two the wrong way round.

M14c (sample sources and a density knob) has not started.

## Related

- [Forge engine (Core)](forge-engine.md)
- [Forge warp](forge-warp.md)
- [Forge mixer and effects racks](forge-mixer-and-fx.md)
- [Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)
