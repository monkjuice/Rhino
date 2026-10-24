---
title: A Forge preset with no subPan loads its sub 3 dB hotter
type: gotcha
summary: Processor::migrated takes any state with no subPan PARAM for one saved before the mixer and multiplies subLevel and noiseLevel by the square root of two, so the 18 original factory presets, which never wrote subPan, load their sub 3 dB above what make-presets.py states; left as is on purpose, and every new preset writes subPan and noisePan.
tags: [forge, presets, gotcha, levels]
sources: []
updated: 2026-10-23
---

# A Forge preset with no subPan loads its sub 3 dB hotter

## What happens

`Processor::migrated` (`instruments/rhino-forge/src/ForgeProcessorState.cpp`) repairs a patch saved before the mixer gave SUB and NOISE a pan, when their levels were on a scale 3 dB hotter. Its mark is `predatesMixer = !parameterEntry(result, "subPan").isValid()`: any state with no `subPan` PARAM. It multiplies `subLevel` and `noiseLevel` by the square root of two, clamped to 1, and adding `subPan` at its default is what clears the mark, so running it twice is safe ([Forge presets and state](forge-presets-and-state.md)).

A preset holds only what differs from the defaults, so a generated or hand-written preset that never mentions `subPan` carries the mark too, whatever its age. The 18 original factory presets are such files: each loads its sub and noise 3 dB above what `presets/make-presets.py` states. Sub Foundation's `subLevel` of 0.45 plays as 0.64. Found on 2026-10-23 while writing the Drum n Bass set.

## Why it stays

Fixing the 18 means adding `MIXED` (`subPan` and `noisePan` at 0) to the script's `BASE`, which drops every one of their subs by 3 dB and changes how they sound. They were left as they are, deliberately; read the script's SUB and NOISE figures for them as 3 dB low.

## What to do

Every new preset, in the script or by hand, writes `subPan` and `noisePan`: the `MIXED` dict, merged into each Drum n Bass preset, so their levels load as written; a sub patch cannot afford the extra 3 dB ([Writing Forge factory presets](writing-forge-factory-presets.md)). Check a preset's level on its `--audition` render, not in the script ([Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)).

## Related

- [Forge presets and state](forge-presets-and-state.md)
- [Writing Forge factory presets](writing-forge-factory-presets.md)
- [Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)
