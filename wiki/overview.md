---
title: Overview
type: overview
summary: "Rhino and Rhino Forge at a glance: what they are, how they are built and where to start reading."
tags: [overview, rhino, forge]
sources: []
updated: 2026-10-03
---

# Rhino and Rhino Forge

This repository holds two products.

- **Rhino** is a native desktop DAW for electronic music: an arrangement view with a note editor, audio and MIDI
  recording, time warp, track groups, a device chain per track, and a clip launcher that is built but switched off.
  It is C++20 on Tracktion Engine and JUCE, under `native/`. Windows is the development platform; macOS is kept
  portable in the architecture but has never been built. `README.md` calls it "an early composition workflow, not a
  complete DAW".
- **Rhino Forge** is an independent wavetable and spectral synthesiser: one JUCE `AudioProcessor` built as a VST3 and
  a standalone app, under `instruments/rhino-forge/`. Rhino hosts it like any third-party VST3. Serum 2 is its north
  star for structure and workflow; it reuses no Serum code, assets, names or presets.

Both are personal-use projects. Ableton Live is the reference for Rhino's workflow vocabulary, as Serum is for Forge's.

## Repository layout

| Path | What it holds |
| --- | --- |
| `native/src/` | Rhino's model (`Session*.cpp`), UI, app shell (`Main.cpp`) and test runners |
| `native/src/core/`, `native/src/devices/` | The `RhinoCore` (JUCE-only DSP, content library) and `RhinoDevices` (built-in plugins, device catalog) targets |
| `native/assets/` | Fonts and app icons, the only assets compiled in |
| `instruments/rhino-forge/` | Forge: `core/` engine (headers only), `ui/`, `src/`, `tests/`, `tables/` |
| `library/` | Rhino's sample content, stored with Git LFS |
| `research/` | The September 2026 DAW study; `research/sources/` is off limits |
| `native/.deps/` | Pinned JUCE and Tracktion, fetched by script, ignored by git, never read |

## Architecture in three sentences

Rhino's UI talks only to `Session`, a message-thread facade over one Tracktion `Edit`; Tracktion owns playback graphs,
streaming and plugin state, and the UI never schedules audio. The model holds the rules — a track's kind, the order of
a device chain, clips never overlapping — so every UI path inherits them. Forge is a header-only voice engine (`Core`)
behind a processor and a declarative panel, and Rhino finds it by name without ever naming one of its parameters.

See [Session, the model](pages/session-model.md), [Rhino's build targets](pages/rhino-build-targets.md),
[Dependency direction](pages/dependency-direction.md), [Rhino Forge](pages/forge.md) and
[Hosting Forge in Rhino](pages/forge-hosting.md).

## Build, run and test

Rhino: fetch the pinned dependencies, configure, build, run the five CTest cases —
[Build and test Rhino](pages/build-and-test-rhino.md). Forge: configure against Rhino's JUCE checkout and run the
per-area cases — [Build and test Forge](pages/build-and-test-forge.md). Before the first build on this machine, read
[LNK1104 means a running binary holds the file](pages/locked-executable-lnk1104.md) and
[A new source file needs an explicit CMake configure](pages/cmake-does-not-reconfigure.md).

## Where to start reading

- **Rhino's model:** [Track kinds](pages/track-kinds.md), [Clips never overlap](pages/clip-placement.md),
  [Region editing](pages/region-editing.md), [Device catalog](pages/device-catalog.md),
  [Device chain order](pages/device-chain-order.md).
- **Rhino's UI:** [Arrangement view](pages/arrangement-view.md), [App shell and control bar](pages/app-shell.md),
  [Note editor (StepGrid)](pages/note-editor.md).
- **Forge:** [Forge engine (Core)](pages/forge-engine.md), [Forge editor (panel)](pages/forge-editor.md),
  [Forge presets and state](pages/forge-presets-and-state.md).
- **Before changing anything:** [Keeping files small](pages/keeping-files-small.md),
  [Real-time audio rules](pages/real-time-audio-rules.md), [Git workflow](pages/git-workflow.md),
  [Directories not to read](pages/off-limits-directories.md).
- **The repository's own docs:** `AGENTS.md` is the most current statement of the rules and wins over older prose;
  `README.md` lists features and shortcuts; `native/README.md` holds the implementation contracts; `HANDOVER.md`
  maps the September 2026 file split; Forge's `README.md`, `PLAN.md` and `SPECTRAL.md` hold its design. Some of
  that prose has fallen behind the code — see
  [Where the written docs disagree with the code](pages/doc-drift.md).

The full catalog is [index.md](index.md).

## History

| Date | Event |
| --- | --- |
| 2026-09-08 | Research study and first native evaluation, under the name Theda, then Theta |
| 2026-09-12 | `Session.cpp`, `StepGrid.cpp` and `Arrangement.cpp` split into focused files (`HANDOVER.md`) |
| 2026-09-13 | Forge scaffolded as an independent VST3 and hosted through Tracktion |
| 2026-09-14 | Session view (clip launcher) built, then paused behind a build switch |
| 2026-09-16 | Renamed Rhino; on 2026-09-18 the binary became `RhinoDAW` to escape a driver profile |
| 2026-09-19/20 | Device catalog and the `RhinoDevices` library; Forge split into ~40 files |
| 2026-09-21 | Rhino Tune, Rhino EQ and Rhino Vocoder; Forge's 34 filter types |
| 2026-09-28 | Forge's third oscillator (preset format 3) |
| 2026-09-30 – 10-03 | Time warp; track kind fixed at creation; per-track monitoring; device reordering; Forge's spectral oscillator |
