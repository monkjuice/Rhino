---
title: Forge warp
type: component
summary: Two warp stages per oscillator, 38 modes in nine families, chained through one read function and anti-aliased by duller tables.
tags: [forge, oscillators, warp]
sources: []
updated: 2026-10-03
---

# Forge warp

Each oscillator has two warp stages (`warpSlots`). Each stage is a MODE and a depth, and the two apply in order (`core/ForgeWarp.h`, PLAN.md M12). Depths are modulation destinations. Modes are not, since sweeping through unrelated modes only stutters.

## Modes

There are 38 modes (`warpModeCount`) in nine families:

- OFF
- SYNC
- ALT WARP: 9 phase bends
- FILTER: 3, pitch-tracked
- DISTORTION: 8 shapers
- PD: 4
- FM: 6
- AM: 3
- RM: 3

Indices are stored, so the list is append-only ([Stored indices are append-only](append-only-stored-indices.md)). The four PD modes first shipped labelled FM and kept their slots when renamed.

FM alone changes the phase increment (`warpPitchFactor`). Linear FM holds the note and clamps at zero (Serum's "can't do thru-zero"). Exponential FM sweeps ±4 octaves and does not hold the note. PD moves the read point and keeps the note.

## Sources

The sources are OSC, SUB and NOISE, plus SELF for PD. They are wired in `renderOscillators` (`core/ForgeCore.h`):

- **OSC** is the next oscillator in a ring: A reads B, B reads C, C reads A.
- **OSC and SUB** stages turn off when their source is disabled. A live stage would still ask for bandwidth and dull the carrier. The source's level can be zero.
- **NOISE** is Core's own white generator. It runs even with the NOISE module off, and it ignores that module's SOURCE. Forge's `README.md` says every source must be on; only OSC and SUB must be (checked 2026-10-03).
- **Spectral oscillators** ignore warp. An OSC stage reading one reads its wavetable at a phase only the wavetable path
  advances, so it most likely gets a constant (traced in the code on 2026-10-03, not measured;
  [Forge spectral oscillator](forge-spectral.md)).

## One implementation, two readers

A stage is handed a `read(phase)`. The voice passes a band-limited table read, the panel passes the authored table, and stage 2 is passed stage 1. With no buffer between stages, phase-domain and sample-domain modes commute.

ODD/EVEN reads twice, so a FILTER in the stage it reads runs at double rate, an octave high. The display draws only SYNC, ALT WARP and DISTORTION.

## Neutral depth and aliasing

Each mode leaves the wave untouched at its `warpNeutralDepth`. That is 0 for most modes, and 12 o'clock for BEND +/−, ASYM and ODD/EVEN. MIRROR also sits at 12 o'clock but always folds. Double-click returns the depth there.

Forge does not oversample. Each mode declares its extra bandwidth (`warpHeadroom`, capped at 16×), and the oscillator reads a duller table level. FLIP and QUANTIZE still alias. SYNC crossfades over the last 2% of the master cycle.

`warpPdCycles = 0.42` was matched to a Serum capture on 2026-09-18. It had been 4 cycles, about nine times deeper, which made copied patches thin. Only that depth was measured.

Tests measure pitch by period, not by the tallest partial, because SYNC makes its own harmonic the loudest.

## Related

- [Forge oscillators and wavetables](forge-oscillators.md)
- [Forge spectral oscillator](forge-spectral.md)
- [Stored indices are append-only](append-only-stored-indices.md)
- [Why a patch copied from Serum sounds different](serum-patches-sound-different.md)
