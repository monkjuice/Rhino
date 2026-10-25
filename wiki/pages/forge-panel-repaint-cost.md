---
title: Forge's tick repaints its live regions at 24 Hz
type: gotcha
summary: Since 2026-10-25 Forge's editor tick repaints only its live regions - the ARP plate, each shown module's header band and display, the matrix and the rack, the table panel - and every control repaints itself when it changes; until then it repainted the whole panel every frame. What is drawn in a live region is still paid 24 times a second, and what moves on its own outside them is never redrawn.
tags: [forge, ui, performance]
sources: []
updated: 2026-10-25
---

# Forge's tick repaints its live regions at 24 Hz

`Editor::timerCallback` (`src/ForgeEditorInput.cpp`) runs on a timer started with `startTimerHz(24)` in `src/ForgeEditor.cpp`. Until 2026-10-25 it ended with an unconditional `repaint()`: every knob, button and label was redrawn 24 times a second whether it moved or not, about half of every frame, and on 2026-09-19 a restyle of the metal took idle CPU from 31.8% to 84% of one core before anyone noticed. Frame pacing is a core requirement (`AGENTS.md`).

Since commit `ad945e1` (2026-10-25) it ends with `repaintLive()`:

- It repaints `Editor::liveRegions()` (public, `src/ForgeEditorPaint.cpp`): what `paint()` draws over the cached chassis that moves without a gesture. That is the ARP plate, every shown module's header band (`ui::liveHeaderHeight`, 40 px in `ui/ForgeModule.h`, covering the second line GLOBAL and MACROS print on) and its `displayBounds`, the matrix and the rack modules whole, and the table panel when visible. The drag line a source handle trails is not there, because a drag repaints the panel on every move of its own.
- When `Editor::liveKey()`, which is `chromeKey(1.0f)`, differs from `liveKeyShown` (a module switched off by a preset, a colour from the host), it repaints the whole panel once instead.
- Everything else repaints itself when it changes: a knob on a value or ring change, a label on `setText`, the keyboard on its own timer.

Two rules follow. Anything new that moves on its own goes in `liveRegions()` or repaints itself, or it freezes on screen. And anything drawn inside a live region, in `Editor::paint` or a LookAndFeel method, is still on the frame path. `liveRegionsSuite` (`tests/ForgeTestsDisplays.cpp`, `forge_displays`) holds the first: it paints the panel whole, advances the voices with a chord held and the timer pumped, paints whole again and requires no pixel outside `liveRegions()` to have changed, both frames painted whole because the rasteriser is not clip-invariant ([JUCE's rasteriser is not clip-invariant](juce-rasteriser-not-clip-invariant.md)).

Measured before the change, 2026-10-25, software-rendered at 1440x900: the editor's own paint 8.7 ms, the whole panel with its children 18.2 ms, and 18.7 ms with a chord held, so notes cost nothing extra on the frame. The user reported knobs while playing as smooth and only the preset browser as slow, which is the work this came with ([Forge preset browser and bank](forge-preset-browser.md#what-it-costs)). After the change, same tool, same machine, same day: idle frame 8.3 ms, whole panel 17.8 ms, the tick's live regions 10.3 ms, whole panel with a chord held 18.4 ms, resize frame 65 ms, module on/off 36 ms, tab walk 35 ms, tab flip 13.7 ms. So the tick now costs 10.3 ms of what was a 17.8 ms whole-panel frame, and the whole-panel figures themselves barely moved, as expected: what changed is how much of the panel a tick invalidates, not what a paint of it costs.

## Measuring it

- `RhinoForgeTests --profile [width height [theme [page [preset]]]]` (`tests/ForgeTestTools.cpp`) builds the real editor headless and prints median frame times: idle, resize, module on/off, a tab walk and a tab flip. The idle and resize frames are the oscillators' unless a page is named (`FX` for the effects rack), and a preset can be loaded first, since an empty rack draws nothing; `-` skips the theme or the page. Those lines time `Editor::paint` alone, so the strips' pictures, panels and readings are counted and the knobs, which are child components, are not. Since 2026-10-25 three more lines paint through `paintEntireComponent`, children included: `whole panel`, `whole panel, chord held` (eight notes sounding, the timer pumped by hand) and `tick, live regions` (the same paint clipped to `liveRegions()`, what the tick now repaints). Its comment says it renders in software, but it paints into a plain `juce::Image`, which on Windows is a Direct2D bitmap with a fixed cost per paint ([Profile paint on a software image](profile-paint-on-a-software-image.md)). Compare two builds rather than reading the absolute numbers.
- Take medians, never means. One 30 ms rebuild among forty cached frames once read as a 1.8 ms idle regression.
- Build the old binary aside and alternate runs: the machine drifts by more than some differences. For idle CPU, sample the standalone's `TotalProcessorTime` twice, about 8 s apart.

## Where the cost is

Path stroking, not rasterisation: in a metal piece the gradient `strokePath` calls dominate and the grain is about 3%. So clipping a repaint to its dirty region bought only about 10% (written, measured, thrown away), and halving the raster scale saves about 40%, not 75%. The live-region tick is the other lever: it invalidates less, so the children outside the regions are not painted at all, rather than clipping the same paint. If those figures came from `--profile`, they were taken on a plain image whose fixed overhead can hide a gain; re-measure on a software image before relying on them.

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
