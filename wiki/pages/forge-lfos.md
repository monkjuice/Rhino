---
title: Forge LFOs
type: component
summary: Each of the six LFOs is a NORMAL shape, a PATH across the pad with X and Y outputs, or an S&H staircase, read FORWARD, REVERSE or PING-PONG, and the panel draws it from the one function the voice reads.
tags: [forge, modulation, lfo, ui]
sources: []
updated: 2026-10-13
---

# Forge LFOs

Six LFOs (`lfoCount`), each with the parameters `lfo{n}Type`, `Shape`, `Mode`, `Direction`, `Smooth`, `Rate`, `RateUnit` and `Division` (`src/ForgeParameters.cpp`). The vocabulary is `core/ForgeVoiceParts.h` (`LfoType`, `LfoDirection`, `LfoShape`, `LfoSetting`, `lfoOutputAt`); tables and paths are `core/ForgeLfoTable.h`; the per-voice running is `core/ForgeCore.h`; the Processor's storage is `src/ForgeProcessorLfo.cpp`; editing is `src/ForgeEditorLfo.cpp` and `src/ForgeEditorInput.cpp`; the picture is `ui/ForgeEnvelopeVisuals.h`. Since 2026-10-13 the types and the direction are Serum 2's TYPE and DIRECTION.

## Three types

- **NORMAL**: one of four built-in shapes (SINE, TRI, SAW, SQR; `lfoShapeCount` is 4) kept as node tables with parabolic bends, or a drawn `LfoTable`. `lfoWave(shape, phase)` reads a built-in. Custom tables ride in the patch; `.forgelfo` files live in `Documents/Rhino Forge/LFO Tables`.
- **PATH**: a route across the pad, `LfoPath`, two to `maxLfoPoints` points with x and y both 0..1. `LfoPath::at` gives every segment equal time, so two points in one place are a pause, which constant speed could not draw. Segments are straight; there are no curve handles and no path file library yet. The default is a diamond (`lfoDefaultPath`, five points from top centre). A path puts out two values, X (the LFO's usual source) and Y, each coordinate scaled to -1..1.
- **S&H**: a staircase of random steps, one per grid column (`lfoStepCount` is the table's `columns`, clamped 1..32, so the footer's column field is the step count). `Core::startLfoCycle` takes a whole flight at the top of every cycle and on retrigger: `count` steps plus one more, the next cycle's first, so the last step's ramp has a target; a cycle that turns over inherits that step, a note takes every step new. `LfoSteps` is 33 floats (`maxLfoSteps`), held per voice and once for the free-running cycle. **SMOOTH** (0..100 %) is the fraction of each column that ramps linearly to the next step instead of holding; at 100 % the staircase is a line through its steps. S&H has no direction (`lfoDirectionOf` answers FORWARD) and no shape.

S&H was shape 4 of the NORMAL list until 2026-10-13. `Processor::migrated` turns a saved `lfo{n}Shape` of 4 or more into Type = S&H and Shape = 0 ([Stored indices are append-only](append-only-stored-indices.md)).

## Direction, travel and position

A voice's `lfoPhase` is the *travel*: 0..1, or 0..2 for PING-PONG (`lfoTravelSpan`), advanced at the rate, so a ping-pong trip takes two cycles. `lfoPositionOf` maps travel to the position in the shape, which is what the panel's indicator shows: REVERSE runs right to left, PING-PONG turns round. ENV mode parks at the end of the travel (`advanceVoiceLfo`), so a reversed one-shot holds the shape's first point; taking the mode off ENV lets it run on.

## One output function

`lfoOutputAt(setting, position, steps)` returns `LfoOutput {x, y}` and is the only reading of an LFO: the voice, the free-running cycle and `drawLfo` all call it. Outside PATH, y repeats x, so a slot fed from an LFO's Y output keeps moving when the type changes instead of falling silent.

The Y outputs are six matrix sources `ModSource::lfo1Y..lfo6Y` (22-27), appended at the end of the source list. `lfoYIndexOf` names them, `lfoOfSource` answers for either axis, and `sourceIsBipolar` reads that, so BI leaves a Y source alone as it does an X ([Forge modulation](forge-modulation.md)).

## What the panel reads

`drawLfo` takes an `LfoPicture` (the `LfoSetting` with its table and path, the steps, the table name, the phase) and samples `lfoOutputAt` for NORMAL and S&H (S&H column by column so each riser is vertical); a PATH is drawn from its route, with a crosshair at the travel point so both outputs read off the grid ([Displays draw from the DSP](displays-draw-from-the-dsp.md)). The staircase shown is the loudest voice's: `Core::meterLfoSource` is a voice index or `freeLfoSource` (-1); `Core::lfoSteps(lfo)` looks the flight up through it rather than copying 33 floats a sample, and the Processor publishes it once a block as `meterLfoStep`, read back by `Processor::lfoSteps`. With nothing sounding the panel is shown the free-running flight whatever the mode, so a resting S&H reads as a staircase rather than a flat line; a note's own flight replaces it the moment one sounds.

**The footer** runs TYPE (`lfoTypeBounds`, far left), the name, the chevrons, DIRECTION (`lfoDirectionBounds`), then the column and row fields, with no slack: the name field is the one whose width gives (`lfoNameBounds(area, type)`); it used to be capped at 200 px, which left a gap before the grid. The name reads "Sine", not "Default / Sine", to fit the minimum window; a path's reads "Default path" or "Custom path", and its menu offers only "Default path". `lfoFooterShowsName/Chevrons/Direction` decide what a type shows, and the painter and the hit-tests both ask them: S&H shows only TYPE and the grid; PATH has no chevrons and its name takes their room. `lfo{n}Type` and `lfo{n}Direction` are `ui::displayParameters` of the LFO module so the layout test finds them ([Forge editor (panel)](forge-editor.md)). The SHAPE stepper is in charge only for a NORMAL LFO on a built-in table, and SMOOTH only for S&H (`applyEnableStates`).

**The Y card.** A PATH LFO splits a second drag card "Y" (`ui::lfoYHandleWidth`, 22 px) off the right-hand end of its "LFO n" card, as Serum's tile splits into X and Y, so the bank cards and the header's rate readout never move (`src/ForgeEditorLayout.cpp`); the first attempt hung it beside the card and pushed the bank cards into the readout at the minimum window width. `lfoPathHandleShown` records whether it is up; `applyEnableStates` calls `resized()` when the shown type flips, so a type changed by a preset or the host moves the header too.

**Editing a path.** Points drag on both axes with no ordering constraint; double-click on the plot adds a point at the nearest segment; right-click or double-click on a point removes it, the ends included, as long as two remain (a table's end points stay put).

## State

A drawn path is a `ForgeLfoPath` node (`index`, `Point x y` children), written only when custom, beside `ForgeLfoTable`. The Processor keeps it in atomics and stores the `custom` flag last with release order, so the audio thread reads the default until the whole path is there ([Forge presets and state](forge-presets-and-state.md)).

## Tests

`tests/ForgeTestsLfo.cpp` (types, the path, S&H steps and SMOOTH, direction, the Y source through the matrix, path round-trip), `ForgeTestsPresets.cpp` (the shape-4 migration), `lfoFooterSuite` in `ForgeTestsDisplays.cpp` (footer geometry without slack, the Y card), `ForgeTestsModulation.cpp` (source count) and `ForgeTestRender.cpp` (LFO 2 is S&H by type).

## Provenance

The Serum 2 User Guide (pp. 188-193) names the three types and DIRECTION but describes neither Path nor S&H; the Path semantics here, an XY route with X and Y outputs, follow third-party Serum 2 guides (noted 2026-10-13; [Comparing Forge with Serum](comparing-forge-with-serum.md)).

## Related

- [Forge modulation: matrix, envelopes, LFOs and macros](forge-modulation.md)
- [Forge engine (Core)](forge-engine.md)
- [Forge editor (panel)](forge-editor.md)
- [Displays draw from the DSP](displays-draw-from-the-dsp.md)
- [Stored indices are append-only](append-only-stored-indices.md)
- [Forge presets and state](forge-presets-and-state.md)
