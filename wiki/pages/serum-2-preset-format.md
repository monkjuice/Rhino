---
title: Serum 2's preset format
type: concept
summary: A .SerumPreset is a JSON metadata header followed by one zstd frame of CBOR; this page records the layout and the knob laws Forge's importer relies on, measured by the GPLv3 serum2vital project and taken from it as facts, not code.
tags: [forge, serum, presets, import, file-format]
sources: []
updated: 2026-10-19
---

# Serum 2's preset format

Read by `src/ForgePresetImportSerum.cpp` and `ForgePresetImportSerumFx.cpp` in `instruments/rhino-forge/` ([Forge preset import](forge-preset-import.md)). Checked against the 626 Serum 2 factory presets on 2026-10-19.

## The container

1. `"XferJson\0"`.
2. A u64 JSON length, then the JSON metadata: name, author, style, description. The browser lists presets from this part alone (`readSerumPresetHeader`), without decompressing anything.
3. A u32 CBOR size, then a u32 encoding, where 2 means zstd.
4. One zstd frame of CBOR: a map of modules. A module's `plainParams` lists only the values that differ from the default, and is the string `"default"` when none do.

Forge decodes the CBOR with its own reader (`src/ForgeCbor.*`) and decompresses with zstd's decompressor only ([Build and test Forge](build-and-test-forge.md)).

An oscillator's table is either embedded in the preset (`embeddedWTData`, the frames end to end, thinned to Forge's 64) or referenced by `relativePathToWT` under Serum's `Tables` folder on this machine. A referenced table that is missing is warned, and the oscillator plays Forge's own table. Either way the converted patch carries its table as a `TABLE` node, not as a path.

## Knob laws and conventions

These were measured by the serum2vital project, which is GPLv3. Its facts were used, not its code; keep it that way.

- **Filter cutoff:** the knob in Hz is `8 × (22050/8)^n`.
- **LFO rate:** stored as `100 × n^4`. In HZ mode that is Hz. When synced, step = `round(14n)` and the cycle is `2^(7 − step)` beats.
- **Envelope times** are in seconds.
- **Table position** is on a fixed 1..256 scale.
- **Warp amounts** run 0..1.
- **Levels and master** are plain gain. Reading them with Serum 1's cubic law put factory masters at −37 dB.
- **Compressor threshold** runs down from 0 dB, so a stored 0 is 0 dB. Read the other way, final limiters silenced whole patches.
- **LFO curve** y values are measured from the top.
- **Effects** are addressed from the matrix as rack × 100 + position (`serumFxTarget`).
- **Filter balance** is set per source: −100 sends it to filter 1 and +100 to filter 2. A filter that is switched off passes audio. Forge has one filter, and it becomes whichever filter the sources reach most.
- **Sends:** a source turned down but sent to an FX bus takes its send as its level, because Serum's sends do not follow the source's level.
- **The FIXED source** (id 38) is a constant, so the importer folds it into the knob instead of routing it.

## Related

- [Forge preset import (Serum 2 and Vital)](forge-preset-import.md)
- [Why a patch copied from Serum sounds different](serum-patches-sound-different.md)
- [Comparing Forge with Serum](comparing-forge-with-serum.md)
