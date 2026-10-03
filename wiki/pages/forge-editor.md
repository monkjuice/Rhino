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

**Adding a control** is its parameter in `src/ForgeParameters.cpp` plus a line in `ui/ForgeModules.h`. The layout test (`tests/ForgeTestsLayout.cpp`) fails if the two disagree in either direction, if a control or enable lacks a real tooltip in `ui/ForgeTooltips.h`, or if a module escapes or overlaps at any allowed size, swept in 20 px steps (about 800 sizes between 1180×820 and 2000×1180).

## Pages

OSC, TABLE and MATRIX swap only the oscillator columns; MIX and FX take the whole signal row. The arp overlay is a sixth page, `Page::arp`, that is never a tab ([Forge arpeggiator](forge-arpeggiator.md)). Switching hides and shows components instead of rebuilding them, so a hidden knob stays attached, automated and modulated.

## Rules that bite

- **Visibility is decided only in `Editor::applyEnableStates`.** It runs every 24 Hz tick, so anything hidden or greyed elsewhere is put back next frame; the first FX rack failed exactly so.
- **Read values through `Editor::value`**, which asks the parameter rather than the cached atomic beside it. The atomic is updated by another listener, and a panel attachment called first reads the old value: a mode field looked right only after an unrelated click (`PLAN.md` M11d).
- **Copy, do not point.** `FxSelector` copies a type's choices, because the slot's type can change before the next repaint.
- **No knob prints its value.** A bubble beside the knob being turned does, so readouts cost no row height.

Every frame repaints the whole panel and every ordinary knob shares one size: see [Forge's panel repaints whole at 24 Hz](forge-panel-repaint-cost.md) and [One knob diameter for the whole Forge panel](forge-knob-diameter-is-panel-wide.md). Lettering is four subset faces chosen by job (`ui/ForgeType.h`); the static metal is drawn by `ui/ForgePanels.h` and cached.

## Related

- [Rhino Forge](forge.md)
- [Forge modulation: matrix, envelopes, LFOs and macros](forge-modulation.md)
- [Seeing the UI without taking the screen](headless-ui-snapshots.md)
- [Colours and typography](colours-and-typography.md)
