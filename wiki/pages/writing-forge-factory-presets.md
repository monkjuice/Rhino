---
title: Writing Forge factory presets
type: guide
summary: Add a Forge factory preset by editing presets/make-presets.py, never a file; the script's vocabulary, why every new preset writes subPan, how the Drum n Bass growls are built to sit over the folder's subs, the filter and LFO facts behind them, and the audition-and-measure check each one passed.
tags: [forge, presets, sound-design, measurement]
sources: []
updated: 2026-10-23
---

# Writing Forge factory presets

Paths are under `instruments/rhino-forge/`. The factory set is 29 `.forgepreset` files, all written by `presets/make-presets.py`: the 18 originals of 2026-10-19 at `presets/Factory/` and, since 2026-10-23, eleven in `presets/Factory/Drum n Bass/` from `write_drum_and_bass()`. Edit the script and run it; it rewrites every file, and the 18 came out byte-identical when the folder was added. The bank copies `Factory/` with its subfolders, so the folder shows as `Factory/Drum n Bass` with no engine or browser change ([Forge preset browser and bank](forge-preset-browser.md)).

## The script's vocabulary

- A preset is only the `PARAM`s that differ from Forge's defaults, as plain values (Hz, seconds, semitones, linear gain, a choice as its index). `preset(name, style, notes, params, routes, fx, macros, folder)` starts from `BASE`, which switches off what a fresh Forge demos; `folder=` writes into `Factory/<folder>` ([Forge presets and state](forge-presets-and-state.md)).
- `WARP`, `FILTER` and `FX` list every mode in `core/ForgeWarp.h`, `core/ForgeFilter.h` and `core/ForgeFx.h`, and `destination()` numbers every matrix destination in `core/ForgeMatrix.h`'s order: the named controls, each rack slot's knobs and mix (`fx<rack>s<slot>...`, MAIN being rack 1), the appended controls, the FX LEVEL bank, then FILTER 2's. The lists are append-only, so a number written here keeps its meaning ([Stored indices are append-only](append-only-stored-indices.md)).
- `lfo(n, shape, rate, division, mode, kind, direction, smooth)`: `kind` is the TYPE (`normal`, `path`, `sh`); it, `direction` and `smooth` are written only when they leave the default ([Forge LFOs](forge-lfos.md)).
- **Merge `MIXED`** (`subPan` and `noisePan` at 0) into every new preset. A state naming neither is taken by `Processor::migrated` for one saved before the mixer and has its sub and noise raised 3 dB. The 18 originals lack it, deliberately ([A Forge preset with no subPan loads its sub 3 dB hotter](forge-presets-without-subpan-load-hotter.md)).

## What shaped the Drum n Bass folder

Four growls (Talking, Yoi, Tearout, Robot; style Bass), three leads (Neuro, Hoover, Liquid; Lead) and four subs (Clean, Warm, 808, Reese; Bass).

- **A growl is a mid-range layer** meant to sit over a sub from the same folder. Each keeps a small sub of its own (`subLevel` 0.1-0.15, `routeSub` 0 so it skips the filter), mostly as the modulator of a `pd_sub` or `rm_sub` warp. Tearout Growl has 4.5 % of its energy below 120 Hz by design, and its notes say so.
- **The MAIN rack runs on the whole voice sum, sub included**, so the growls' distortion uses the POST LP filter mode with a mix below 1, and `COMP_GLUE`, a compressor with automatic make-up (5:1 from -27 dB), holds them to a -11 dBFS ceiling. The formant and acid filters pass a narrow band, so those patches run oscillator, filter drive and output hot.
- **FORMANT reads CUTOFF as a vowel**: its position on a log scale from 30 Hz to 18 kHz, times 4 (`filterVowelPosition`, `core/ForgeFilter.h`), so A is 30 Hz, E about 148 Hz, I about 735 Hz, O about 3.6 kHz, U 18 kHz. The matrix moves the cutoff in the parameter's range instead, skew 0.25 (`src/ForgeParameters.cpp`): normalised 0.25 is about 100 Hz, 0.45 about 770 Hz, 0.65 about 3.2 kHz, so one depth is a whole vowel high up and a fraction of one low down ([Forge filter](forge-filter.md)).
- **An LFO puts out -1..1**, so a depth `d` swings its destination ±`d` around the knob in the destination's normalised range (`Core::applyModulation`; [Forge modulation](forge-modulation.md)).
- **The dubstep "yoi"** is a SAW read in REVERSE at a 1/4 division on the acid filter's cutoff (Yoi Growl): the spectral centroid jumps high on each beat and falls over about 150 ms.

## Verify by measuring

Each preset was rendered with `RhinoForgeTests.exe --audition <preset> <out.wav>`, C1 for 0.6 s then a C major chord ([Build and test Forge](build-and-test-forge.md)), and measured in numpy: peak and RMS, the bass note's loudest partial and share of energy below 60 and 120 Hz, and the spectral centroid per 43 ms frame ([Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)). On 2026-10-23 the four subs put 93-100 % of the C1 note below 120 Hz at peaks of -9 to -14 dBFS; the growls peak at -11 dBFS with RMS -25 to -28 dBFS during notes, and their centroids follow the LFOs (Talking Growl's chord 550-1657 Hz, Yoi Growl's bass note 113-1145 Hz). The original factory basses audition at peaks of -12 to -18 dBFS. CTest areas `forge_presets`, `forge_import` and `forge_browser` pass, and `--snapshot browser.png 1440 900 PRESETS` shows the folder under Factory.

## Related

- [Forge presets and state](forge-presets-and-state.md)
- [Forge preset browser and bank](forge-preset-browser.md)
- [A Forge preset with no subPan loads its sub 3 dB hotter](forge-presets-without-subpan-load-hotter.md)
- [Forge filter](forge-filter.md)
- [Forge LFOs](forge-lfos.md)
- [Forge modulation: matrix, envelopes, LFOs and macros](forge-modulation.md)
- [Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)
- [Build and test Forge](build-and-test-forge.md)
