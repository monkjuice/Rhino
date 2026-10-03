---
title: Why a patch copied from Serum sounds different
type: gotcha
summary: Check matrix polarity, PD warp depth, the filter's FREQ and the output stage before suspecting Forge's tuning or DSP.
tags: [forge, serum, sound-design]
sources: []
updated: 2026-10-03
---

# Why a patch copied from Serum sounds different

Patches copied knob for knob from Serum 2 have sounded an octave out, thin or wrongly filtered; so far the cause has been one of these differences, not a broken oscillator. Check them first, then measure ([Comparing Forge with Serum](comparing-forge-with-serum.md)).

## 1. Matrix polarity

A Serum matrix row has a POL switch that makes its source bipolar. With POL on, a macro *at rest* pulls the destination as far negative as the depth reaches, which on a pitch destination at full depth is a whole octave. Forge's equivalent is the per-slot **BI** chip (`mod{n}Bipolar`, off by default so older patches are unchanged). It subtracts 0.5 from a source that only rises and leaves LFOs alone. Compare absolute offsets, not the steps between macro positions: `2m × 12` and `m × 24` take the same steps. Against a Serum capture with POL on, the macro at 0% gave −11.96 st in Serum and −12.00 in Forge.

Serum also floors a modulated SEM to whole semitones. Forge stays continuous on purpose: with no CRS control, `A/B/C PITCH` are its only pitch destinations, and snapping would kill vibrato and slides.

## 2. PD warp depth

Forge's PD used to be `amount × 4` cycles, about nine times Serum's index at the same percentage. At 37% the carrier all but cancelled, and a bass kept 0.2% of its energy below 100 Hz where Serum kept 44%. It is now `warpPdCycles = 0.42` (`core/ForgeWarp.h`), which re-voiced every patch using PD. The value was calibrated from one captured point, Serum at 37%, and is assumed linear elsewhere. Serum's upper sideband varies by about 24% between takes because RAND randomises phase, while Forge's holds within 1%. Judge a match on the lower sideband, the centroid and the band energies.

## 3. The filter's FREQ

On the dual types, FREQ is the second filter's own absolute corner, not an offset from CUTOFF. Forge once had it as an offset, until a Serum screenshot showed CUTOFF at its minimum (its peak below the display) and a notch kilohertz higher that only an absolute FREQ explains. Read the knob angles before tuning anything. A Serum knob sweeps 270°: 7:30 is 0, 12:00 is 50% and 4:30 is 100%.

## 4. Level

Core's master stage is `output × 0.28`, so a full-scale oscillator at the default output reads about −13.5 dB through Core. Measure the piece itself, on the user's actual file, at several notes ([Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)). "The spectral oscillator is a little quiet" turned out on 2026-10-03 to be two level problems — a bin-by-bin pitch shift losing 7-11 dB an octave from the root, and samples arriving unnormalised — plus a pitch one, a 44.1 kHz file playing 147 cents sharp; all three are fixed ([Forge spectral oscillator](forge-spectral.md)).

Note names misled too: Forge called MIDI 60 C4, so keys labelled alike sounded an octave apart. It now says C3, as Serum, Live, FL and Logic do (`src/ForgeEditor.cpp`).

## Related

- [Forge modulation: matrix, envelopes, LFOs and macros](forge-modulation.md)
- [Forge warp](forge-warp.md)
- [Forge filter](forge-filter.md)
- [Comparing Forge with Serum](comparing-forge-with-serum.md)
