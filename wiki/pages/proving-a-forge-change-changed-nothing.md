---
title: Proving a Forge change changed nothing
type: guide
summary: Hash --render audio and --snapshot PNGs against a worktree build of the old revision, and time intended changes by --profile medians.
tags: [forge, testing, refactoring, performance]
sources: []
updated: 2026-10-03
---

# Proving a Forge change changed nothing

A passing suite says the checks still hold, not that nothing moved. For a change meant to preserve behaviour — a
refactor, a file split, a faster path — Forge's test binary writes deterministic artefacts that can be compared byte for
byte with the previous revision's. That is how the September 2026 file split was shown to be free: identical audio
bytes and identical PNGs of all five tabs ([Forge's engine splits into headers only](forge-engine-headers-only.md)).

## The artefacts

- `--render out.raw [blocks]` plays a deliberately unmusical patch built to touch as much of the voice as one render
  can (`everythingPatch` in `tests/ForgeTestRender.cpp`): three detuned unison oscillators with warp, sub, noise, a
  driven resonant filter, four modulation slots and two effects, playing a five-note chord with one note released
  halfway so the release path and the voice tail are in it. It writes raw interleaved stereo floats, 512-sample blocks
  at 48 kHz, 60 blocks unless told otherwise.
- `--snapshot out.png width height PAGE` renders the real editor on one tab
  ([Seeing the UI without taking the screen](headless-ui-snapshots.md)).

## Steps

1. Build `RhinoForgeTests` from the working tree ([Build and test Forge](build-and-test-forge.md)).
2. Check the previous revision out beside it, without touching the working tree:
   ```powershell
   git worktree add --detach <scratch>\baseline HEAD~1
   cmake -S <scratch>\baseline\instruments\rhino-forge -B <scratch>\baseline-build -G "Visual Studio 17 2022" -A x64 -DRHINO_JUCE_DIR=<repo>\native\.deps\juce
   ```
   `RHINO_JUCE_DIR` is required: `native/.deps` is gitignored, so the worktree has no JUCE of its own. Build its
   `RhinoForgeTests` in the background; from scratch it takes minutes.
3. From one scratch directory (both tools resolve paths against it), run `--render` and a `--snapshot` per tab with
   each binary, then compare hashes (`Get-FileHash`). Any differing byte is a finding, not noise.
4. Remove the worktree with `git worktree remove` when done.

## When the change is meant to change something

For speed, `--profile [width height]` paints the editor in software and prints the median cost of an idle frame, a
resize frame, a module switched on and off, a walk through the tabs and a flip between two. Take medians, never means:
one cache rebuild among cached frames is a 30 ms outlier that a mean reports as a regression. Keep the old binary aside
(a worktree serves; stashing risks sweeping up someone else's uncommitted edits) and run the two alternately for three
rounds, because the machine drifts by more than some of the differences being measured. For idle CPU, launch the
standalone and sample its `TotalProcessorTime` twice about eight seconds apart, divided by the wall time. Where frame
time goes, and the caches that absorb it, is in [Forge's panel repaints whole at 24 Hz](forge-panel-repaint-cost.md).

Caching gets its own guard: `chromeCacheSuite` in `tests/ForgeTestsDisplays.cpp` (area `displays`) renders a page on
an editor walked around the panel and requires it to match a fresh editor's render of the same page.

## Related

- [Build and test Forge](build-and-test-forge.md)
- [Forge's engine splits into headers only](forge-engine-headers-only.md)
- [Forge's panel repaints whole at 24 Hz](forge-panel-repaint-cost.md)
- [Seeing the UI without taking the screen](headless-ui-snapshots.md)
- [Forge engine (Core)](forge-engine.md)
