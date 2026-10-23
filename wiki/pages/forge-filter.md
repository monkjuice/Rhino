---
title: Forge filter
type: component
summary: Two per-voice filters of 34 types in six families, each source routed into either, both or neither and FILTER 1 optionally in series into FILTER 2, shown as one two-bank module, with the drawn response and the audio read from the same header.
tags: [forge, filter, dsp]
sources: []
updated: 2026-10-22
---

# Forge filter

FILTER is two filters per voice since 2026-10-22 (M15; `filterCount` in `core/ForgeVoiceParts.h`), each fed by whichever sources are routed into it. Their DSP lives in `core/ForgeFilter.h`, which depends on nothing else in Forge. `filterSample` renders the audio and `filterMagnitude` draws the response, and both read the same `FilterShape` (`filterShapeOf` in `core/ForgePatch.h`). So the drawn curve cannot disagree with what is rendered ([Displays draw from the DSP](displays-draw-from-the-dsp.md)).

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

- **Knobs.** CUTOFF (30 Hz–18 kHz), RES and DRIVE, then FREQ, PAN and MIX. PAN and MIX belong to the mixer's FILTER 1 or FILTER 2 channel. Both filters have the same controls.
- **The fourth knob.** Each type's row defines it: FREQ, MORPH, FAT, PAIN, DAMP, STAGES, SHIFT, SPREAD, DIFF or FEED. The row also sets its label, readout, double-click value and opening value.
- **FREQ is an absolute second corner, not an offset.** It was first built as an offset. A Serum screenshot then showed CUTOFF at its minimum with a notch several kHz higher, which an offset could not reach. Read the knob angles before tuning to a reference image ([Comparing Forge with Serum](comparing-forge-with-serum.md)).
- **Seated rows.** TYPE sits along the display's top edge. The routing chips A, B, C, S and N, plus KEY, sit along its foot (`Seat` in `ui/ForgeModule.h`). The oscillators also seat MODE and their loop strip.
- **DRIVE** affects only the routed sources. Switching the module off bypasses DRIVE as well, while the filter state keeps running so that switching back on does not click.
- **KEY** tracks the voice's sounding pitch, including glide, from middle C. It is off by default, and the display ignores it.

## Display

The curves are analogue prototypes: Core prewarps its corners, so the knee sits where the knob says. Types that are not linear draw the most truthful picture they can:

- RING MOD and DIFFUSOR draw a flat line at unity, labelled PHASE ONLY.
- S&H draws the sinc of its hold.
- REVERB draws the envelope its damping puts on the tail.

**Pitfall:** the FX rack's FILTER type is a separate SVF in `core/ForgeFxDsp.h`, with its own mapping from resonance to damping. The same RES value therefore gives a different Q there ([Forge mixer and effects racks](forge-mixer-and-fx.md)).

## Two filters

- **Routing.** Each source (A, B, C, SUB, NOISE) has a route switch into each filter: one, the other, both (heard through each, in parallel, so twice) or neither (dry to the output). Each filter is a whole channel, `Core::renderFilter`: drive, filter, MIX, LEVEL, PAN and sends, run for FILTER 1 then FILTER 2.
- **Series.** `filterSerial` (host name "Filter 1 Destination", MAIN or FILTER 2) sends FILTER 1's channel output into FILTER 2's input. It is a destination, not a send: in series FILTER 1 reaches the output only through FILTER 2.
- **Ids.** Filter 1 kept every old id (`cutoff`, `resonance` and `drive` bare, `routeA`..`routeNoise`, `filterType`, `filterEnable` ...), so old patches and automation still point at it. Filter 2's are `filter2` + the suffix (`filter2Cutoff`, `filter2RouteA` ...), spelled by `filterParameterId(filter, suffix)`, and they and `filterSerial` are appended at the end of the parameter list, after the arp ([Stored indices are append-only](append-only-stored-indices.md)). Filter 1's route toggles now read On/Off, not MAIN/FILTER. In the patch each filter is a `FilterSetting` in `patch.filters`.
- **An unfed FILTER 2 is skipped.** `filterFed(patch, filter)` is always true for filter 1, and true for filter 2 when any source is routed into it or `filterSerial` is on. Filter 2 defaults to every route off, so a patch that predates it renders, and costs, exactly as before. When filter 2 becomes fed again, `resetFilter` zeroes every voice's state and delay lines for it, so no stale ring from its last use replays.
- **One module, two banks.** At the user's request FILTER 2 is not a module of its own: FILTER is a two-bank module switched by cards "1" and "2" on the end of its title plate. The header LED, the display (`filterShape(shownFilter())`) and the seated chips follow the bank shown ([Forge editor (panel)](forge-editor.md)). `refreshFilterFields` runs every 24 Hz tick, so it matches TYPE and FREQ through cached id arrays (`filterOfTypeControl`, `filterOfFreqControl`, `src/ForgeEditorInternal.h`) instead of building id strings per tick, which would allocate. Filter 2's tooltips are filter 1's entries with FILTER 1 read as FILTER 2 (`ui/ForgeTooltips.h`).
- **Not imported yet.** The Serum 2 and Vital converters still fill FILTER 1 only ([Forge preset import](forge-preset-import.md)).

`twoFiltersSuite` (`tests/ForgeTestsFilter.cpp`) holds the rules above. An unrouted FILTER 2 leaves a render unchanged to the bit, a filter switched off passes audio, a source routed into both is heard twice, and in series FILTER 1 is heard only through FILTER 2. It also checks FILTER 2's fader, pan, sends and an F2 CUTOFF macro. On 2026-10-22, `--render` and `--audition` of all 18 factory presets were byte-identical to a worktree build of `ec69248`, and `--profile-audio` stayed within noise at zero allocations per block ([Proving a Forge change changed nothing](proving-a-forge-change-changed-nothing.md)).

## Time-varying stability

The BASIC SVF's FAT control adds level-dependent damping. That damping is part of the zero-delay implicit solve: `filterSvf` derives a nonnegative feedback coefficient from `1 - tanh(x) / x` and applies the same coefficient to both the numerator and denominator. Keeping it in only the numerator can appear stable at a fixed cutoff yet run to Inf/NaN when cutoff moves quickly, because the new high cutoff coefficient solves the old state against an inconsistent denominator. Once a voice state becomes non-finite it can poison the output until the engine is reset.

The coefficient tends to zero with signal level, so the small-signal response is preserved; the `heat == 0` path retains its original arithmetic. `tests/ForgeTestsFilter.cpp` protects the live failure mode at 44.1 kHz in 441-sample blocks: a hot resonant patch repeatedly sweeps the full cutoff range, then must remain finite and audible.

## Related

- [Forge engine (Core)](forge-engine.md)
- [Forge mixer and effects racks](forge-mixer-and-fx.md)
- [Displays draw from the DSP](displays-draw-from-the-dsp.md)
- [Forge editor (panel)](forge-editor.md)
- [Forge modulation: matrix, envelopes, LFOs and macros](forge-modulation.md)
- [Why a patch copied from Serum sounds different](serum-patches-sound-different.md)
