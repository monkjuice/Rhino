---
title: Measure sound, don't read the DSP
type: convention
summary: Claims about pitch, level or timbre are settled by rendering audio and measuring it by a route that cannot agree with the DSP by construction.
tags: [both, testing, audio, measurement]
sources: []
updated: 2026-10-03
---

# Measure sound, don't read the DSP

## The rule

What a synth or a device *sounds* like is settled by rendering audio and measuring it, never by reading the code that makes it (`AGENTS.md`, *Verifying pitch, level or timbre*). Measure by a different route from the DSP (a transform, not the phase accumulator; a sweep, not the coefficients), so the two cannot agree by construction.

## The patterns

- **Pitch.** Forge's `tuningSuite()` (`tests/ForgeTestsOscillator.cpp`) renders notes at 44.1 and 48 kHz, finds each fundamental by FFT peak interpolation, and requires it within 2 cents of nominal. `AGENTS.md` still places it in a `ForgeTests.cpp` that no longer exists. Rhino's `AutoTuneTest.cpp` does the same with a windowed transform on a synthesised vowel ([Rhino Tune](rhino-tune.md)).
- **Response.** `EqTest.cpp` sweeps sines through [Rhino EQ](rhino-eq.md) and compares the output with the drawn curve. `VocoderTest.cpp` measures which of four carrier tones a single modulator tone lets through.
- **Level.** Forge's `levelSuite()` (`tests/ForgeTestsSpectral.cpp`) requires a [spectral oscillator](forge-spectral.md) to keep a sample's level at its root and an octave either side.
- **Fail against the old behaviour.** Forge's voice-tail check requires a stopping voice to end more than 66 dB below its peak. The old hard cut left 39 dB, so the test fails on the code it replaced. A test that fails the first time it runs is worth more than one written to pass: `EqTest`'s click check caught coefficients swapped without a crossfade.

## Lessons the tests taught

- Do not test ties. C# is as far from C as it is from D.
- Prefer a statistic over the whole spectrum to a single peak. A formant shift measured by its tallest partial read as broken while it worked.
- Print the measured and the wanted value.
- Tell a played note from a harmonic by its amplitude ratio: a saw's second harmonic is 0.50 of the fundamental, so 0.46 is a harmonic and 0.99 a note on top of one.

## A level complaint

Forge's master stage multiplies by `output * 0.28` (`core/ForgeCore.h`), which is -13.5 dB at the default output, so a level read through `Core` misleads. Measure the suspect piece alone, on the user's own file, at several notes. On 2026-10-03, "the spectral oscillator is a bit quiet" turned out to be three causes: a bin-by-bin pitch shift (11 dB lost an octave down, 7 dB up), unnormalised samples, and a 44.1 kHz file playing 147 cents sharp. A sine at the root had measured perfectly; the worst loss sat two octaves away.

Decode files with Python's `wave` plus numpy, or with an `ffmpeg.exe` bundled in software under `%LOCALAPPDATA%\Programs`. To compare with another synth, record both and measure ([Comparing Forge with Serum](comparing-forge-with-serum.md)).

## Related

- [Displays draw from the DSP](displays-draw-from-the-dsp.md)
- [Writing Rhino tests](writing-rhino-tests.md)
- [Why a patch copied from Serum sounds different](serum-patches-sound-different.md)
