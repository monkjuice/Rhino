---
title: "Forge modulation: matrix, envelopes, LFOs and macros"
type: component
summary: Thirty matrix slots, eight on the table until a patch needs more, route envelopes, LFOs, macros and performance sources to any continuous control, the envelopes' own knobs and the oscillators' OCT and FINE included, per voice, in the knob's own range.
tags: [forge, modulation, lfo, envelopes]
sources: []
updated: 2026-10-13
---

# Forge modulation: matrix, envelopes, LFOs and macros

Everything that moves a control goes through one matrix of thirty slots (`modSlotCount`, `core/ForgeMatrix.h`; eight until 2026-10-13). Each slot is four host parameters, `mod{n}Source`, `mod{n}Dest`, `mod{n}Depth` and `mod{n}Bipolar` (`modParameterId`), so a routing automates and saves like a knob; the parameters exist whether or not the table shows their row, and a preset written with eight opens with the other twenty-two at their defaults through `Processor::migrated`, no format bump ([Forge presets and state](forge-presets-and-state.md)). Engine: `Core::applyModulation` in `core/ForgeCore.h`. Panel: `src/ForgeEditorModulation.cpp` for what a slot routes, `src/ForgeEditorMatrixView.cpp` for the table's window.

## Sources and destinations

Sources (`ModSource`): ENV 1-4, LFO 1-6, velocity, note, MACRO 1-8, mod wheel, and since 2026-10-13 LFO 1-6 Y (22-27, appended at the end): a PATH LFO's second axis, which repeats the X output for any other type. `lfoIndexOf` names an X source, `lfoYIndexOf` a Y, `lfoOfSource` either ([Forge LFOs](forge-lfos.md)). Destinations are the continuous controls, from oscillator pitch to every rack slot's knobs and the warp depths, plus, since commit `e8702e4` (2026-10-13), each envelope's ATTACK, DECAY, SUSTAIN and RELEASE (`ENV 1 ATK` .. `ENV 4 REL`) and each oscillator's OCT and FINE (`A OCT`, `A FINE` ..). SEMI already was one, labelled `A PITCH`; OCT steps in whole octaves ([Forge oscillators and wavetables](forge-oscillators.md)). Deliberately absent: `output`, bus levels and sends (applied after the voice sum), a rack slot's LEVEL, and every mode, because sweeping unrelated choices is a stutter. Slots store both ends as indices: see [Stored indices are append-only](append-only-stored-indices.md).

The list (`core/ForgeMatrix.h`) runs: the named controls, then every rack slot's knobs and mix (from `fxDestinationBase`), then whatever was appended since, which is the warp depths, the filter's second field, the noise controls, then the envelope bank (`envDestinationBase`, four per envelope) and the tuning bank (`tuningDestinationBase`, OCT then FINE for A to C), which ends at `destinationCount`. So "is this a rack control" is a range test, `isFxDestination`, never `>= fxDestinationBase`, and `isEnvelopeDestination` is one too. The lower-bound test counted the appended controls as rack controls, which switched every bus on (commit `7e135cd`; [Forge mixer and effects racks](forge-mixer-and-fx.md)).

## How slots apply

- Per voice, per sample, in the destination's normalised space. `prepareToPlay` hands Core each destination's `NormalisableRange`, so one depth means the same on a pan as on a skewed frequency. A voice writes only the fields its live slots point at, onto a scratch copied once a block ([Forge engine (Core)](forge-engine.md)).
- Offsets are summed per destination, then applied once and clamped. Applied slot by slot, two half-depth slots would not equal one full one.
- **BI** subtracts 0.5 from a source that only rises; LFOs already swing ±1 and are left alone. It is Serum's POL ([Why a patch copied from Serum sounds different](serum-patches-sound-different.md)).
- A per-voice source on a rack knob resolves to the loudest voice, because racks run after the voices.
- **Envelope knobs are moved before the envelopes run.** A voice updates its envelopes before the scratch patch is modulated, so writing their fields there would do nothing. When a live slot targets the envelope bank (`envModulated`, found once a sample beside `fxModulated`), `Core::modulateEnvelopes` copies `patch.envs` into `envScratch`, applies only that bank's offsets with the same summed-once arithmetic, and the envelopes update from the copy. `applyModulation` still sums those offsets, so the rings publish, but writes no envelope field ([Forge engine (Core)](forge-engine.md)).
- Sources are read before this sample's envelope update, so an envelope routed to its own knobs reads its previous sample: a one-sample feedback, not a circular dependency. Not guarded: ENV 1 to `ENV 1 SUS` at −100% alternates at the sample rate (each sample is the knob minus the last), bounded by the clamp and converging only below full depth. A stepped source on SUSTAIN steps the amplitude, as on any level.
- Once a block, `Modulation::settle` (called from `Processor::modulation`) lists the slots with both ends set and a depth, and every per-voice walk in Core, `applyModulation`, `restoreModulatedFields` and the LFO-in-use scan, reads that list, so thirty slots cost what the routings in use cost. A `Modulation` built by hand and never settled has nothing live, which is what `renderSample(patch, l, r)` passes.

## On the panel

Drag a source's handle onto a knob, stepper or bar. The handle is never the control itself, so dragging a knob still turns it. A new routing lands at half depth so the drop visibly does something. Drop targets and the Dest menu are both built from `destinations()`, so a newly appended destination needs no panel change: the ENV knobs and the OCT and FINE steppers became targets, and the end of the menu lists them, without a UI edit. A modulated knob shows a faint reach ring plus a bright arc for where the loudest voice has it now, published once per block; the pointer stays at the parameter's value. The ring is draggable only while exactly one slot points there, since a sum of two has no single meaning.

The MATRIX tab's table opens on eight blank rows and shows one row past the last slot with either end set, up to thirty (`ui::matrixShownRowCount`, `ui/ForgePlacement.h`), so there is always a blank row to set a routing up in by hand; clearing routings shrinks it back. Rows have one fixed height, what eight rows would get, clamped to 30-40 px (`matrixRowHeight`), and the window shows whole rows only: at the default window eight, at the largest eleven. The wheel over anything but a field scrolls it (a field steps on the wheel, and the editor hears that as an event naming a slider), a thumb in the gutter down the right says how far, and a drop or a menu assignment scrolls the row it landed in into view (`revealMatrixSlot`). Which rows are on screen is settled in `applyEnableStates` like everything else, from `matrixFirstRow` and the shown count; the header counts the routings. `matrixGrowsWithRoutingsSuite` in `tests/ForgeTestsLayout.cpp` holds the growth rule through the visible BI chips, and the layout sweep holds the window's geometry at every size.

Pictures follow the same loudest voice. Since `2d14aba` (2026-10-13) the oscillator tube draws a modulated POSITION and modulated warp depths through `Editor::modulatedValue`; before, an LFO on POSITION swept the sound while the tube stayed on the knob. With no note sounding the published offset is zero, so the tube, like the rings and ENV 1's display, rests on the knob ([Forge editor (panel)](forge-editor.md)). The envelope display is the gap: it draws the knob values, not the modulated shape, because the picture is also the hit-test for dragging its nodes, so while its knobs are modulated the playhead dot may leave the curve.

## The modulators

ENV 1 is the amplitude; ENV 2-4 reach anything only through a slot, and all four run in every voice. An LFO in TRIG (the default) or ENV runs per voice; OFF is one free-running cycle shared by all. RATE reads in HZ or BPM, with 120 standing in for a host that reports no tempo. An LFO is NORMAL (a built-in node table with parabolic bends, or a drawn one; even the Default sine is two parabolas, about 3% hotter in RMS), PATH (a route across the pad with X and Y outputs) or S&H (a random staircase with SMOOTH), read FORWARD, REVERSE or PING-PONG; all of that is on [Forge LFOs](forge-lfos.md). The eight macros are sources only.

Slot 1 ships wired to LFO 1 at zero depth with the literal destination `13` (`src/ForgeParameters.cpp`), meant as CUTOFF; since the format-3 reorder, 13 is C PAN (checked 2026-10-03).

## Stability coverage

`tests/ForgeTestsModulation.cpp` includes a deliberately hostile modulation scenario for finding DSP state corruption rather than checking one routing in isolation. It fills eight matrix slots by hand and then rewires all thirty at random every few blocks, runs six fast LFOs, drives cutoff, resonance, drive and the filter type's secondary control at once, and holds four notes through representative BASIC, ANALOG, RESONATOR and CHARACTER filters. Every render must stay finite, bounded and non-silent; since 2026-10-13 it also routes `ENV 1 ATK`, `ENV 1 SUS`, `A OCT` and `A FINE`. `appendedDestinationsSuite` holds the appended banks' order, ids and fields, `modulatedEnvelopeSuite` (`ForgeTestsEnvelope.cpp`) and `modulatedTuningSuite` (`ForgeTestsOscillator.cpp`) what they do, and `ForgeTestsLayout.cpp` that every oscillator's OCT, SEMI and FINE are stepper destinations; all pass in the 18 Forge CTest areas, built with no warnings (`e8702e4`, 2026-10-13). This complements the direct cutoff-sweep regression in `tests/ForgeTestsFilter.cpp`: the focused test preserves the exact live failure cadence, while the matrix stress test broadens the combinations likely to expose the same class of fault.

## Related

- [Forge LFOs](forge-lfos.md)
- [Forge oscillators and wavetables](forge-oscillators.md)
- [Forge engine (Core)](forge-engine.md)
- [Forge editor (panel)](forge-editor.md)
- [Forge mixer and effects racks](forge-mixer-and-fx.md)
- [Forge MIDI learn](forge-midi-learn.md)
- [Displays draw from the DSP](displays-draw-from-the-dsp.md)
- [Hazards found while seeding the wiki](known-hazards.md)
