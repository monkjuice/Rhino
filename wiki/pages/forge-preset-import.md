---
title: Forge preset import (Serum 2 and Vital)
type: component
summary: Forge opens Serum 2 and Vital presets by converting each one into a Forge state tree through one PatchWriter and loading that tree like a .forgepreset; anything Forge lacks is left out with one warning each, and all 75 local Vital presets and all 626 Serum 2 factory presets convert.
tags: [forge, presets, serum, vital, import]
sources: []
updated: 2026-10-19
---

# Forge preset import (Serum 2 and Vital)

Built on 2026-10-18/19. The code is in `instruments/rhino-forge/src/`:

- `ForgePresetImport.*` holds the entry points, `PatchWriter` and the LFO fitting.
- `ForgePresetImportInternal.h` is private to the converters.
- `ForgePresetImportVital.cpp` and `ForgePresetImportVitalTables.cpp` convert Vital presets.
- `ForgePresetImportSerum.cpp` and `ForgePresetImportSerumFx.cpp` convert Serum 2 presets.
- `ForgeCbor.*` is Forge's own small CBOR reader.

Conversion runs on the message thread only.

## One way in

`importPreset` chooses a converter by file extension. The converter fills an `ImportedPreset`: a `RhinoForgeState` tree plus `PresetInfo`, whose `source` is "Serum 2" or "Vital" and whose `warnings` list what was lost.

`Processor::loadPreset` applies that tree through `applySavedState`, exactly as it applies a `.forgepreset` ([Forge presets and state](forge-presets-and-state.md)). `convertPreset` writes the tree to a file instead.

`PatchWriter` does all the writing:

- It sets plain values clamped to each parameter's range. A value more than 1% of the span past an end is warned by the end it is held at ("ENV 2 RELEASE: past Forge's maximum, set to 8.00 s"), never by its original value. A parameter formats only values inside its range ([A JUCE parameter cannot print a value outside its range](juce-parameter-text-clamps-to-range.md)), and a raw number could be in units the knob does not show, such as linear gain on a dB knob.
- It puts each routing in the next free of the 30 matrix slots.
- It writes `TABLE` nodes, LFO-table nodes and macro names through the same helpers a save uses (`wavetableStateNode`, `lfoTableStateNode`).
- It writes every unused slot as off, so slot 1's stale default never survives into a converted patch ([Hazards found while seeding the wiki](known-hazards.md)).

**Leave things out, and say so.** At the user's request, anything Forge has no equivalent for is left out with one warning, not approximated. Examples are Vital's random sources and wavetable modifiers, Serum's spectral, sample, multisample and granular oscillators, a second filter, a flanger and chaos LFOs. The warnings travel with the patch, so the title plate, its tooltip and the browser still show them after the preset is saved into the bank ([Forge preset browser and bank](forge-preset-browser.md)).

## Rules both converters share

- **Depth from both ends of the reach.** The importer sends the source synth's control to both ends of its reach through that synth's own knob law. The routing's depth is the difference between the two results in Forge's normalised 0..1.
- **A unipolar LFO recentres the setting.** Forge's LFOs swing ±1, so for a unipolar LFO routing the importer moves the setting by half the reach. It first clamps the reach to what the control can actually travel. Without that clamp, an LFO taking a level of 0.3 down by 100% parked the level below zero and the routing did nothing. That was the cause of most early silent conversions.
- **BI swings half a depth either way**, so a bipolar routing from anything other than an LFO keeps its whole reach. A factor-of-two error here was fixed during this work.
- **Modulation of a modulation is folded in.** This covers Vital routings into `modulation_N_amount` and Serum's aux sources. The importer evaluates them at a typical source position: a macro or the wheel as saved, velocity 0.8, note 60, an LFO or random source 0.5. Each fold is warned.
- **A saved mod-wheel position is warned, not applied**, because Forge keeps the wheel out of patches.
- **LFOs and envelopes past Forge's counts** are allocated in order of use. ENV 1 stays the amplitude envelope.

## Vital

- **Stored domains.** Values are converted from the domains Vital stores them in. Cutoff is a MIDI note, envelope time is `v^4` seconds and LFO rate is `2^v` Hz. Levels are squared, and so is ENV 1, so its sustain is squared too.
- **Wavetables** are rendered from keyframes to at most 64 frames. The importer handles Wave Source (time or frequency interpolation), Line Source and Audio File Source. Modifiers are not rendered, and each one is warned.
- **LFO lines** are fitted to Forge's bent segments (`import::lfoTableFrom`) or matched to one of Forge's four shapes.
- **Effects** go onto the MAIN rack in `effect_chain_order`.
- **The sample oscillator** is played by Forge's noise module, with the noise source chosen from the sample's name. A name that does not describe a noise falls back to white noise with a warning, and the sample's pitch is not converted.

## Serum 2

The file layout and the knob laws the converter relies on are in [Serum 2's preset format](serum-2-preset-format.md). Forge's warps and matrix were already calibrated to Serum's, so warp depths and routing depths map one to one. Serum's racks 0, 1 and 2 become Forge's MAIN, BUS 1 and BUS 2.

## Results and open items

As of 2026-10-19:

- All 75 local Vital presets and all 626 Serum 2 factory presets convert without error.
- 50 of the Serum presets are silent, every one because its oscillators use engines Forge lacks, and their warnings say so. Two (BA - Kitchen Sink, GTR - Future Punk Powercords) peak at 0 dBFS; why has not been looked into. Counted by auditioning every preset with `--audition` on 2026-10-19.
- Converted Serum presets play quieter: their median peak is about −27 dBFS, against about −16 dBFS for Vital conversions and for Forge's own presets. Absolute level has not yet been calibrated against Serum. That calibration is an open item, to be done by capture ([Comparing Forge with Serum](comparing-forge-with-serum.md)).

## Tests and tools

Area `import` (`tests/ForgeTestsImport.cpp`, CTest `forge_import`) writes its own Vital preset and its own `.SerumPreset`, then checks the conversion setting by setting. The Serum file is hand-encoded CBOR inside zstd *stored* blocks, so the test needs no compressor.

The same area holds a regression for `serumStyle`'s folder walk. For a file outside Serum's folder the walk never ended, because a drive root is its own parent. A loop that climbs with `getParentDirectory` must stop when the parent equals the folder.

`--audition` and `--convert` run any preset through the importer from the command line ([Build and test Forge](build-and-test-forge.md)).

## Related

- [Serum 2's preset format](serum-2-preset-format.md)
- [Forge preset browser and bank](forge-preset-browser.md)
- [Forge presets and state](forge-presets-and-state.md)
- [Forge modulation: matrix, envelopes, LFOs and macros](forge-modulation.md)
- [Forge LFOs](forge-lfos.md)
- [Why a patch copied from Serum sounds different](serum-patches-sound-different.md)
