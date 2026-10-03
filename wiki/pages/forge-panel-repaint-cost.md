---
title: Forge's panel repaints whole at 24 Hz
type: gotcha
summary: Forge's editor repaints the entire panel 24 times a second, so anything added to its paint path is paid on every frame.
tags: [forge, ui, performance]
sources: []
updated: 2026-10-03
---

# Forge's panel repaints whole at 24 Hz

`Editor::timerCallback` (`src/ForgeEditorInput.cpp`) ends with an unconditional `repaint()`, on a timer started with `startTimerHz(24)` in `src/ForgeEditor.cpp`. Every knob, button and label is redrawn 24 times a second whether it moved or not, so anything added to `Editor::paint` (`src/ForgeEditorPaint.cpp`) or to a LookAndFeel draw method is on the frame path. Frame pacing is a core requirement (`AGENTS.md`), and on 2026-09-19 a restyle of the metal took idle CPU from 31.8% to 84% of one core before anyone noticed.

## Measuring it

- `RhinoForgeTests --profile [width height]` (`tests/ForgeTestTools.cpp`) builds the real editor headless and prints median frame times: idle, resize, module on/off, a tab walk and a tab flip. It renders in software, so compare two builds rather than reading the absolute numbers.
- Take medians, never means. One 30 ms rebuild among forty cached frames once read as a 1.8 ms idle regression.
- Build the old binary aside and alternate runs: the machine drifts by more than some differences. For idle CPU, sample the standalone's `TotalProcessorTime` twice, about 8 s apart.

## Where the cost is

Path stroking, not rasterisation: in a metal piece the gradient `strokePath` calls dominate and the grain is about 3%. So clipping a repaint to its dirty region bought only about 10% (written, measured, thrown away), and halving the raster scale saves about 40%, not 75%.

## The cached layers

- `chassisLayer`: the backdrop alone, keyed on size and raster scale (`Editor::chassisKey`), about 30 ms of a 50 ms full rebuild.
- `chrome`: chassis plus module plates, keyed by `Editor::chromeKey` (page, FX view state, each module's shown/on state, oscillator colours). **Anything drawn in `paintPlates` needs its state in `chromeKey`**, or a stale layer stays on screen. That is why `drawModuleDetail`, whose envelope stage and LFO rate change per frame, is drawn live.
- `previousChrome`: the last tab's layer, so flipping between two tabs is a swap (60 to 11 ms).

`chromeCacheSuite` (`tests/ForgeTestsDisplays.cpp`, area `displays`) checks a page reached by walking the tabs against a fresh editor's. To prove it still bites, take both the page and the per-module character out of `chromeKey`; either alone is covered by the other.

A half-resolution layer during window drags was added and removed the same day (commit `226d892`) because headings went soft. `resizeSharpnessSuite` now fails if a resize frame differs from a fresh editor at that size.

## Text

Custom faces rasterise where the platform face was cached. `setBufferedToImage(true)` on the knob labels saved 4.5 points of a core, caching each `juce::Font` per face and size (`ui/ForgeType.h`) about one more, and a clipped highlight pushed per knob per frame had cost 7 before it became a fitted ellipse (commits `8ff50f7` and `1ef629f`, 2026-09-19 and 20).

## Related

- [Forge editor (panel)](forge-editor.md)
- [Proving a Forge change changed nothing](proving-a-forge-change-changed-nothing.md)
- [One knob diameter for the whole Forge panel](forge-knob-diameter-is-panel-wide.md)
- [Colours and typography](colours-and-typography.md)
