---
title: Forge editor (panel)
type: component
summary: One Editor class across fifteen files builds the panel from declared modules and decides what is visible in one place.
tags: [forge, ui, layout]
sources: []
updated: 2026-10-03
---

# Forge editor (panel)

The panel is one `Editor` (`src/ForgeEditor.h`) defined across fifteen `.cpp` files, `src/ForgeEditor.cpp` plus `ForgeEditor{Envelope,Filter,Fx,FxView,Input,Layout,Lfo,Modulation,Noise,OscMode,Pages,Paint,Spectral,Warp}.cpp`, sharing the private `src/ForgeEditorInternal.h`. The `README.md` layout table still says nine (checked 2026-10-03). This is the split Rhino uses ([Keeping files small](keeping-files-small.md)); unlike the engine, the panel may span translation units freely ([Forge's engine splits into headers only](forge-engine-headers-only.md)).

## Declared, not computed

`ui/ForgeLayout.h` gathers four headers: `ForgeModule.h` (what a `Control`, `Row` and `Module` are), `ForgeModules.h` (every module, declared once), `ForgePlacement.h` (window limits, where a module lands on the 24-column grid) and `ForgeControlBlock.h` (where a control lands in its row). The editor walks the declarations; no call site computes a cell. The vocabulary covers the odd cases: `banks` (four envelopes, six LFOs, three racks, one bank shown at a time), `sharesCell` (two readings of one setting, such as an LFO rate in HZ or BPM), `enabledBy`/`disabledBy`/`modeBy`, `displayWeight` for the rack's per-slot strips, and `Seat` for a row placed inside a display or header.

`Module` is an aggregate initialised positionally, so its optional trailing fields (`pages` through `group`) are reached by counting. Adding or removing one means revisiting every initialiser in `ui/ForgeModules.h` that reaches past it; when `plateName` went (commit `7be9a56`), the six arp panes were the ones easy to miss.

**Adding a control** is its parameter in `src/ForgeParameters.cpp` plus a line in `ui/ForgeModules.h`. The layout test (`tests/ForgeTestsLayout.cpp`) fails if the two disagree in either direction, if a control or enable lacks a real tooltip in `ui/ForgeTooltips.h`, or if a module escapes or overlaps at any allowed size, swept in 20 px steps (about 800 sizes between 1180×820 and 2000×1180). A parameter set by dragging on a display rather than from a cell must be listed in `displayParameters` (`ui/ForgeModules.h`) for that test to find it. As of `dbd223d` the only ones are the spectral loop markers ([Forge spectral oscillator](forge-spectral.md)).

## Pages

OSC, TABLE and MATRIX swap only the oscillator columns; MIX and FX take the whole signal row. The arp overlay is a sixth page, `Page::arp`, that is never a tab ([Forge arpeggiator](forge-arpeggiator.md)). Switching hides and shows components instead of rebuilding them, so a hidden knob stays attached, automated and modulated.

## Rules that bite

- **Visibility is decided only in `Editor::applyEnableStates`.** It runs every 24 Hz tick, so anything hidden or greyed elsewhere is put back next frame; the first FX rack failed exactly so.
- **A switched-off module greys everything and hides nothing.** `applyEnableStates` keeps two flags: `inCharge` (`disabledBy`/`enabledBy`, the LFO custom-table Shape rule, `fxKnobLive`) and `on = module.on() && inCharge`. Greying reads `on`; hiding a control in a shared cell (`ui::inSharedCell`) reads only `inCharge`, while `modeBy`/`modeIs` hides the other mode's set. When the hide used `on`, switching an oscillator off emptied the POSITION/SCAN, DETUNE/CUT and BLEND/MIX cells (commit `9b1790a`); the LFO's HZ/BPM rate pair had the same latent fault. `switchedOffControlsStaySuite` in `tests/ForgeTestsLayout.cpp` (CTest `forge_layout`) guards it. When a test counts visible labels, count ones unique to the module (POSITION, DETUNE, BLEND, UNISON, SCAN, CUT): PAN, LEVEL and MIX also label the sub, noise and filter modules on the OSC page.
- **Read values through `Editor::value`**, which asks the parameter rather than the cached atomic beside it. The atomic is updated by another listener, and a panel attachment called first reads the old value: a mode field looked right only after an unrelated click (`PLAN.md` M11d).
- **Copy, do not point.** `FxSelector` copies a type's choices, because the slot's type can change before the next repaint.
- **No knob prints its value.** A bubble beside the knob being turned does, so readouts cost no row height.
- **A caption that does not fit its box changes font.** A `juce::Label` narrower than its text first squashes the glyphs (default minimum horizontal scale 0.7), then drops the font size, so the word reads as another face; POSITION did in a box its knob's width. So `ui::knobLabelBounds(block, cell)` (`ui/ForgeControlBlock.h`) spans the wider of the knob, fader or rocker block and its cell, and `dressControlLabel` (`src/ForgeEditor.cpp`) sets a `{1, 0, 1, 0}` border with no side padding, without which WARP 1/WARP 2 still overflowed their weight-2 cells at the minimum window. `captionsKeepTheirSizeSuite` (`forge_layout`) checks every caption-size label's string width against its box on OSC, MATRIX, MIX and FX at the minimum and default sizes. It walks visibility flags, because `isShowing()` is always false in a headless editor with no peer.
- **Plates carry no legend.** A plate's foot is only `plateFootMargin` (6 px, `ui/ForgeModule.h`), kept clear of the housing's rail and rivet. The 20 px stamped legend strip (OSCILLATOR A … A-03) was removed in `7be9a56` because it repeated the header, and its height went to the knobs, displays, lower row and mixer strips. Part numbers (B-01…, A-06) stay in the header via `plateCode`; a group such as SUB / NOISE is keyed only by its `group` string, and `drawGroupPlate` draws just the housing.

Every frame repaints the whole panel and every ordinary knob shares one size: see [Forge's panel repaints whole at 24 Hz](forge-panel-repaint-cost.md) and [One knob diameter for the whole Forge panel](forge-knob-diameter-is-panel-wide.md). Lettering is four subset faces chosen by job (`ui/ForgeType.h`); the static metal is drawn by `ui/ForgePanels.h` and cached.

## Related

- [Rhino Forge](forge.md)
- [Forge modulation: matrix, envelopes, LFOs and macros](forge-modulation.md)
- [Seeing the UI without taking the screen](headless-ui-snapshots.md)
- [Colours and typography](colours-and-typography.md)
