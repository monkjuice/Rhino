# Theta Forge: synth build-out plan

Forge's long-term north star is the [Serum 2 manual](https://xferrecords.com/web-manual/serum-2/welcome)
— its module layout, its routing model, and its drag-a-modulator-onto-any-knob
workflow. Forge takes inspiration from that structure. It reuses no Serum code,
assets, names, presets, or artwork.

This plan covers **the synth only**. Effects are deliberately out of scope until
the synth is finished; see [Out of scope](#out-of-scope-for-now).

The delivery plan for Theta's instruments as a whole lives in
[INSTRUMENT_PLAN.md](../../INSTRUMENT_PLAN.md). This file supersedes its
"Milestone B, item 4" for everything synth-side.

## Where Forge stands today

The v0.1 engine works but mixes concepts. One flat array of 39 parameters is
laid out by index arithmetic across two pages, so:

- Oscillator knobs are spread over unrelated boxes; nothing can be switched off.
- Unison and detune are shared by both oscillators rather than owned by each.
- Oscillator A has no tuning at all and no level of its own — it is whatever is
  left over after Oscillator B's level.
- There are two envelopes (amp and filter) and no way to route either.
- Chorus and delay live inside the synth's signal path and inside its macros.
- Module titles and knob labels are drawn into the same strip, so they collide.

## Target architecture

Every block of the synth becomes a **module**: a bordered panel with a header
carrying an enable LED and a name, its own display where a display earns its
place, and its own knobs contained inside it. Nothing is laid out by index
arithmetic; modules declare their contents.

```
+- FORGE -- [ OSC ][ MATRIX ] ------------------ preset -- LOAD SAVE --+
| +-- OSC A ---------------------+ +-- OSC B ---------------------+    |
| | [ waveform ]                 | | [ waveform ]                 |    |
| |   OCT    SEMI    FINE        | |   OCT    SEMI    FINE        |    |
| | POS UNI DET BLEND PAN LEVEL  | | POS UNI DET BLEND PAN LEVEL  |    |
| +------------------------------+ +------------------------------+    |
| +- SUB -+ +NOISE+ +- FILTER --------+ +- GLOBAL ---------+ +MACROS+  |
| | LEVEL | | LVL | | TYPE [A][B][S][N]| | POLY MONO LEGATO | | 1  2 |  |
| |  (o)  | | (o) | | CUTOFF RES DRIVE | | GLIDE  OUTPUT    | | 3  4 |  |
| +-------+ +-----+ +------------------+ +------------------+ | 5  6 |  |
| +-- ENV 1 -------------------+ +-- LFO 1 ----------------+ | 7  8 |  |
| | [ ADSR curve ]             | | [ shape + phase ]       | |      |  |
| | ATTACK DECAY SUSTAIN RELEA | | RATE                    | |      |  |
| +----------------------------+ +-------------------------+ +------+  |
+----------------------------------------------------------------------+

The MATRIX tab puts the matrix in the top row in place of the two oscillators.
Nothing below that row moves.

+- FORGE -- [ OSC ][ MATRIX ] ------------------ preset -- LOAD SAVE --+
| +-- MATRIX ------------------------------------------- 8 SLOTS ---+  |
| |  #   SOURCE        AMOUNT              DESTINATION              |  |
| |  1  [ MACRO 2 ]   [-----|======  ]    [ A LEVEL ]               |  |
| |  2  [ ENV 1   ]   [  ===|------  ]    [ A POS   ]               |  |
| |  3  [ OFF     ]   [-----|------  ]    [ OFF     ]   ... eight   |  |
| +-----------------------------------------------------------------+  |
+----------------------------------------------------------------------+
```

### Decisions locked in

| Question | Decision |
| --- | --- |
| How many envelopes | One. It is hardwired to the voice amplitude, exactly as Serum's ENV 1 is. ENV 2–4 are auxiliary modulators you drag onto knobs, and arrive with the modulation matrix, not before. |
| Filter envelope | Removed. The filter is its own module and owns no envelope. |
| Macros | Removed. They are hardwired parameter offsets today, which is the wrong shape. They return as real assignable macros with the modulation matrix. |
| Effects | Removed from the synth path. Chorus, delay, distortion and the rest return as a proper FX rack after the synth is done. |
| Build order | Structure and look first, voice rebuild second. |
| Old presets | Not supported. Format 1 files are refused rather than migrated. Within format 2, presets are still reconciled against whatever parameters exist when they open, so a preset saved at one milestone keeps working at the next. |

### Known consequence of removing the filter envelope

Until the modulation matrix lands, the only thing that can move the cutoff is
LFO 1's `> CUTOFF` depth. Plucks and filter sweeps that relied on the filter
envelope will sound flatter in the interim. This is accepted deliberately: a
hardwired second envelope is the thing being replaced, and rebuilding it now
would be thrown away by M6.

## Milestones

Each milestone builds in Release, passes `ctest`, and leaves Forge loadable in
both the standalone app and Theta.

### M1 — Strip Forge back to a synth

Foundation. Nothing new to look at; everything after this gets easier.

- Delete `src/ForgeVoice.h` / `src/ForgeVoice.cpp`. They are dead: absent from
  `CMakeLists.txt` and included by nothing.
- Remove the chorus and delay DSP and their six parameters.
- Remove the four macro parameters and their hardwired offset mapping.
- Remove the filter envelope: `filterAttack`, `filterDecay`, `filterSustain`,
  `filterRelease`, `filterEnvAmount`.
- Retire the `MOTION / FX` page. Forge is one page until the FX rack exists.
- Preset format version 2. Format 1 files are refused: they describe a synth
  that no longer exists. Within format 2, a preset is reconciled against the
  parameters that exist when it opens — unknown entries are dropped and
  parameters the preset predates return to their defaults — which is what keeps
  presets working across the milestones still to come.
- Fix the knob readouts. `SliderAttachment` overwrites the editor's
  `textFromValueFunction`, which is why every knob reads `0.5500000`. Formatting
  moves into the parameter layout via `AudioParameterFloatAttributes`, so the
  host's automation lane and Forge's own knobs agree.

**Tests:** format 1 and future versions are both refused; a partial preset
restores what it names and defaults what it omits; retired keys are pruned on
re-save; parameter text formatting is correct for every unit.

### M2 — Module framework, the pedal look, and enables

The first milestone with something to look at.

- Introduce declarative module descriptors (`ui/ForgeLayout.h`): a module owns a
  title, an accent, an optional display, and rows of knobs. The editor walks the
  descriptors; no call site computes a cell index.
- Every module header reserves its own strip, so titles and knob labels stop
  overlapping.
- Add enable toggles: `oscAEnable`, `oscBEnable`, `subEnable`, `noiseEnable`,
  `filterEnable`. A disabled module is visibly dimmed and is skipped in the
  voice render, not merely zeroed.
- `SUB` and `NOISE` become single-knob pedals of their own.
- Every knob on the panel is drawn at one size, taken from whichever module has
  the least room, so a module with one knob does not dwarf a module with five.

**Tests:** disabling a source removes it from the render; a disabled module's
parameters still save and restore; no two modules overlap and none escapes the
content area at any allowed window size; every declared knob id resolves to a
real parameter and every parameter appears somewhere on the panel.

**You can test this one by eye.**


### M3 — Per-oscillator architecture

- Split shared `unison` / `detune` into `oscAUnison` / `oscADetune` and
  `oscBUnison` / `oscBDetune`.
- Give each oscillator its own `Level` and `Pan`. Oscillator A stops being
  "whatever is left over after B".
- Give each oscillator `Octave`, `Semitone` and `Fine` tuning, replacing the
  single `oscBTune`.
- Add `Blend` per oscillator (unison centre-versus-spread balance).
- Each oscillator draws its own display from its own position.
- The layout gains control **rows** with styles, so the tuning strip can sit
  above the knob row as a set of compact numeric fields rather than three more
  knobs. `GLOBAL` moves up beside `FILTER`, which the extra oscillator height
  pays for.
- `Patch` gains an `Oscillator` sub-struct and is built by name rather than
  positionally, so the parameter list and the patch layout no longer have to be
  kept in the same order to stay correct.

**Tests:** tuning arithmetic and the frequency the voice actually produces;
equal-power pan law, and hard-left leaving the right channel empty; level
scaling linearly; stacking voices not changing the oscillator's level; one
oscillator's controls never reaching the other.

### M4 — One filter, with source routing

- `routeA`, `routeB`, `routeSub`, `routeNoise` chips in the filter module,
  labelled the way Serum's `S A B C N` buttons are.
- The voice accumulates two buses: sources routed through the filter, and
  sources that bypass it straight to the voice sum.
- `Drive` moves out of the global path and into the filter path, so it only
  touches what is routed there, and switching the filter module off bypasses
  the drive with it. M3 had already made drive honest at zero — the output
  stage no longer saturates when drive is off, and a soft clipper that is
  linear below its knee does the bounds-keeping the old unconditional `tanh`
  was doing.
- Filter type: low-pass, high-pass, band-pass. A state-variable filter computes
  all three responses anyway, so this is a choice of tap rather than a second
  filter.
- Controls carry their own style, so one row can mix the type field with the
  routing chips.

**Tests:** cutoff attenuates a routed source and leaves an unrouted one
bit-for-bit alone; routing is per source, so closing the filter on one leaves
another; drive does nothing to a bypassed source, and nothing at all while the
module is off; every filter type renders finite audio and the low pass passes a
low note more than the high pass does.

### M5 — ENV 1 module with a live display

- One ADSR, hardwired to amplitude.
- The display draws the actual curve from the current A/D/S/R values and marks
  the live stage while a note sounds, with the header naming the stage.
- The engine publishes ENV 1's level and stage once per block; the audio thread
  writes two atomics, the message thread reads them, and nothing else crosses.
- `MONO` and `LEGATO` become rocker switches rather than knobs whose only
  readout is ON or OFF. The rocker actually rocks — the raised face swaps from
  top to bottom and the indicator travels with it — so the state reads from the
  shape as well as the light, and it carries no text of its own. It sits where
  a knob's circle sits, so the gap under its label matches the knobs' either
  side.
- `POLY` greys out while mono is on, because the engine already ignores
  polyphony there. Controls declare what disables them, which the modulation
  matrix will want again.

**Tests:** stage transitions at the expected sample offsets and the expected
levels between them; a note released mid-attack falls from the level actually
reached rather than from sustain, and still takes the full release time; mono
plays one note where poly plays three, and polyphony does nothing at all while
mono is on.

### M6 — Modulation matrix

**Moved ahead of LFO 1.** In Serum an LFO has no destinations of its own at
all: it is a source you drag onto a knob, and the same LFO can drive many knobs
at different depths. Forge's `> CUTOFF`, `> POSITION` and `> PITCH` knobs are a
placeholder for that, not a design — they exist only so the synth has some
modulation while the filter envelope is gone. Building LFO shapes and sync
against three hardwired destinations would mean designing the LFO module twice,
and ENV 2–4 cannot exist at all until this lands.

Delivered in two parts. **M6a, the engine and the panel, is done.**

**M6a — slots, engine, matrix panel**

- Eight slots, each three parameters (source, target, depth), so a host can
  automate a routing as readily as a knob and the whole matrix saves with a
  preset without a separate serialisation path.
- Sources: ENV 1, LFO 1, velocity, note. ENV 2–4, LFO 2–6 and macros follow
  once the mechanism exists.
- Fifteen destinations, covering both oscillators' position, level, pan, detune
  and pitch, the sub and noise levels, and the filter's cutoff, resonance and
  drive. `Output` is deliberately not one: it is applied once after the voices
  are summed, so a per-voice modulation of it would not mean anything.
- Modulation happens in the destination's own normalised space, so one depth
  control behaves the same whether it points at a percentage, a frequency with
  a skewed range, or a pan position. The Processor hands the Core each
  destination's range at prepare time, so no range is defined twice.
- Offsets accumulate per destination and are applied once, not slot by slot.
  Applying them in turn would round-trip through the range between slots, so
  two half-depth slots would not add up to one at full depth, and an early slot
  hitting a limit would swallow a later one pulling the other way.
- It is per voice and per sample, because every source except the LFO is per
  voice and every destination is read inside the voice. With no live slot the
  patch is used as it stands and nothing is copied.
- The three `>` depth knobs are gone. LFO 1 is a source with a rate and nothing
  else. `Semitone` became a continuous parameter so pitch can be swept smoothly
  through it, while its field still snaps to whole semitones under the hand.

**M6b — macros, drag to knob, keyboard** — done

- Eight macros, two across and four down in a column at the right-hand edge,
  standing beside GLOBAL and LFO 1 rather than under them. They are smaller
  than the controls they drive, and sources only: a macro reaches a control
  through a slot or not at all. A module can declare its knobs compact, which
  both keeps eight small knobs from shrinking every knob in Forge to match and
  holds them below the diameter the rest of the panel shares.
- Every source carries a drag handle that is **not** the control itself —
  dragging a knob has to keep meaning "turn this", so the grab point is the
  numbered tag beside a macro, and the named tag in ENV 1's and LFO 1's
  headers. A line follows the cursor and the knob under it is outlined, so a
  drop lands where it looks like it will.
- Right-clicking any knob the matrix can reach lists the sources, and offers to
  take away anything already pointed there.
- A modulated knob carries a ring showing how far its slots can move it, drawn
  outside the value arc so the two never read as one. What that ring shows grew
  in M6e, below.
- An eighty-eight key keyboard across the bottom, playing through the same path
  the host's own notes take.

A new routing lands at half depth rather than at zero, which would be the
correct value but would look like the drop had done nothing. M6c, below, is how
it gets moved off that.

**M6c — draggable depth rings** — done

- A press that lands on a knob's ring sets the depth of the slot pointed there;
  a press inside the body still turns the knob. Splitting them by where the
  press lands is what lets the ring become a control without the knob losing the
  gesture a hand already knows. The ring band is the clear air outside the body,
  so the two never overlap.
- Dragging up is more and down is less, with the whole bipolar range in 200
  pixels, so a depth can be taken from one sign to the other without letting go.
  Double-clicking the ring returns it to no depth, mirroring what a double-click
  on the knob does to its value.
- The ring lifts under the cursor. Without that, nothing tells you it is a
  control rather than a reading.
- Depth is written through the parameter, so a dragged ring reaches the host's
  automation lane and the matrix field by the same path a depth typed into the
  matrix does. The matrix updates as the ring moves.
- **Known limit:** a ring is only draggable while exactly one slot points at
  that knob. Pointed at by two, the ring is their sum, and there is nothing a
  single drag could honestly mean. Those are edited in the matrix, or taken
  apart from the knob's own right-click menu. A slot sitting at zero depth still
  counts, so a routing can be dialled up from nothing by its ring.

**M6d — the matrix tab** — done

- The matrix moved out of the bottom of the panel and onto a tab of its own,
  which it shares with the oscillators. Tabs switch that one row and nothing
  else, so the filter, the envelope, the LFO and the macros stay reachable
  while a routing is being made. Losing the fourth grid row is what paid for
  the macro column and for a shorter window at every resize limit.
- The matrix reads as a table rather than as a grid of fields: one row per
  slot, numbered down the side, with the amount between the source driving it
  and the control it moves. Column titles are drawn once above the rows, so no
  field carries a label of its own.
- Amount became a horizontal bar that fills out from zero, so the sign and the
  size of a depth read across the row without the number being looked at.
- A slot's number lights once the slot has both ends, which makes the routings
  in use countable at a glance.

**M6e — modulation shown on the knob it is driving** — done

- A modulated knob now draws two things on its ring: the reach, faint, because
  how far a slot *could* move the knob is potential rather than a value; and
  where the modulation actually has it this instant, bright, with a marker at
  the end. The second arc fills and empties as the source plays, so an envelope
  or an LFO is visible on the knob it is driving and not only in the module it
  comes from.
- The pointer stays where the parameter is set. Moving it would make the knob
  argue with its own readout and with the host's automation lane; the ring is
  what moves, exactly as Serum does it.
- The engine publishes the offset per destination the same way it already
  publishes ENV 1's level: the reading is taken from the loudest sounding voice,
  the audio thread stores it once per block, and the message thread reads it.
  Nothing new crosses between the threads and nothing is computed twice — the
  voice renders with the offsets that get published.
- The animation runs only while something is sounding, exactly as ENV 1's
  playhead does. A source reaches a destination through a voice, so with no
  voice there is no modulated value to draw, and a patch making no sound does
  not animate. This holds for a macro too, which is the case that looks most
  like it should be an exception: the hand is on the macro, but until a note is
  played the macro is moving nothing. The faint reach stays drawn throughout, so
  a knob still shows that it is wired to something.
- Whether the panel is live is asked of the panel rather than inferred from the
  offset being non-zero. An LFO passes through zero twice a cycle, and a marker
  that blinked out each time it did would read as a fault rather than as a
  crossing.

**Tests:** an idle matrix publishes nothing; a destination nothing points at
publishes nothing, and one a slot has left goes back to publishing nothing; at
full depth from ENV 1 the published offset is bit-for-bit the same reading ENV
1's own display draws, so the ring and the curve cannot disagree; a unipolar
source at full depth never publishes past full travel; and a patch with nothing
sounding publishes nothing at all, while the same patch under a note does, so
that check is measuring silence rather than a routing that was never live.

**Tests:** a slot at zero depth, and a slot pointed at nothing, both render
bit-identically to no slot at all; two half-depth slots sum exactly to one at
full depth; switching a source off restores the plain render exactly;
modulating a parameter already at its maximum changes nothing and stays finite;
a harder note opens a velocity-driven filter further, measured as brightness so
it cannot be satisfied by a hard note merely being louder.

### M7 — LFO 1 module with a live display

Built once, as a real source, after the matrix exists.

- Shape selection: sine, triangle, saw, square, sample-and-hold.
- Rate, with tempo sync against the host.
- The display draws the shape with a running phase indicator.

**Tests:** each shape is bounded and periodic; tempo sync tracks a BPM change.

### M8 — Interaction and preset polish

- Resize behaviour at every supported size, with no module clipping.
- Knob interaction: scroll, fine drag, double-click to default, right-click menu.
- Tooltips on every control.
- Refresh both bundled presets against the new parameter set and add a small
  set of init variants that show each module off.

## Out of scope for now

These are the north star, not this plan. They come after the synth is finished.

- **M9 — Real wavetables.** Loadable tables, a table editor, and frame
  interpolation replacing today's four-frame analytic morph.
- **M10 — ENV 2–4, LFO 2–6 and macros 1–8** as further matrix sources.
- **M11 — FX rack.** Chorus, distortion, delay, reverb, compressor, EQ, in a
  reorderable chain.
- **M12 — Second filter,** with the serial/parallel routing Serum exposes.
- **M13 — Preset browser** with tags and search.
- **Later still:** MPE, sample and granular sources, spectral oscillators.

## Building and testing

```powershell
cmake -S instruments/theta-forge -B instruments/theta-forge/build -G "Visual Studio 17 2022" -A x64
cmake --build instruments/theta-forge/build --config Release --parallel 2
ctest --test-dir instruments/theta-forge/build -C Release --output-on-failure
```

Three CTest cases run from one binary, selected by argument, as Theta's own
tests do: `forge_layout` checks the panel geometry and that the declared layout
and the parameter list agree, `forge_presets` checks saving, loading and
reconciliation, and `forge_engine` renders audio and checks the module enables,
the filter bypass, and that output stays finite and inside full scale.

The standalone build at
`build/ThetaForge_artefacts/Release/Standalone/Theta Forge.exe` is the quickest
way to look at a change.

## Compatibility notes

- Theta discovers Forge by plugin name in `SessionExternalPlugins.cpp`; it never
  names a Forge parameter. Parameter changes therefore cannot break Theta's
  hosting, only the Forge state stored inside an already-saved `.thetaedit`.
- Retired parameters are dropped from saved state rather than kept as ballast,
  and parameters a preset predates load at their defaults. A preset saved at one
  milestone therefore still opens at the next while the control set is moving.
  This applies to host state too, so a Theta project holding older Forge state
  opens without carrying dead parameters.
- No allocation, locking, or filesystem work in `renderSample`. This is a hard
  gate on every milestone.

## Status

| Milestone | State |
| --- | --- |
| M1 Strip back to a synth | **done** |
| M2 Module framework and enables | **done** — ready to test by eye |
| M3 Per-oscillator architecture | **done** — ready to test by ear |
| M4 Filter routing | **done** — ready to test by ear |
| M5 ENV 1 | **done** — ready to test by ear |
| M6a Matrix engine and panel | **done** — ready to test by ear |
| M6b Macros, drag to knob, keyboard | **done** — ready to test by ear |
| M6c Draggable depth rings | **done** — ready to test by hand |
| M6d Matrix tab | **done** — ready to test by eye |
| M6e Modulation shown on the knob | **done** — ready to test by eye |
| M7 LFO 1 | not started |
| M8 Polish | not started |
