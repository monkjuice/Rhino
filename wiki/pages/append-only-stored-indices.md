---
title: Stored indices are append-only
type: convention
summary: Forge saves choices and routings as list indices, so a list is only ever appended to; reordering one is a preset-format change.
tags: [forge, presets, compatibility]
sources: []
updated: 2026-10-03
---

# Stored indices are append-only

Forge saves a choice as its index into a list: a filter type, a warp mode, a noise source, an effect type, an arp shape, a modulation slot's source and destination. Presets and host state hold those numbers, so an insertion silently repoints every saved patch past it. Each of these lists carries a comment saying it is appended to and never inserted into or reordered: `FilterType` (LOW, HIGH and BAND are still 0-2), `WarpMode`, `NoiseSource`, `FxType`, `ArpShape`.

## How the rule shows up

- **Stored order and shown order differ.** Noise sources are stored in arrival order (GEIGER is 3, among the colours) and shown by family; the two meet only at `noiseSourceAt` and `noiseSourcePosition` (`core/ForgeNoise.h`).
- **A wrong name keeps its place.** Four warp modes first shipped as "FM" were really phase distortion; they were renamed PD and kept their indices.
- **Normalised fields keep their landing points.** An FX slot's mode fields are 0..1 floats read as `round(value × (count − 1))`. When the distortion's filter field grew from three choices to five, PRE LP and POST LP went to 2 and 4 so old values of 0.5 and 1 still land on a low pass (`core/ForgeFx.h`).
- **Modes are not modulation destinations**: sweeping unrelated choices is a stutter, so only continuous controls get a destination index.

## When an insertion happened anyway

LFO 2-6 and ENV 2-4 were inserted into the source list. `Processor::migrated` (`src/ForgeProcessorState.cpp`) detects states that predate them by the old `lfoShape`/`attack` names, renames those, and shifts every saved source index past the insertion point, oldest change first. LFO 1's old position is written as a literal, because the enum constant has moved since. Renaming clears the mark, so a second run does nothing.

## Reordering is a format change

Through format 2 the destination list grew only at its end, so its entries sat in the order they arrived. The third oscillator (commit `a5103e9`, 2026-09-28) rebuilt it compactly and bumped `presetFormatVersion` to 3, so format-2 presets are refused ("the previous destination schema is refused", `tests/ForgeTestsPresets.cpp`). `core/ForgeMatrix.h` now states the rule: the order is a schema, and changing it bumps the preset format rather than leaving historical gaps in hot-path lookup tables.

Two traps that bump left behind (checked 2026-10-03):

- Host state carries no version, so a Rhino project saved before the bump is reconciled, not refused, and its slot destinations past B PITCH now name other controls.
- A literal survived: slot 1's default destination is `13` in `src/ForgeParameters.cpp`, commented as the cutoff, and 13 is now C PAN. Write indices through named constants such as `cutoffDestination`.

Rhino's analogue is the device catalog id, which drag descriptions carry and which therefore never changes (`Equaliser` still names Rhino EQ; `native/src/devices/DeviceCatalog.h`).

## Related

- [Forge presets and state](forge-presets-and-state.md)
- [Forge modulation: matrix, envelopes, LFOs and macros](forge-modulation.md)
- [Forge noise module](forge-noise.md)
- [Forge warp](forge-warp.md)
- [Device catalog](device-catalog.md)
