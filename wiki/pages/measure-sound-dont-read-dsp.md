---
title: Measure sound, don't read the DSP
type: convention
summary: Claims about pitch, level or timbre are settled by rendering audio and measuring it by a route that cannot agree with the DSP by construction.
tags: [both, testing, audio, measurement]
sources: []
updated: 2026-10-09
---

# Measure sound, don't read the DSP

## The rule

What a synth or a device *sounds* like is settled by rendering audio and measuring it, never by reading the code that makes it (`AGENTS.md`, *Verifying pitch, level or timbre*). Measure by a different route from the DSP (a transform, not the phase accumulator; a sweep, not the coefficients), so the two cannot agree by construction.

## The patterns

- **Pitch.** Forge's `tuningSuite()` (`tests/ForgeTestsOscillator.cpp`) renders notes at 44.1 and 48 kHz, finds each fundamental by FFT peak interpolation, and requires it within 2 cents of nominal. `AGENTS.md` still places it in a `ForgeTests.cpp` that no longer exists. Rhino's `AutoTuneTest.cpp` does the same with a windowed transform on a synthesised vowel ([Rhino Tune](rhino-tune.md)).
- **Response.** `EqTest.cpp` sweeps sines through [Rhino EQ](rhino-eq.md) and compares the output with the drawn curve. `VocoderTest.cpp` measures which of four carrier tones a single modulator tone lets through.
- **Level.** Forge's `levelSuite()` (`tests/ForgeTestsSpectral.cpp`) requires a [spectral oscillator](forge-spectral.md) to keep a sample's level at its root and an octave either side.
- **Tempo, mix and effect.** `DjTest.cpp` measures the DJ mixer's bands, kills, fader curves, crossfader and cue bus on tones, the compressor by how far it holds a loud tone down and lifts a quiet one, the send unit by when a click comes back, the mic's talkover by the master's dip and its release, a braked stop and start by the frames of material they cover, a hand on the platter by which way the deck runs, the pitch effect by zero-crossing frequency, a vinyl brake by the silence it ends in, times an echo's repeats and a trans gate, and analyses a synthesised 126.5 BPM A-minor beat with a drop at bar 8 ([DJ view and the booth](dj-view.md)).
- **Fail against the old behaviour.** Forge's voice-tail check requires a stopping voice to end more than 66 dB below its peak. The old hard cut left 39 dB, so the test fails on the code it replaced. A test that fails the first time it runs is worth more than one written to pass: `EqTest`'s click check caught coefficients swapped without a crossfade.

## Lessons the tests taught

- Do not test ties. C# is as far from C as it is from D.
- Prefer a statistic over the whole spectrum to a single peak. A formant shift measured by its tallest partial read as broken while it worked.
- Print the measured and the wanted value.
- Tell a played note from a harmonic by its amplitude ratio: a saw's second harmonic is 0.50 of the fundamental, so 0.46 is a harmonic and 0.99 a note on top of one.
- When a measured number is wrong, replicate the arithmetic outside the binary and print the curve. The DJ analyser read 158.2 BPM for a 126.5 beat; a Python replica of its onset envelope and autocorrelation showed a peak every 0.4 beats, the ripple of a 256-frame column shorter than one cycle of the kick, which pointed at the fix (a 46 ms average for the tempo envelope) in one round rather than several guesses at the DSP.
- Ask of each band what its crossovers can give. The DJ channel's four-band isolator has fourth-order Linkwitz-Riley crossovers at 150 Hz, 600 Hz and 3 kHz, so its two inner bands are two octaves wide and a tone in the middle of one keeps about −19 dB of its neighbours whatever the kill does. `checkMixer` therefore asks −30 dB of the outer bands and −15 dB of the inner ones; one figure for all four would have failed the product for physics.
- Let smoothing settle before measuring. `DjSendFx` glides its delay time a thousandth of the way each sample, so a type change takes roughly 100 blocks of 512 to reach the time the knob names; the send-return check runs those blocks first, or the click returns early and the test blames the delay.

## A level complaint

Forge's master stage multiplies by `output * 0.28` (`core/ForgeCore.h`), which is -13.5 dB at the default output, so a level read through `Core` misleads. Measure the suspect piece alone, on the user's own file, at several notes. On 2026-10-03, "the spectral oscillator is a bit quiet" turned out to be three causes: a bin-by-bin pitch shift (11 dB lost an octave down, 7 dB up), unnormalised samples, and a 44.1 kHz file playing 147 cents sharp. A sine at the root had measured perfectly; the worst loss sat two octaves away.

Decode files with Python's `wave` plus numpy, or with an `ffmpeg.exe` bundled in software under `%LOCALAPPDATA%\Programs`. To compare with another synth, record both and measure ([Comparing Forge with Serum](comparing-forge-with-serum.md)).

## Related

- [Displays draw from the DSP](displays-draw-from-the-dsp.md)
- [Writing Rhino tests](writing-rhino-tests.md)
- [Why a patch copied from Serum sounds different](serum-patches-sound-different.md)
