---
title: Forge filter
type: component
summary: One per-voice filter of 34 types in six families, whose drawn response and audio come from the same header.
tags: [forge, filter, dsp]
sources: []
updated: 2026-10-03
---

# Forge filter

FILTER is one filter per voice, fed by whichever sources are routed into it (PLAN.md M4). Its DSP lives in `core/ForgeFilter.h`, which depends on nothing else in Forge. `filterSample` renders the audio and `filterMagnitude` draws the response, and both read the same `FilterShape` (`filterShapeOf` in `core/ForgePatch.h`). So the drawn curve cannot disagree with what is rendered ([Displays draw from the DSP](displays-draw-from-the-dsp.md)).

## Types

There are 34 types in six families (`filterTypes()`):

- **BASIC (5):** the taps of one zero-delay state-variable filter (SVF).
- **DUAL (12):** two SVFs in series, set by CUTOFF and FREQ.
- **MORPH (4):** three taps crossfaded by MORPH.
- **ANALOG (4):** LADDER, ACID, DIRTY and EMS.
- **RESONATORS (3):** COMB, FLANGER and PHASER.
- **CHARACTER (6):** FORMANT, RING MOD, S&H, DIFFUSOR, SCREAM and REVERB.

Type indices are stored in patches, so new types are only ever appended. LOW, HIGH and BAND are still 0, 1 and 2, and FREQ opens at 0 (FAT off), so old patches sound unchanged ([Stored indices are append-only](append-only-stored-indices.md)).

## Controls

- **Knobs.** CUTOFF (30 Hz–18 kHz), RES and DRIVE, then FREQ, PAN and MIX. PAN and MIX belong to the mixer's FILTER channel.
- **The fourth knob.** Each type's row defines it: FREQ, MORPH, FAT, PAIN, DAMP, STAGES, SHIFT, SPREAD, DIFF or FEED. The row also sets its label, readout, double-click value and opening value.
- **FREQ is an absolute second corner, not an offset.** It was first built as an offset. A Serum screenshot then showed CUTOFF at its minimum with a notch several kHz higher, which an offset could not reach. Read the knob angles before tuning to a reference image ([Comparing Forge with Serum](comparing-forge-with-serum.md)).
- **Seated rows.** TYPE sits along the display's top edge. The routing chips A, B, C, S and N, plus KEY, sit along its foot (`Seat` in `ui/ForgeModule.h`). Forge's `README.md` says the filter is the only module built this way, but the oscillators now seat MODE and their loop strip too (checked 2026-10-03).
- **DRIVE** affects only the routed sources. Switching the module off bypasses DRIVE as well, while the filter state keeps running so that switching back on does not click.
- **KEY** tracks the voice's sounding pitch, including glide, from middle C. It is off by default, and the display ignores it.

## Display

The curves are analogue prototypes: Core prewarps its corners, so the knee sits where the knob says. Types that are not linear draw the most truthful picture they can:

- RING MOD and DIFFUSOR draw a flat line at unity, labelled PHASE ONLY.
- S&H draws the sinc of its hold.
- REVERB draws the envelope its damping puts on the tail.

**Pitfall:** the FX rack's FILTER type is a separate SVF in `core/ForgeFxDsp.h`, with its own mapping from resonance to damping. The same RES value therefore gives a different Q there ([Forge mixer and effects racks](forge-mixer-and-fx.md)).

## Related

- [Forge engine (Core)](forge-engine.md)
- [Forge mixer and effects racks](forge-mixer-and-fx.md)
- [Displays draw from the DSP](displays-draw-from-the-dsp.md)
- [Why a patch copied from Serum sounds different](serum-patches-sound-different.md)
