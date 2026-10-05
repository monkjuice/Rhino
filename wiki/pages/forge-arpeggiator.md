---
title: Forge arpeggiator
type: component
summary: Stands in front of the voices, hands notes back through two callbacks, and opens as an overlay from a plate beside the keys.
tags: [forge, arpeggiator, midi]
sources: []
updated: 2026-10-05
---

# Forge arpeggiator

One arpeggiator modelled on Serum's ARP (manual pp. 244-266). The logic is `core/ForgeArp.h`, `Processor::processBlock` (`src/ForgeProcessor.cpp`) drives it, and its six panes are declared in `ui/ForgeModules.h`.

## In front of the voices

The arp decides which notes Core plays, so it is not part of `Core` and Core knows nothing of it. It hands notes back through two callbacks, so `tests/ForgeTestsArp.cpp` drives it with two lambdas and checks lists of note numbers, not waveforms; only the last suite uses a `Processor`, to prove notes reach the voices.

Nothing allocates: `advance` runs every sample, so held keys (up to 16, oldest dropped), play order and sounding notes are fixed arrays. The Processor converts tempo to a step length, using 120 BPM when the host reports none.

## Shapes and timing

Eighteen shapes (`ArpShape`, append-only), and **one list serves twice**: `arpOrder` works on indices, so it orders the held keys and, called again, the transposition stages. The tests pin down the turning points: Up/Down plays the top note once, Up+Down plays it twice. TRIP and DOT scale the division instead of adding entries, so the list stays at seven (1/1 to 1/64, default 1/16). LAUNCH QUANT delays a starting pattern to the next division of the host's bar, only while the host plays.

## On the panel

The ARP plate took the keyboard's bottom octave at exactly that width, so every key kept its size. Its lamp switches the arp on; the rest of the plate opens an **overlay on ENV and LFO**, not a tab, leaving the macros, oscillators and filter in view. It is still declared as a page, `Page::arp`, that no tab is ever set to; `everyPage` means every *tab*, so nothing shares a page with the arp and the layout test's no-overlap check holds. `coveredByArp` derives what it covers from its panes' grid cells. The overlay and the expanded FX rack close each other.

## Behaviour worth knowing

- With THRU on, keys also reach the voices directly. Core keys voices by note number, so a held key and the arp's same pitch share one voice and the arp's gate cuts it short.
- A key's release follows the key, not the switches. The processor remembers which keys went straight to the voices (`directNotes`) and sends their note-off there whatever ARP and THRU say by the time the key comes up; all-notes-off reaches the voices whenever any key did. Before commit `32147e1`, a key pressed with the arp off and released after switching it on (or let through by THRU, then THRU switched off) released only into the arp, and its voice hung until stolen.
- The arp tracks at most 32 sounding notes (`arpMaxSounding`). When the list is full, which a many-key chord under a long gate with RANGE and SHIFT reaches, the note nearest its own end is stopped to make room. The arp used to start the new note untracked, and nothing ever stopped it (commit `2cc79de`).
- The sustain pedal (CC 64) works the latch. Switching the arp off mid-phrase releases the notes it holds.
- `getSampleRate()` is 0 when `prepareToPlay` is called directly, as the tests and the standalone do. The arp divided by it, stepped every sample and stacked voices into a drone; it now keeps the rate `prepareToPlay` was given. The test that caught it checks for silence between notes; an earlier one averaged over a longer window and passed on the broken arp.

Not started: the twelve launchable slots (M13b) and the pattern editor (M13c), which plans to reuse Rhino's `StepGrid` once a Session-free core is extracted from it ([Note editor (StepGrid)](note-editor.md)).

## Related

- [Rhino Forge](forge.md)
- [Forge engine (Core)](forge-engine.md)
- [Forge editor (panel)](forge-editor.md)
- [Real-time audio rules](real-time-audio-rules.md)
