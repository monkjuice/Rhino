---
title: Forge mixer and effects racks
type: component
summary: The MIX tab's nine channels and two busses, and three eight-slot effects racks that run on the summed voices.
tags: [forge, mixer, fx, signal-flow]
sources: []
updated: 2026-10-05
---

# Forge mixer and effects racks

MIX and FX each take the whole signal row. Engine: `Core::renderSample` and `resolveBuses` in `core/ForgeCore.h`, racks rendered by `core/ForgeFxDsp.h` from `core/ForgeFx.h`. Panel: `src/ForgeEditorFx.cpp`, `src/ForgeEditorFxView.cpp`.

## The mixer

Nine channels in signal order: SUB, OSC A, OSC B, OSC C, NOISE, FILTER, BUS 1, BUS 2, MAIN. None keeps its own copy of a setting: a source's TO field is the `route*` switch the filter's chips drive (FILTER or MAIN), its header enable is the source's own (and so its mute), and MAIN's fader is GLOBAL's `output`.

- The busses are summed once across all voices, so a reverb on a bus is one tail fed by every note.
- A bus goes to MAIN or to the other bus. If the two busses point at each other, the engine sends the second one to MAIN. The panel does not refuse the setting, because a host could automate into it anyway.
- Two pan laws, in `core/ForgePatch.h`: a *source* pan is equal-power (−3 dB at centre); a *channel* pan (filter, busses) is the same curve normalised to unity at centre, so centring a balanced sum does not turn it down. Panning cost SUB and NOISE 3 dB, so their defaults rose 3 dB and `Processor::migrated` raises pre-mixer patches' levels by √2, once.

## The racks

Three racks (MAIN, BUS 1, BUS 2) of eight slots (`fxSlotCount`); a slot holds one of eight types: reverb, delay, chorus, distortion, EQ, filter, compressor, phaser (`FxType`). A bus runs sends → its rack → its fader and pan → its destination; everything reaching MAIN runs the MAIN rack, then the master level. Why every slot has the same anonymous parameters: [Forge's FX slots declare generic parameters](forge-fx-slots-have-generic-parameters.md).

- Each slot holds state for every type, sized at `prepare` (delay lines of 1.6 s), so changing type mid-note never allocates.
- A per-voice source on a rack knob resolves to the loudest voice; an LFO in OFF drives a rack cleanly. Only the racks come from that voice: the busses are always read from the patch as it stands. Until commit `7e135cd` any modulation counted as "on a rack" (see `isFxDestination` in [Forge modulation](forge-modulation.md)), and the busses were then resolved from a scratch patch holding only the racks, so every bus came back on, routed to MAIN at 0.75.
- Nothing clears a rack in normal playing, and all-notes-off no longer does either, so a rack zeroes non-finite input, and a slot that produces a NaN or infinity itself is reset rather than fed back (commit `2cc79de`). Before, one bad sample rang on in a feedback line for good.
- A knob the type lacks is removed; one a mode has made meaningless (`fxKnobLive`) is greyed. Both are decided in `Editor::applyEnableStates`, because the 24 Hz refresh overwrites the decision anywhere else, which is how the first rack failed.
- Slot displays are computed from the DSP's own functions ([Displays draw from the DSP](displays-draw-from-the-dsp.md)).
- In the list, **+ ADD EFFECT** fills the next free slot. Dragging a row reorders the chain by moving values between slots, and removing a row closes the gap. Alt+F expands the rack over both module rows.

Still absent as of 2026-10-03: a `DIRECT` output, Serum's Bode, Convolve, Flanger, Hyper/Dimension and splitter types, rack presets, and any metering.

## Related

- [Forge modulation: matrix, envelopes, LFOs and macros](forge-modulation.md)
- [Forge filter](forge-filter.md)
- [Forge noise module](forge-noise.md)
- [Forge engine (Core)](forge-engine.md)
