---
title: One knob diameter for the whole Forge panel
type: gotcha
summary: Every ordinary knob takes the tightest cell's size, so crowding one module shrinks them all; the test meant to catch it cannot fail.
tags: [forge, ui, layout, testing]
sources: []
updated: 2026-10-03
---

# One knob diameter for the whole Forge panel

Every ordinary knob on Forge's panel is drawn at one size, `ui::uniformKnobDiameter` in `ui/ForgeControlBlock.h`. That is the smallest `min(cell width − 6, cell height − 14)` over every knob cell on the panel, where 14 is the label line above the circle. The rule is deliberate: sized per module, SUB's single knob would dwarf GLOBAL's. The catch is that a row or cell added to *one* module can shrink every knob on the panel.

## Who competes

Modules declared with `compactKnobs` size their knobs to their own cells and are left out of the minimum: the macros, the nine MIX strips, the FX rack and the six arp panes (`ui/ForgeModules.h`). Without that, the narrow mixer channels alone would set the size for everything. The oscillators, sub, noise, filter, GLOBAL, ENV and LFO share the one budget.

Height usually binds, because the rule takes 14 px off the height and only 6 off the width. On 2026-09-22, with two oscillators, the oscillators set the size at 54 px from 74×68 cells, and narrowing the filter's cells from 104 to 69 px wide changed nothing. The third oscillator (commit `a5103e9`, 2026-09-28) narrowed each oscillator plate to 16/3 grid columns and cut their display share from 44% to 30%, so those figures are history. Since commit `7be9a56` (2026-10-03) dropped the 20 px plate legend for a 6 px foot margin, the diameter at 1440×900 is 46 px, still bound by the oscillators' two knob rows (95×60 cells); with the legend it was 43 px (95×57). Print the limits before trusting any number.

## The test no longer guards it

`uniformKnobDiameter` never returns less than 40 (`jmax(40, smallest)`). Since `a5103e9` the layout test's threshold is also 40, so `the shared knob diameter stays usable` and `knobs stay usable at every allowed size` in `tests/ForgeTestsLayout.cpp` cannot fail, and the per-cell printout behind them never runs (checked 2026-10-03). A crowded cell now shows only as a 40 px knob drawn larger than its cell. Until the floor and the threshold are separated, check by eye, or temporarily print every cell's limit.

One related standard still bites: `a stacked choice stays tall enough to read` (`tests/ForgeTestsFx.cpp`, area `fx`) wants each segment of a two- or three-choice selector to be at least 14 px tall at the *minimum* window, 1180×820. It names the module, row and cell when it fails.

## Making room

- Print every cell's limit once instead of guessing at row weights. Giving the filter a fourth row once took three build-and-test cycles of guessing.
- What usually pays for a new row is the module's `displayShare`. The plate foot is already down to `plateFootMargin` (6 px), the clearance the housing's rail needs, so there is nothing left to reclaim there.
- Better still, seat the row inside the display (`Seat` in `ui/ForgeModule.h`). The filter's type field and routing chips sit in strips at the top and foot of its response display, which costs the plot a strip and the knobs nothing.

## Related

- [Forge editor (panel)](forge-editor.md)
- [Forge filter](forge-filter.md)
- [Forge's panel repaints whole at 24 Hz](forge-panel-repaint-cost.md)
- [Seeing the UI without taking the screen](headless-ui-snapshots.md)
- [Build and test Forge](build-and-test-forge.md)
- [Hazards found while seeding the wiki](known-hazards.md)
