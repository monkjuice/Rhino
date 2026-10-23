---
title: Forge mixer and effects racks
type: component
summary: The MIX tab's ten channels and two busses, and three eight-slot effects racks of nine types that run on the summed voices, each slot printing its readings and fed meters by the engine once a block.
tags: [forge, mixer, fx, signal-flow]
sources: []
updated: 2026-10-22
---

# Forge mixer and effects racks

MIX and FX each take the whole signal row. Engine: `Core::renderSample` and `resolveBuses` in `core/ForgeCore.h`, racks rendered by `core/ForgeFxDsp.h` from `core/ForgeFx.h`. Panel: `src/ForgeEditorFx.cpp`, `src/ForgeEditorFxView.cpp`.

## The mixer

Ten channels in signal order since 2026-10-22 (`mixerColumnSpan` 24/10): SUB, OSC A, OSC B, OSC C, NOISE, FILTER 1 (module `mixFilter`), FILTER 2 (`mixFilter2`), BUS 1, BUS 2, MAIN. None keeps its own copy of a setting. A source's TO row is two chips, F1 and F2: the same route switches each filter's chips drive (`routeA`, `filter2RouteA` ...), so a source can feed both. It was one FILTER/MAIN stepper while there was one filter. FILTER 1's TO field is `filterSerial` (MAIN or FILTER 2); FILTER 2 has none ([Forge filter](forge-filter.md)). A strip's header enable is the source's own (and so its mute), and MAIN's fader is GLOBAL's `output`.

- The busses are summed once across all voices, so a reverb on a bus is one tail fed by every note.
- A bus goes to MAIN or to the other bus. If the two busses point at each other, the engine sends the second one to MAIN. The panel does not refuse the setting, because a host could automate into it anyway.
- Two pan laws, in `core/ForgePatch.h`: a *source* pan is equal-power (−3 dB at centre); a *channel* pan (filters, busses) is the same curve normalised to unity at centre, so centring a balanced sum does not turn it down. Panning cost SUB and NOISE 3 dB, so their defaults rose 3 dB and `Processor::migrated` raises pre-mixer patches' levels by √2, once.

## The racks

Three racks (MAIN, BUS 1, BUS 2) of eight slots (`fxSlotCount`); a slot holds one of nine types: reverb, delay, chorus, distortion, EQ, filter, compressor, phaser and, since 2026-10-21, flanger (`FxType`, append-only, so FLANGER is 9). A bus runs sends → its rack → its fader and pan → its destination; everything reaching MAIN runs the MAIN rack, then the master level. Why every slot has the same anonymous parameters: [Forge's FX slots declare generic parameters](forge-fx-slots-have-generic-parameters.md).

- Each slot holds state for every type, sized at `prepare` (delay lines of 1.6 s), so changing type mid-note never allocates.
- Every rack knob, MIX and LEVEL is a modulation destination; drag a source's handle onto it as anywhere else ([Forge modulation](forge-modulation.md)).
- A per-voice source on a rack knob resolves to the loudest voice; an LFO in OFF drives a rack cleanly. Only the racks come from that voice: the busses are always read from the patch as it stands. Until commit `7e135cd` any modulation counted as "on a rack" (see `isFxDestination` in [Forge modulation](forge-modulation.md)), and the busses were then resolved from a scratch patch holding only the racks, so every bus came back on, routed to MAIN at 0.75.
- Nothing clears a rack in normal playing, and all-notes-off no longer does either, so a rack zeroes non-finite input, and a slot that produces a NaN or infinity itself is reset rather than fed back (commit `2cc79de`). Before, one bad sample rang on in a feedback line for good.
- A knob the type lacks is removed; one a mode has made meaningless (`fxKnobLive`) is greyed. Both are decided in `Editor::applyEnableStates`, because the 24 Hz refresh overwrites the decision anywhere else, which is how the first rack failed.
- Since 2026-10-21 each slot is laid out by its type's own strip plan (`ui/ForgeFxStrips.h`), and its identity card is its bypass switch ([Forge's FX strips are per-type plans in a pure header](forge-fx-strip-plans.md)). The plan replaced the declared row and `controlBlock` for rack slots.
- Each type draws one or two pictures (`ui/ForgeFxDisplay.h`) from the DSP's own functions, held to measured audio ([Displays draw from the DSP](displays-draw-from-the-dsp.md)). Most can be dragged by their nodes to set the knobs they stand for (`ui/ForgeFxGestures.h`).
- In the list, **+ ADD EFFECT** fills the next free slot. Dragging a row reorders the chain by moving values between slots, and removing a row closes the gap. A filled slot's type cannot be changed. Alt+F expands the rack over both module rows.

## Readings and meters

- **Readings.** Every rack knob prints its value under it at rest (`Editor::paintFxReadings`). The text is the parameter's own (`Processor::fxKnobText`), so it matches the bubble and the DSP. This is the panel's one exception to "no knob prints its value" ([Forge editor (panel)](forge-editor.md)).
- **Meters.** `FxRack` keeps an `FxSlotMeter` per slot as it renders (`core/ForgeFxDsp.h`). It records whether the slot rendered, the input peak, the wet output peak, the compressor's gain reduction, and the LFO phase of a chorus, phaser or flanger.
- **Publishing.** `Processor::processBlock` takes the meters once a block into atomics, which only ever raise a peak or deepen a reduction, so a reading holds until it is taken and a lost race costs one frame. `Editor::takeFxMeters` runs on the 24 Hz timer, but only for the shown rack and only while the FX page shows. It copies them into `ui::FxLive`, easing the gain-reduction needle back over a few frames.
- **Readers.** The meters drive the panel lamps (`fxLampLevel`), the compressor's needle and the input dot on its curve, and the chorus, phaser and flanger sweeps. Until a slot has published, its pictures draw from the settings alone and its live lamps stay dark.
- **The compressor's meter** (since commit `44e19bb`) is lit from behind in the type colour, with a red band past -10 dB. The live pass adds a lamp that lights with reduction and prints the reduction as a figure. A `forge_fx` test checks the needle end to end: it drives a compressor through `processBlock`, pumps the editor's own timer through a wait ([Seeing the UI without taking the screen](headless-ui-snapshots.md)), and requires the meter's pixels to change. The default compressor takes about 14 dB off the default patch. When the pictures looked greyed out, the cause was the picture cache, not the art ([A JUCE image is drawn at the last colour's opacity](juce-image-draws-at-current-opacity.md)).

Still absent as of 2026-10-21: a `DIRECT` output, Serum's Bode, Convolve, Hyper/Dimension and splitter types, and rack presets.

## Related

- [Forge's FX strips are per-type plans in a pure header](forge-fx-strip-plans.md)
- [Forge modulation: matrix, envelopes, LFOs and macros](forge-modulation.md)
- [Forge filter](forge-filter.md)
- [Forge noise module](forge-noise.md)
- [Forge engine (Core)](forge-engine.md)
