---
title: Forge warp
type: component
summary: Two warp stages per oscillator, 43 modes in nine families, OSC modes able to read either other oscillator, chained through one read function and anti-aliased by duller tables.
tags: [forge, oscillators, warp]
sources: []
updated: 2026-10-13
---

# Forge warp

Each oscillator has two warp stages (`warpSlots`). Each stage is a MODE and a depth, and the two apply in order (`core/ForgeWarp.h`). Depths are modulation destinations. Modes are not, since sweeping through unrelated modes only stutters.

## Modes

There are 43 modes (`warpModeCount`) in nine families:

- OFF
- SYNC
- ALT WARP: 9 phase bends
- FILTER: 3, pitch-tracked
- DISTORTION: 8 shapers
- PD: 5
- FM: 8
- AM: 4
- RM: 4

Indices are stored, so the list is append-only ([Stored indices are append-only](append-only-stored-indices.md)). The four PD modes first shipped labelled FM and kept their slots when renamed. The five previous-oscillator modes (`pdOscPrevious` … `rmOscPrevious`, indices 38-42) were appended on 2026-10-13 (commit `d0a7e0f`), so no preset format bump was needed.

## Names and shown order

Names depend on the oscillator. `warpModeName(mode, carrier)` (`core/ForgeVoiceParts.h`) adds the letter an OSC mode reads, so PD lists PD OSC B and PD OSC C on A, and PD OSC A and PD OSC C on B. There is no one-argument form, so every caller must name the oscillator. The names come from a static table as `const char*`, because a selector's `choices` holds pointers. Host lanes (`src/ForgeParameters.cpp`) and tooltips (`ui::warpTooltipFor(mode, carrier)`) use the same names.

The shown order is not the stored order. `warpModeOrder(carrier)` lists the families in `WarpCategory` order, which puts FM before PD as the menu does. Within a family, each OSC mode sits beside its partner, in letter order. The warp field's selector holds a position, not a mode, and `warpModePosition`/`warpModeAt` convert between them (`src/ForgeEditorWarp.cpp`, `src/ForgeEditor.cpp`). The field's arrows therefore step FM before PD.

FM alone changes the phase increment (`warpPitchFactor`). Linear FM holds the note and clamps at zero (Serum's "can't do thru-zero"). Exponential FM sweeps ±4 octaves and does not hold the note. PD moves the read point and keeps the note.

## Sources

The sources are OSC, SUB and NOISE, plus SELF for PD. They are wired in `renderOscillators` (`core/ForgeCore.h`):

- **OSC** is either of the other two oscillators, named relative to the carrier. `nextOscillator` modes read the next one round the ring (A reads B, B reads C, C reads A), and the `*OscPrevious` modes read the one before it (A reads C, B reads A, C reads B). Because sources are relative, every mode is valid on every oscillator and no stage can read itself. `warpSourceOscillator(mode, carrier)` resolves the source to an index, and `warpOtherSide(mode)` gives a mode's partner. Before 2026-10-13 every OSC mode read the next oscillator, so each carrier could reach only one neighbour.
- **OSC and SUB** stages turn off when their source is disabled. A live stage would still ask for bandwidth and dull the carrier. The source's level can be zero.
- **NOISE** is Core's own white generator. It runs even with the NOISE module off, and it ignores that module's SOURCE. Only OSC and SUB sources must be enabled.
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

`tests/ForgeTestsWarp.cpp` checks direction by behaviour. For each carrier, AM OSC and its partner must modulate when only the named oscillator is on, and do nothing when only the other one is. When the engine was temporarily set back to next-only reading, 11 of those checks failed.

## Related

- [Forge oscillators and wavetables](forge-oscillators.md)
- [Forge spectral oscillator](forge-spectral.md)
- [Stored indices are append-only](append-only-stored-indices.md)
- [Why a patch copied from Serum sounds different](serum-patches-sound-different.md)
