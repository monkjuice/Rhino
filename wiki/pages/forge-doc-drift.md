---
title: Where Forge's docs disagree with its code
type: analysis
summary: Statements in Forge's README, plan and handover notes that the code has overtaken, checked on 2026-10-03.
tags: [forge, docs, maintenance]
sources: []
updated: 2026-10-03
---

# Where Forge's docs disagree with its code

Forge's prose (`instruments/rhino-forge/README.md`, `PLAN.md`, `SPECTRAL.md`, `HANDOVER-M9B.md`) is detailed and
mostly right, but the third oscillator (2026-09-28) and later work left parts of it behind. Paths below are relative
to `instruments/rhino-forge/`. Rhino's list is [Where the written docs disagree with the code](doc-drift.md). Fix the
doc, then delete its row.

## Plans and handover

| Doc | Says | The code (2026-10-03) |
| --- | --- | --- |
| `PLAN.md`, `INSTRUMENT_PLAN.md` | Two oscillators | Three (`oscillatorCount`) |
| `PLAN.md`, `HANDOVER-M9B.md` | Preset format 2 | Format 3; any other version is refused ([Forge presets and state](forge-presets-and-state.md)) |
| `PLAN.md` M6a, M10c | The destination list is appended to, never inserted into | Since format 3 its order is a schema, and changing it bumps the format ([Stored indices are append-only](append-only-stored-indices.md)) |
| `PLAN.md` M9b-1, `HANDOVER-M9B.md` | Eleven octave-spaced band-limited levels, about 2.1x memory, `levelFor(hz * 1.03)` | Three levels per octave (`wavetableLevelsPerOctave`), read at `hz * 1.05 * headroom` ([Forge oscillators and wavetables](forge-oscillators.md)) |
| `PLAN.md` M10c | Eight MIX channels | Nine, with OSC C |
| `PLAN.md` M11a, M11e | Four slots and six types per rack; no reordering by drag | Eight and eight; dragging a row reorders (`Editor::moveFxSlot`) ([Forge mixer and effects racks](forge-mixer-and-fx.md)) |
| `PLAN.md` M8 | No presets in the repository | One, `presets/aLiLBitDark.forgepreset`: format 1, referenced by nothing, refused by this build |
| `PLAN.md`, out of scope | The preset browser is M14 | M14 is the noise module; the status table ends at M14c and M16 lives in `SPECTRAL.md` |
| `INSTRUMENT_PLAN.md` | The DSP is a JUCE-only static library | There is none: `forge_sources` compiles into the plugin and the test binary alike |
| `HANDOVER-M9B.md` | Three CTest cases; CMake only inside Visual Studio | Eighteen areas; CMake is on `PATH` |
| `SPECTRAL.md` | Warp applies to a spectral oscillator's output, with the table-read families left out of the menu | `renderSpectralOscillator` takes no warp stages, so warp does nothing in spectral mode, and the menu is unfiltered ([Forge spectral oscillator](forge-spectral.md)) |

## README

| Says | The code (2026-10-03) |
| --- | --- |
| The editor is one class across nine files; four headers under `ForgeCore.h`, seven under `ForgeVisuals.h` | Fifteen `src/ForgeEditor*.cpp`; five and eight ([Forge editor (panel)](forge-editor.md)) |
| Twenty-six warp modes (in one paragraph) | Thirty-eight, as the same file says elsewhere (`warpModeCount`) |
| Every warp source must be switched on | NOISE is Core's own white generator: it works with the NOISE module off and ignores its SOURCE ([Forge warp](forge-warp.md)) |
| The filter is the one module with controls seated in its display | The oscillators now seat MODE and the spectral loop strip the same way |
| Three places register a test area | Four: the new `.cpp` also goes in `target_sources` ([Build and test Forge](build-and-test-forge.md)) |

## Stale comments in code

- `core/ForgeFx.h` and `src/ForgeParameters.cpp` say an automation lane reads `FX 1.2 KNOB 3`; it reads `MAIN 2 Knob 3`.
  The `legacyFxSlotCount` comment promises stable identities from a constant nothing uses.
- `ui/ForgeModules.h` sends a new parameter to `ForgeProcessor.cpp`; it belongs in `src/ForgeParameters.cpp`.
- `ui/ForgeType.h` credits the font cache with 7 points of a core; the measured saving was about 1, and the 7 was a
  per-knob clip ([Forge's panel repaints whole at 24 Hz](forge-panel-repaint-cost.md)).
- `src/ForgeParameters.cpp` comments slot 1's default destination as the cutoff while the literal `13` now means
  C PAN ([Hazards found while seeding the wiki](known-hazards.md)).

## Related

- [Where the written docs disagree with the code](doc-drift.md)
- [Rhino Forge](forge.md)
- [Hazards found while seeding the wiki](known-hazards.md)
- [Forge presets and state](forge-presets-and-state.md)
