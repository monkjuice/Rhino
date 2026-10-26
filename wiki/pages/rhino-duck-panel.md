---
title: Rhino Duck's panel
type: component
summary: Duck's 1000 x 620 industrial panel since 2026-10-26: a rail with the brand, a rubber-duck gain-reduction meter and Mix; the chips, scope and sixteen curve cells; a strip that bends the chosen curve beside the band split; and a preset bar that only steps through the curves.
tags: [duck, ui, juce, meter]
sources: []
updated: 2026-10-26
---

# Rhino Duck's panel

The editor of [Rhino Duck](rhino-duck.md), redrawn in 7a4c161 (2026-10-26) from a designer's package (`rhino_duck_handoff.zip`:
`HANDOFF.md`, mockups, `palette.json`, `layout.json`, SVGs) that merged Duck with the industrial Rhino and Forge look.
The scope stays the panel's reason for being. [Displays draw from the DSP](displays-draw-from-the-dsp.md) holds
throughout, and every control draws itself (`ui/DuckControls.h`: `Chip` with vector glyphs and a visible disabled look,
one `Knob` for Mix and the strip, `CutoffSlider`, `Switch`, `PresetBox`, `CurveCell`), so there is no look-and-feel.

## Layout

`ui::layoutFor` in `ui/DuckLayout.h` is still a pure function over 1000 x 620, scaled as a whole by the menu's window
size (75% to 200%, `setScaleFactor`, kept on the state as `uiScale`):

- **Rail** (left): the brand, the meter, Mix with DRY/WET ends and its reading.
- **Main plate**: Length chips (1/8 to 1/1), SYNC/MIDI/AUDIO, LOOP and the menu; the scope (718 x 224 at 100%, the
  test requires at least 700 x 216); the Shift arrows round the curve's caption; sixteen cells in two rows
  (`curveAtCell`).
- **Strip** (under it): Attack, Hold, Release, Depth knobs for the chosen curve, then the band: SOLO, the cutoff
  slider and WHOLE SIGNAL, a switch drawn as the inverse of the `band` parameter.
- **Preset bar** (foot): back and forward arrows and a box whose list is the sixteen curves under family headers.

The strip knobs are `juce::Slider`s whose ranges follow `holdRoom`/`releaseRoom` (in phase) for the shown curve; they
are synced from the processor's memory under a `syncing` guard so a sync never writes back. Hold and Release read in
ms at the current tempo (`Processor::cycleSeconds`), Attack in its stored ms.

`ui/DuckArt.h` holds the plates, recessed wells, screws, a 128 px grain tile made once per editor, and the rubber duck
as vector paths (body, wing, head, beak, a horn on the crown, a scowl). The duck's head is the brand mark. The palette
in `ui/DuckTheme.h` follows the handoff's `palette.json`. The editor is two units: `src/DuckEditor.cpp` (construction,
wiring, refresh, menus) and `src/DuckEditorPaint.cpp` (plates, brand, readings, dividers). The editor's 30 Hz tick
still repaints the scope and follows host-moved parameters.

## The meter

`ui/DuckMeter.h` is a real gain-reduction meter: an opaque component on its own 60 Hz timer that repaints only while
something moves. Each frame it takes `Engine::takeDeepestGain()` and `cycleCount()` ([the engine](rhino-duck.md#the-engine)).

- `meterDepthForDb` spaces 0, -3, -6, -12, -18 dB and -inf evenly down the scale; -18 to -48 dB fills the last fifth.
- A spring (stiffness 900, damping 38: about 50 ms, one small bob) moves the duck; tilt is at most 1.5 degrees and
  squash 2%. **The waterline never moves**, only the duck.
- A cycle that ducks starts a ripple; the figure holds its deepest reading 0.5 s; with no block for 0.25 s the duck
  surfaces.

## The scope

`src/DuckScopeView.cpp` draws in the handoff's layer order: recessed well, grid (sixteenths faint, quarters firm),
sloped-part tint, kick (`#9C5D35`), dry (`#989DA5`) and out (`#D0D3D8`) overlaid, a faint fill under the curve, the
curve, two handles (Hold at the sloped part's start, Release at its end; dragging the part itself still moves Hold),
the End marker in one-shot mode, the playhead, a KICK/DRY/OUT legend in a 22 px strip above the plot, and reset.

## Tests and snapshots

`duck_layout` checks that the new parts sit inside the panel and apart, the scope's minimum size, that the handles are
grabbable, that the strip edits the chosen curve both ways (knob to memory, memory to knob and its ms reading), the
meter's mapping and settling, a fixed waterline, no repaint at rest, and that the editor feeds the meter.
`RhinoDuckTests --snapshot out.png [SYNC|MIDI|AUDIO] [BAND] [SOLO] [ONESHOT] [SHAPED] [CURVE=n] [SECONDS=s] [SCALE=f]`
steps the meter one frame per rendered block, so the duck is mid-dip in the picture
([Seeing the UI without taking the screen](headless-ui-snapshots.md)).

## Decisions

- **The preset bar chooses among the sixteen curves**; there are no preset files or preset system (the user: "there's
  16 presets right now").
- **The duck's head is the brand mark.** The handoff's SVG mark was tried and read as a blob at logo size.
- **The band keeps one cutoff**; see [Rhino Duck's decisions](rhino-duck.md#decisions).

## Related

- [Rhino Duck](rhino-duck.md)
- [Displays draw from the DSP](displays-draw-from-the-dsp.md)
- [Seeing the UI without taking the screen](headless-ui-snapshots.md)
- [Rhino Forge](forge.md)
