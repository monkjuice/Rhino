---
title: "Forge modulation: matrix, envelopes, LFOs and macros"
type: component
summary: Eight matrix slots route envelopes, LFOs, macros and performance sources to any continuous control, per voice, in the knob's own range.
tags: [forge, modulation, lfo, envelopes]
sources: []
updated: 2026-10-03
---

# Forge modulation: matrix, envelopes, LFOs and macros

Everything that moves a control goes through one matrix of eight slots (`modSlotCount`, `core/ForgeMatrix.h`). Each slot is four host parameters, `mod{n}Source`, `mod{n}Dest`, `mod{n}Depth` and `mod{n}Bipolar`, so a routing automates and saves like a knob. Engine: `Core::applyModulation` in `core/ForgeCore.h`. Panel: `src/ForgeEditorModulation.cpp`.

## Sources and destinations

Sources (`ModSource`): ENV 1-4, LFO 1-6, velocity, note, MACRO 1-8, mod wheel. Destinations are the continuous controls, from oscillator pitch to every rack slot's knobs and the warp depths. Deliberately absent: `output`, bus levels and sends (applied after the voice sum), a rack slot's LEVEL, and every mode, because sweeping unrelated choices is a stutter. Slots store both ends as indices: see [Stored indices are append-only](append-only-stored-indices.md).

## How slots apply

- Per voice, per sample, in the destination's normalised space. `prepareToPlay` hands Core each destination's `NormalisableRange`, so one depth means the same on a pan as on a skewed frequency.
- Offsets are summed per destination, then applied once and clamped. Applied slot by slot, two half-depth slots would not equal one full one.
- **BI** subtracts 0.5 from a source that only rises; LFOs already swing ±1 and are left alone. It is Serum's POL ([Why a patch copied from Serum sounds different](serum-patches-sound-different.md)).
- A per-voice source on a rack knob resolves to the loudest voice, because racks run after the voices.

## On the panel

Drag a source's handle onto a knob, stepper or bar. The handle is never the control itself, so dragging a knob still turns it. A new routing lands at half depth so the drop visibly does something. A modulated knob shows a faint reach ring plus a bright arc for where the loudest voice has it now, published once per block; the pointer stays at the parameter's value. The ring is draggable only while exactly one slot points there, since a sum of two has no single meaning.

## The modulators

ENV 1 is the amplitude; ENV 2-4 reach anything only through a slot, and all four run in every voice. An LFO in TRIG (the default) or ENV runs per voice; OFF is one free-running cycle shared by all. RATE reads in HZ or BPM, with 120 standing in for a host that reports no tempo. LFO shapes are node tables (`core/ForgeLfoTable.h`) with parabolic bends; even the Default sine is two parabolas, about 3% hotter in RMS. Custom tables ride in the patch, and `.forgelfo` files live in `Documents/Rhino Forge/LFO Tables`. The eight macros are sources only.

Slot 1 ships wired to LFO 1 at zero depth with the literal destination `13` (`src/ForgeParameters.cpp`), meant as CUTOFF; since the format-3 reorder, 13 is C PAN (checked 2026-10-03).

## Related

- [Forge engine (Core)](forge-engine.md)
- [Forge editor (panel)](forge-editor.md)
- [Forge mixer and effects racks](forge-mixer-and-fx.md)
- [Forge MIDI learn](forge-midi-learn.md)
- [Displays draw from the DSP](displays-draw-from-the-dsp.md)
- [Hazards found while seeding the wiki](known-hazards.md)
