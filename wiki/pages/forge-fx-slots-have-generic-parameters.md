---
title: Forge's FX slots declare generic parameters
type: decision
summary: Every rack slot exposes the same twelve anonymous parameters, because a host's parameter list is fixed when the plugin is built.
tags: [forge, fx, parameters, automation]
sources: []
updated: 2026-10-03
---

# Forge's FX slots declare generic parameters

## Context

Forge's racks take Serum's shape: any effect type in any slot, in any order, duplicates allowed (three racks of eight slots and eight types as of 2026-10-03, see [Forge mixer and effects racks](forge-mixer-and-fx.md)). A plugin's parameter list is fixed when it is constructed, so a slot cannot grow parameters for whatever it holds. Naming every control of every type in every slot would mean several hundred parameters, nearly all dead at any moment.

## Decision

Each slot declares the same twelve parameters whatever it holds (`src/ForgeParameters.cpp`): `fx{rack}s{slot}Type`, `ModeA`, `ModeB`, `Bypass`, `Knob1`-`Knob6`, `Mix` and `Level`. The knobs are plain 0..1. What a knob *means* belongs to the type and is declared once in `fxTypes()` in `core/ForgeFx.h`: knob labels, mode choices, opening values. Three readers take it from there: the DSP in `core/ForgeFxDsp.h`, the panel's labels (`Editor::refreshFxSlots`) and the readout (`Processor::fxKnobText`), which calls the same helpers the DSP does, so "480 ms" in the bubble is the delay being heard. The mode fields are floats, not choice parameters, because their choices change with the type.

Rejected (`PLAN.md` M11a): a fixed chain of named effects, which would name every automation lane honestly but fix the order and forbid duplicates.

## Consequences

- An automation lane reads `MAIN 2 Knob 3`, not "Reverb Damp", and the matrix calls it `MAIN 2 K3`. Serum pays the same price for the same reason.
- Automation and matrix routings belong to a slot position, not an effect. Dragging a row in the FX list moves values between the fixed slot parameters (`Editor::moveFxSlot`), so whatever pointed at `MAIN 2 K3` now drives the effect that moved into slot 2.
- One default cannot suit eight types (100% wet suits an EQ and drowns a reverb on MAIN), so each type carries opening values. They are applied only when a type is chosen on the panel; a type arriving from a preset or from automation keeps the values it came with.
- A knob the type lacks has a null label and leaves the panel; one a mode makes meaningless (`fxKnobLive`) greys out.
- Each slot preallocates every type's state at `prepare`, so a type change mid-note never allocates.
- A mode field is read as `round(value × (count − 1))`, so growing its list has to keep old values landing on equivalent choices. The distortion's filter field keeps PRE LP and POST LP at 2 and 4 for that reason, and `FxType` is append-only ([Stored indices are append-only](append-only-stored-indices.md)).
- Going from four slots to eight added `s5`-`s8` parameters and changed no existing id.

## Related

- [Forge mixer and effects racks](forge-mixer-and-fx.md)
- [Stored indices are append-only](append-only-stored-indices.md)
- [Displays draw from the DSP](displays-draw-from-the-dsp.md)
- [Forge modulation: matrix, envelopes, LFOs and macros](forge-modulation.md)
