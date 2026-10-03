---
title: Rhino Forge
type: component
summary: Rhino's independent wavetable and spectral synth, a VST3 and standalone app built from one JUCE AudioProcessor.
tags: [forge, synth, overview]
sources: []
updated: 2026-10-03
---

# Rhino Forge

Forge is the polyphonic synth under `instruments/rhino-forge/`. One JUCE `AudioProcessor` (`src/ForgeProcessor.*`) builds both a VST3 and a standalone app. Forge owns no Tracktion or Rhino types, and Rhino hosts it as an external VST3 ([Forge is an independent VST3](forge-is-an-independent-vst3.md)). Its CMake project takes JUCE from Rhino's pinned checkout through `RHINO_JUCE_DIR` (default `native/.deps/juce`), so Rhino's dependencies must be fetched first. It was scaffolded on 2026-09-13, under the project's earlier name, Theta.

## The panel

- **Signal row:** SUB and NOISE share one housing, then OSC A, OSC B, OSC C and FILTER.
- **Lower row:** GLOBAL, ENV (four envelopes, one shown at a time), LFO (six, one shown at a time) and eight MACROS.
- **Keyboard:** 76 keys. The ARP plate takes the place of the bottom octave.
- **Tabs:** OSC, TABLE and MATRIX swap only the oscillator columns. MIX and FX take over the whole signal row. The arp's settings are an overlay (`Page::arp`), not a tab.

Each module has its own page:

- [Forge oscillators and wavetables](forge-oscillators.md)
- [Forge warp](forge-warp.md)
- [Forge spectral oscillator](forge-spectral.md)
- [Forge filter](forge-filter.md)
- [Forge noise module](forge-noise.md)
- [Forge modulation: matrix, envelopes, LFOs and macros](forge-modulation.md)
- [Forge mixer and effects racks](forge-mixer-and-fx.md)
- [Forge arpeggiator](forge-arpeggiator.md)
- [Forge MIDI learn](forge-midi-learn.md)

The panel itself is covered in [Forge editor (panel)](forge-editor.md).

Forge follows Serum 2's manual for structure: layout, routing and drag-to-modulate. It reuses no Serum code, assets, names or presets ([Why a patch copied from Serum sounds different](serum-patches-sound-different.md)).

## Source layout

| Directory | Holds |
| --- | --- |
| `core/` | The engine, header-only ([Forge engine (Core)](forge-engine.md)) |
| `ui/` | The declarative layout and the visuals, all headers |
| `src/` | The translation units for the Processor and the Editor |
| `tests/` | One file per area, 18 areas (`forge_test_areas` in `CMakeLists.txt`) |
| `tables/` | Ten factory wavetables and `make-tables.py`, the script that writes them |
| `presets/` | `aLiLBitDark.forgepreset`, a format 1 file that the current build refuses |

## Docs

- **`README.md`** describes the behaviour of every module, plus the build, tests and file layout.
- **`PLAN.md`** holds milestones M1 to M14c and the settled decisions. Parts are stale: it mostly describes two oscillators (there have been three since 2026-09-28), gives preset format 2 (it is now 3), and still lists spectral oscillators under "Later still".
- **`SPECTRAL.md`** is M16, the spectral oscillator.
- **`HANDOVER-M9B.md`** covers wavetable internals. Its band-limiting figures are stale ([Forge oscillators and wavetables](forge-oscillators.md)).

Every stale statement found on 2026-10-03 is listed in [Where Forge's docs disagree with its code](forge-doc-drift.md).

## Status

As of 2026-10-03:

- **Done:** everything from M1 to M14b except the items below, plus M16a to M16c. M16d is partly done.
- **Not started:**
  - M13b, the twelve arp slots.
  - M13c, the pattern editor.
  - M14c, sample noise sources.
  - The factory preset set planned in M8.
- **Deferred:**
  - M11e, the rest of the effects rack.
  - The preset browser, which PLAN.md also numbers M14.
  - M15, a second filter.
  - MPE.

## Related

- [Overview](../overview.md)
- [Forge presets and state](forge-presets-and-state.md)
- [Build and test Forge](build-and-test-forge.md)
- [Hosting Forge in Rhino](forge-hosting.md)
