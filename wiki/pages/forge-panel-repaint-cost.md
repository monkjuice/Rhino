---
title: Forge's panel repaints whole at 24 Hz
type: gotcha
summary: Forge's editor repaints the entire panel 24 times a second, so anything added to its paint path is paid on every frame.
tags: [forge, ui, performance]
sources: []
updated: 2026-10-22
---

# Forge's panel repaints whole at 24 Hz

`Editor::timerCallback` (`src/ForgeEditorInput.cpp`) ends with an unconditional `repaint()`, on a timer started with `startTimerHz(24)` in `src/ForgeEditor.cpp`. Every knob, button and label is redrawn 24 times a second whether it moved or not, so anything added to `Editor::paint` (`src/ForgeEditorPaint.cpp`) or to a LookAndFeel draw method is on the frame path. Frame pacing is a core requirement (`AGENTS.md`), and on 2026-09-19 a restyle of the metal took idle CPU from 31.8% to 84% of one core before anyone noticed.

## Measuring it

- `RhinoForgeTests --profile [width height [theme [page [preset]]]]` (`tests/ForgeTestTools.cpp`) builds the real editor headless and prints median frame times: idle, resize, module on/off, a tab walk and a tab flip. The idle and resize frames are the oscillators' unless a page is named (`FX` for the effects rack), and a preset can be loaded first, since an empty rack draws nothing; `-` skips the theme or the page. It times `Editor::paint` alone, so the strips' pictures, panels and readings are counted and the knobs, which are child components, are not. Its comment says it renders in software, but it paints into a plain `juce::Image`, which on Windows is a Direct2D bitmap with a fixed cost per paint ([Profile paint on a software image](profile-paint-on-a-software-image.md)). Compare two builds rather than reading the absolute numbers.
- Take medians, never means. One 30 ms rebuild among forty cached frames once read as a 1.8 ms idle regression.
- Build the old binary aside and alternate runs: the machine drifts by more than some differences. For idle CPU, sample the standalone's `TotalProcessorTime` twice, about 8 s apart.

## Where the cost is

Path stroking, not rasterisation: in a metal piece the gradient `strokePath` calls dominate and the grain is about 3%. So clipping a repaint to its dirty region bought only about 10% (written, measured, thrown away), and halving the raster scale saves about 40%, not 75%. If those figures came from `--profile`, they were taken on a plain image whose fixed overhead can hide a gain; re-measure on a software image before relying on them.

## The cached layers

- `chassisLayer`: the backdrop alone, keyed on size and raster scale (`Editor::chassisKey`), about 30 ms of a 50 ms full rebuild. Since 2026-10-13 it also carries the ARP plate's housing, well, engraved rule and star sky (`drawArpPlateHousing`, `ui/ForgePanels.h`): drawn live, the plate cost about 3 ms a frame, `drawMetalPiece` and `drawEdgeWear` being the expensive primitives.
- `chrome`: chassis plus module plates, keyed by `Editor::chromeKey` (page, FX view state, each module's shown/on state, oscillator colours, and whether each oscillator is spectral). **Anything drawn in `paintPlates` needs its state in `chromeKey`**, or a stale layer stays on screen. That is why `drawModuleDetail`, whose envelope stage and LFO rate change per frame, is drawn live. The oscillators' picture tubes, glass and bezel only, moved into this layer in commit `abdd73d`; only the trace on them stays live. A spectral oscillator trades its tube for a flat well, so the mode joined the key. The idle frame at 1440×900 fell from 9.2-9.6 ms to 8.0-8.2 ms. Other cached pictures follow the same rule: the spectrogram is keyed on its oscillator's colour as well as its sample ([Forge spectral oscillator](forge-spectral.md)).
- `previousChrome`: the last tab's layer, so flipping between two tabs is a swap (60 to 11 ms).
- `fxPictures`: each FX rack picture, kept by `Editor::paintFxWell` (`src/ForgeEditorFxView.cpp`) and keyed by `ui::FxPictureKey` (`ui/ForgeFxDisplay.h`) on the well's size, the slot's type, modes and knobs, the sample rate, the tempo, the bypass and the raster scale. **Anything a picture is drawn from must be in that key**, or a stale picture stays on screen. Drawn live, four strips of pictures cost 4.6 ms of a 13.7 ms idle FX frame at 1440x900. Kept, the FX page measured 9.6 ms idle and 65.9 ms resize, against 9.9 and 65.8 ms before the per-type strips (2026-10-21, alternating runs, the `all-fx` preset). What moves with the sound is drawn over the kept copy each frame (`drawFxWellLive`): the compressor's needle and curve dot, and the chorus playhead. The phaser's and flanger's combs move whole, so they are keyed on their LFO phase in steps of 1/512 of a turn and redrawn every frame while they play. A well whose size changed since the last frame is drawn straight through, so a window drag does not allocate an image a frame. The paste sets the opacity to 1 first: until commit `44e19bb` it took the last translucent colour's, and every kept picture showed faded ([A JUCE image is drawn at the last colour's opacity](juce-image-draws-at-current-opacity.md)). A test in `forge_fx` moves a knob and reads the picture's pixels, found by where the pointer turns to a crosshair, and holds the kept frame to a drawn one. After `44e19bb` the FX page measured 9.0 ms idle and 62.9 ms resize (`--profile 1440 900 - FX all-fx`). The strips are taller by then, so three show at that size instead of four, and the figures do not compare like for like with the ones above.

`chromeCacheSuite` (`tests/ForgeTestsDisplays.cpp`, area `displays`) checks a page reached by walking the tabs against a fresh editor's, and switches oscillator A to spectral on an editor that has already drawn the page. To prove it still bites, take both the page and the per-module character out of `chromeKey`; either alone is covered by the other.

A half-resolution layer during window drags was added and removed the same day (commit `226d892`) because headings went soft. `resizeSharpnessSuite` now fails if a resize frame differs from a fresh editor at that size.

## The ARP plate's live pass

`drawArpPlateState` (`ui/ForgeArpPlate.h`) draws what moves on the plate - the lamp, the lit outline, four buttons and up to 48 falling sparks - as fills of small shapes; the only stroked path is the open state's outline. A spark is drawn from its age alone, so nothing is stepped per frame ([Forge ARP plate, slots and waterfall](forge-arp-plate.md)). `--profile` at 1440x900 after the change read 8.6 ms median idle (2026-10-13) against the 8.0-8.2 ms recorded after `abdd73d`, with no same-session baseline, so the difference is unattributed.

## Text

Custom faces rasterise where the platform face was cached. `setBufferedToImage(true)` on the knob labels saved 4.5 points of a core, caching each `juce::Font` per face and size (`ui/ForgeType.h`) about one more, and a clipped highlight pushed per knob per frame had cost 7 before it became a fitted ellipse (commits `8ff50f7` and `1ef629f`, 2026-09-19 and 20).

## Related

- [Forge editor (panel)](forge-editor.md)
- [Proving a Forge change changed nothing](proving-a-forge-change-changed-nothing.md)
- [One knob diameter for the whole Forge panel](forge-knob-diameter-is-panel-wide.md)
- [Colours and typography](colours-and-typography.md)
- [A JUCE image is drawn at the last colour's opacity](juce-image-draws-at-current-opacity.md)
