---
title: Forge H1, the hardware control surface
type: component
summary: Forge H1 is a planned hardware control surface for standalone Forge (55 encoders, 6 displays, about 60 buttons, a Nektar SE49 keybed), designed in its own repository, C:\projects\forge-h1, with a browser planner; nothing in this repository depends on it yet, and how its controls will talk to Forge is not decided.
tags: [forge, hardware, controller, forge-h1]
sources: []
updated: 2026-10-27
---

# Forge H1, the hardware control surface

## What it is

Forge H1 is the user's planned instrument around [Rhino Forge](forge.md): 55 rotary encoders, about 60 buttons, a
toggle, LEDs, six displays (three 3.2″ SPI TFTs for the oscillator, LFO and envelope, a 5″ RA8875 screen for a
context page, two 128 × 64 OLEDs for the arp and the clock) and a 49-key keybed taken from a Nektar SE49. A PC runs
standalone Forge, at first outside the box on its own supply. As of its last plan (2026-10-24) it is a prototype
being laid out, not a built device.

## Where it lives

A git repository of its own, `C:\projects\forge-h1` (branch `main`, no remote on 2026-10-27), apart from this one.
Its wiki, `wiki/` there, is the canonical record of the hardware; start at `wiki/overview.md` and
`wiki/pages/hardware-plan.md`. This page only points there.

- `planner/`: a local React and TypeScript browser app (`npm run dev`, Node 20.19+) that lays out the top and rear
  panels at true scale, checks spacing and clearances, derives the bill of materials and the wiring, renders the
  instrument in 3-D, draws dimensioned views, writes a build guide and exports DXF, SVG and CSV.
- `planner/projects/forge-h1.json`: the panel's model, the source of truth every planner view derives from;
  `forge-h1-small.json` is a smaller concept explored beside it, not a decision.

## How it reaches Forge

- As planned on 2026-10-24, four Raspberry Pi Picos read the encoders and buttons through 74HC165 shift registers,
  drive the LEDs through PCA9685 drivers and the screens over SPI and I2C, and reach the PC through a powered USB
  hub, with the SE49's main board and the USB audio interface on the same hub.
- The SE49 is class-compliant USB MIDI, so its keys reach Forge as any keyboard's do.
- What the Picos send Forge, and which side draws the screens' pictures, are not decided: forge-h1's notes on
  screen drivers speak of drivers "in Forge" without settling where they run. Neither Forge's code nor Rhino's has
  anything written for the panel yet. Today the one way a controller binds to Forge's controls is
  [MIDI learn](forge-midi-learn.md).

## The earlier proposal

A first architecture for the console (six screens, 48 encoders reached by banking, pictures rendered on the display
nodes) was written in this repository on 2026-10-23 as `docs/forge-hardware-console-handoff.md` and a decision page,
`pages/forge-hardware-console.md`. Both were deleted on 2026-10-27 before they were committed; forge-h1's plan is the
one to read.

## Related

- [Rhino Forge](forge.md)
- [Forge MIDI learn](forge-midi-learn.md)
