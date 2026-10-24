---
title: Rhino Forge
type: component
summary: Rhino's independent wavetable and spectral synth, a VST3 and standalone app built from one JUCE AudioProcessor.
tags: [forge, synth, overview]
sources: []
updated: 2026-10-23
---

# Rhino Forge

Forge is the polyphonic synth under `instruments/rhino-forge/`. One JUCE `AudioProcessor` (`src/ForgeProcessor.*`) builds both a VST3 and a standalone app. Forge owns no Tracktion or Rhino types, and Rhino hosts it as an external VST3 ([Forge is an independent VST3](forge-is-an-independent-vst3.md)). Its CMake project takes JUCE from Rhino's pinned checkout through `RHINO_JUCE_DIR` (default `native/.deps/juce`), so Rhino's dependencies must be fetched first. It was scaffolded on 2026-09-13, under the project's earlier name, Theta.

## The panel

- **Signal row:** SUB and NOISE share one housing, then OSC A, OSC B, OSC C and FILTER (two filters, one shown at a time).
- **Lower row:** GLOBAL, ENV (four envelopes, one shown at a time), LFO (six, one shown at a time) and eight MACROS.
- **Keyboard:** A1 to C7 (`keyboardLowestNote` 33 to `keyboardHighestNote` 96, 38 white keys), starting against the wheels. The ARP plate stands between the keys and the note guide since 2026-10-13 ([Forge ARP plate, slots and waterfall](forge-arp-plate.md)).
- **Tabs:** OSC, TABLE and MATRIX swap only the oscillator columns. MIX and FX take over the whole signal row. The arp's settings are an overlay (`Page::arp`), not a tab.
- **Themes:** Forge's gunmetal or Merli's fur, over the same layout, chosen per machine from a menu on the title plate (or a right-click on bare chassis) and applied at the next start ([Forge themes and Merli](forge-themes.md)).
- **Presets:** the title plate names the loaded preset. LOAD and SAVE open a preset browser over the modules that lists the player's bank, factory set included, beside the machine's Serum 2 and Vital libraries ([Forge preset browser and bank](forge-preset-browser.md)). Serum 2 and Vital presets are converted as they open ([Forge preset import](forge-preset-import.md)).

Each module has its own page:

- [Forge oscillators and wavetables](forge-oscillators.md)
- [Forge warp](forge-warp.md)
- [Forge spectral oscillator](forge-spectral.md)
- [Forge filter](forge-filter.md)
- [Forge noise module](forge-noise.md)
- [Forge modulation: matrix, envelopes, LFOs and macros](forge-modulation.md)
- [Forge mixer and effects racks](forge-mixer-and-fx.md)
- [Forge arpeggiator](forge-arpeggiator.md) and its [ARP plate, slots and waterfall](forge-arp-plate.md)
- [Forge MIDI learn](forge-midi-learn.md)
- [Forge preset browser and bank](forge-preset-browser.md) and [Forge preset import (Serum 2 and Vital)](forge-preset-import.md)

The panel itself is covered in [Forge editor (panel)](forge-editor.md).

Forge follows Serum 2's manual for structure: layout, routing and drag-to-modulate. It reuses no Serum code, assets, names or presets ([Why a patch copied from Serum sounds different](serum-patches-sound-different.md)). It ships none of Serum's presets either: the importer only converts the ones a user already has on the machine.

## Source layout

| Directory | Holds |
| --- | --- |
| `core/` | The engine, header-only ([Forge engine (Core)](forge-engine.md)) |
| `ui/` | The declarative layout and the visuals, all headers |
| `src/` | The translation units for the Processor and the Editor |
| `tests/` | One file per area, 22 areas (`forge_test_areas` in `CMakeLists.txt`) |
| `tables/` | Ten factory wavetables, `make-tables.py` that writes them, and the git-ignored `user/` where the panel's NEW and SAVE write ([Forge oscillators and wavetables](forge-oscillators.md)) |
| `presets/` | `Factory/`, the 29 factory presets (18 at its root, eleven in `Factory/Drum n Bass/`), and `make-presets.py`, which writes them ([Forge presets and state](forge-presets-and-state.md), [Writing Forge factory presets](writing-forge-factory-presets.md)); also `aLiLBitDark.forgepreset`, a format 1 file that the current build refuses |

## Documentation

Forge's `README.md` is the newcomer-facing product and setup page. The component pages linked above, plus the build,
state and hosting guides, are the canonical engineering documentation ([Documentation ownership](documentation-ownership.md)).

## Status

As of 2026-10-23:

- **Done:** everything from M1 to M14b except the items below, plus M16a to M16c. M16d is partly done. M13b is partly done: four arp slots arrived on 2026-10-13 as the ARP plate's buttons (commit `8f28dc7`); EDIT ALL has not.
- **Done on 2026-10-18/19:** the preset browser and bank, the factory set planned in M8, and import of Serum 2 and Vital presets. A converted Serum preset's absolute level is not yet calibrated against Serum ([Forge preset import](forge-preset-import.md)).
- **Done on 2026-10-22:** M15, a second filter, in parallel or in series with the first ([Forge filter](forge-filter.md)). The importers do not fill it yet.
- **Done on 2026-10-23:** a Drum n Bass folder in the factory set, eleven growls, leads and subs, 29 presets in all ([Writing Forge factory presets](writing-forge-factory-presets.md)).
- **Not started:**
  - M13c, the pattern editor.
  - M14c, sample noise sources.
- **Deferred:**
  - M11e, the rest of the effects rack.
  - MPE.

## The unload freeze, fixed 2026-10-22

Until commit `f2a972e` Forge could freeze its host when its DLL unloaded after its editor had painted: four static Direct2D images in `ui/` were released under the loader lock, where NVIDIA's driver deadlocks. Found on 2026-10-12; the images now live in JUCE's `ImageCache`, and no `juce::Image` in Forge may have static storage ([A plugin's static Direct2D images deadlock NVIDIA's driver at unload](plugin-static-images-deadlock-on-unload.md)).

## Related

- [Overview](../overview.md)
- [Forge presets and state](forge-presets-and-state.md)
- [Build and test Forge](build-and-test-forge.md)
- [Hosting Forge in Rhino](forge-hosting.md)
- [A plugin's static Direct2D images deadlock NVIDIA's driver at unload](plugin-static-images-deadlock-on-unload.md)
