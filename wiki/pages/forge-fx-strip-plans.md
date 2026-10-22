---
title: Forge's FX strips are per-type plans in a pure header
type: decision
summary: Each rack slot is laid out by its effect type's own strip plan, from one pure function in ui/ForgeFxStrips.h that the editor, the painter and the layout test all call; the slot's identity card is its bypass, and a filled slot's type is never changed in place.
tags: [forge, fx, ui, layout]
sources: []
updated: 2026-10-22
---

# Forge's FX strips are per-type plans in a pure header

## Context

Every rack slot declares the same twelve controls ([Forge's FX slots declare generic parameters](forge-fx-slots-have-generic-parameters.md)), and until 2026-10-21 each slot was laid out from that declaration as one generic row, whatever it held. The effects are not the same shape: a distortion's filter mode, FREQ and Q are one call into the DSP and belong beside the curve they draw; a phaser's stage count belongs on its comb. The user approved the redesign on 2026-10-21 from a published mockup page. It landed in commits `7f3e81e`, `d9acbe8`, `17a2fea`, `abbb2f7` and `a350964`.

## Decision

- **The declaration stays the inventory.** `ui/ForgeModules.h` still declares ids, styles, attachments and banks. Geometry comes from `fxStripLayout(type, strip, sizes)` in `ui/ForgeFxStrips.h`, a pure function: a type and a rectangle go in, an `FxStrip` comes out, and nothing is read from the processor. The `FxStrip` holds:
  - one `FxSpot` per `FxRole`: the type card, two modes, six knobs, MIX, LEVEL and bypass;
  - up to two `FxWell`s, the pictures (`FxWellKind`);
  - up to four `FxPanel`s, framed groups of controls the DSP couples. Each panel's caption is engraved up a side lane. A panel may carry an `FxLamp`, and a mode `gate` that dims the whole panel.
- **One frame for every type.** The strip is a 48-unit grid of its width: 5 units of card, a 38-unit body that each type lays out its own way, and a 5-unit tail of MIX and LEVEL as matched knobs. Every knob, hero, normal or mini (`fxKnobSizesFor`), stands on the same caption, centre and reading lines.
- **92 px tall, three whole strips.** The user asked for taller strips and larger text, so `fxSlotHeight` (`ui/ForgeModule.h`) went from 76 to 92 px in commit `44e19bb`. 92 is the tallest height that still fits three whole strips in the rack at the smallest window. The rack's viewport is about 281 px there and 325 px at the default 1440x900, which now shows three strips instead of four. `rackCaptionsFitSuite` (`tests/ForgeTestsLayout.cpp`) requires `fxVisibleSlotCount >= 3` at both sizes. Each line a strip is built on (caption, knob centre, reading, mini rows, card, stacks) is a constant in `ui/ForgeFxStrips.h` set for that height, so a new height means moving every one of them.
- **Text.**
  - Readings are 11 pt. `drawFittedText` squeezes a long one to 0.8 width rather than cutting it.
  - Panel legends and the automation name are 9.5 pt.
  - Words inside a picture are 8.5 pt (`fxPictureTextSize`, `ui/ForgeFxDisplay.h`).
  - Captions keep the panel's one caption size.
- **Refactor first.** `7f3e81e` introduced the plan and reproduced the old row byte for byte. Only after that did each type get its own strip (`d9acbe8` onward).
- **The card is the bypass.** The identity card (`FxPlate`, `ui/ForgeFxVisuals.h`) is lit in the type colour while the slot plays and dark when it is bypassed, with the FX list's power glyph in its corner. A right-click opens the bypass parameter's menu, for MIDI learn. At the user's request, the BYP chip and the card's replace-the-effect menu were removed. Effects are added and removed only in the list, so a slip while reaching for a knob cannot change what a slot holds.

## Consequences

- The editor (`Editor::fxStripFor`, `src/ForgeEditorLayout.cpp`), the shelf painter (`src/ForgeEditorFxView.cpp`) and the layout test all call the same function, so they cannot disagree. The plan works in editor coordinates, and the controls are moved into `fxControlViewport`'s coordinates ([Forge editor (panel)](forge-editor.md)).
- The layout reads `fxTypesShown`, the type each slot last showed, not the parameter. When a preset or the host changes a type, the 24 Hz timer notices the mismatch and runs `refreshFxSlots`, `resized` and `repaint`.
- A new type needs a strip function and a case in `fxStripLayout`. Until it has them, `fxGenericStrip` lays it out: its modes, one well showing the type's mark, and its knobs sharing the rest. The old generic per-type pictures that used to fill that well were deleted once every type had its own.
- A picture that stands for a knob can be dragged by its node (commit `f4241e4`). The gestures are pure functions in `ui/ForgeFxGestures.h` that invert the knob laws (`fxKnobForHertz`, `fxKnobForScaled`) on the axes the picture is drawn on ([Displays draw from the DSP](displays-draw-from-the-dsp.md)):
  - `fxWellGrabAt` decides what a press takes and keeps it for the whole drag: the nearer of the EQ's two band nodes, the nearer of the reverb's two cuts, the delay's TIME above the picture's middle and OFFSET below it. The gain-reduction meter and the flanger's comb only show what is happening and take no press.
  - A press counts only inside the well's `plot`, not the whole well. The phaser's STAGES field is seated in the top of its well, and an early version took the presses meant for it.
  - `fxWellDragTo` gives the knobs a drag sets. A node jumps to the press, as a slider's thumb does, under a crosshair cursor. The exception is the distortion's shape picture (`fxWellFollowsPointer`): it sets DRIVE by how far the pointer moves up, under an up-down cursor.
  - `Editor::fxWellAt` (`src/ForgeEditorFxView.cpp`) finds the well under the pointer on the shown rack. The editor opens one host gesture per knob a drag touches and closes them on release.
  - The tests drag every picture to a drawn value and read the knob back, and drive one drag through the panel's own pointer handling. A picture's axis can be wider than its knob's range: the phaser's starts at 20 Hz but FREQ at 80 Hz, so a press near the left edge clamps FREQ to its bottom, and a drag that stays below 80 Hz moves nothing. That failed the first end-to-end test. It now finds the picture by scanning for the crosshair cursor and presses in its middle.
- `requireFxStripsHold` (`tests/ForgeTestsLayout.cpp`, CTest `forge_layout`) sweeps every type at many window sizes. It requires that:
  - exactly the type's controls are placed, inside the strip and beside the list;
  - no control overlaps another, or a well it is not seated in;
  - knobs are round, at least 24 px and no larger than the shared diameter;
  - each stacked choice is at least 14 px tall;
  - the bypass lies inside the card.
- Tests find controls by where they stand, never by a fixed offset. `rackKnobs` (`tests/ForgeTestsFxModulation.cpp`) used to look a fixed distance below a caption's top, which assumed the 76 px strip. It now takes the knob whose top lies within 12 px of the caption's foot.

## Related

- [Forge mixer and effects racks](forge-mixer-and-fx.md)
- [Forge's FX slots declare generic parameters](forge-fx-slots-have-generic-parameters.md)
- [Displays draw from the DSP](displays-draw-from-the-dsp.md)
- [Forge editor (panel)](forge-editor.md)
- [One knob diameter for the whole Forge panel](forge-knob-diameter-is-panel-wide.md)
