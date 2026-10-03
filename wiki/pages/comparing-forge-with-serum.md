---
title: Comparing Forge with Serum
type: guide
summary: Settle "it sounds different in Serum" by capturing and measuring both synths, reading screenshots by pixel, and using Serum's own tables.
tags: [forge, serum, audio, measurement]
sources: []
updated: 2026-10-03
---

# Comparing Forge with Serum

A patch that "sounds thinner than in Serum" is settled by measurement: not by asking the developer to describe it
better, and not by reading the DSP ([Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)). First rule
out the known causes in [Why a patch copied from Serum sounds different](serum-patches-sound-different.md).

## Capture both synths

The tooling is the user-level `synth-ab` skill (`%USERPROFILE%\.claude\skills\synth-ab\`), kept out of the repository
on purpose. `capture.py` records the Windows loopback through ffmpeg, leaving Start, Stop, Save and Discard to the
developer; `analyze.py` prints two takes side by side: partials, centroid, band energies, side/mid width per band.

1. **Close the Forge standalone** when the take comes from a DAW: it hears the same keyboard and plays into the same
   capture.
2. **Check the route.** The loopback device is the onboard audio's Stereo Mix, which mirrors only the onboard output: a
   USB interface as the default playback device records silence, and a DAW on the onboard ASIO driver bypasses it.
3. **Match the settings and write them down** (note, octaves, macros, anything with RAND), and take both captures in
   one session, peaking near -6 dBFS; the loopback is 16-bit.

Record long and find the note by its envelope. Stop ffmpeg by writing `q` to its stdin (a killed one leaves a header
Python's `wave` refuses); its WAV data starts at byte 78, not 44.

## Read the numbers honestly

- **Identify the take before trusting it.** Partial ratios and exact detune reconstruct the synth and the note: a take
  whose OSC B sat +0.864 st sharp matched a macro's modulation in the saved Forge preset.
- **Level-match first.** Three decibels reads as "fuller" to everyone.
- **Serum's upper sideband scatters about 24% between takes** with RAND at 100, while Forge's holds to 1%. Judge on the
  lower sideband, the centroid and the band energies.

## Shapes and screenshots

- Serum's factory wavetables sit in `%USERPROFILE%\Documents\Xfer\Serum 2 Presets\Tables\` as 32-bit float WAVs of
  2048-sample frames, Forge's own frame size (`wavetableFrameSize`). Decode them with ffmpeg and compare frames
  directly; a capture's noise floor hides any shape difference worth having.
- In a reference screenshot, trace the curve per column by maximum blueness, `b - (r+g)/2`, and read the knob pointers:
  a Serum knob sweeps 270°, so 7:30 is 0%, 12:00 is 50% and 4:30 is 100%. The knobs usually explain the difference. A
  "missing" filter peak sat at 8 Hz because CUTOFF was at its minimum, and the notch above it proved FREQ is an
  absolute frequency ([Forge filter](forge-filter.md)).
- Measure first, then read the manual for intent, then write the reference case into a test with its settings in a
  comment. In the Serum 2 User Guide (Downloads folder; printed page = PDF page): warp pp. 49-54, spectral pp. 104-122,
  filter types pp. 135-139, mixer pp. 144-151, effects from p. 152, arpeggiator pp. 244-266.

After a fix, such as PD warp's depth ([Forge warp](forge-warp.md)), re-record at matched settings and put the numbers
in the commit message.

## Related

- [Why a patch copied from Serum sounds different](serum-patches-sound-different.md)
- [Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)
- [Forge warp](forge-warp.md)
- [Forge filter](forge-filter.md)
- [Development environment and reference material](development-environment.md)
