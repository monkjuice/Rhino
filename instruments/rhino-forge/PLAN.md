# Rhino Forge: synth build-out plan

Forge's long-term north star is the [Serum 2 manual](https://xferrecords.com/web-manual/serum-2/welcome)
— its module layout, its routing model, and its drag-a-modulator-onto-any-knob
workflow. Forge takes inspiration from that structure. It reuses no Serum code,
assets, names, presets, or artwork.

This plan covered **the synth only** for its first eleven milestones. Effects
were deliberately out of scope until the synth was finished, and arrived at M11a
once the mixer's busses had somewhere to send to; the rest of the rack, and the
things still absent, are in [Out of scope](#out-of-scope-for-now).

The delivery plan for Rhino's instruments as a whole lives in
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
+- FORGE -- [ OSC ][ TABLE ][ MATRIX ] ---------- preset -- LOAD SAVE --+
| +-SUB-+ +NOISE+ +-- OSC A ---------+ +-- OSC B ---------+ +-FILTER----+ |
| |LEVEL| | LVL | | [ waveform     ] | | [ waveform     ] | |[ response ]| |
| | (o) | | (o) | |  OCT SEMI FINE   | |  OCT SEMI FINE   | | TYPE ABSN  | |
| |     | |     | | POS UNI DET ...  | | POS UNI DET ...  | | CUT RES DR | |
| |     | |     | | (o)[MOD][MOD](o) | | (o)[MOD][MOD](o) | |            | |
| +-----+ +-----+ +------------------+ +------------------+ +-----------+ |
| +GLOBAL+ +-- ENV -------------+ +-- LFO -------------+ +-MACROS-+      |
| |POLY GL| | [ ADSR curve    ] | | [ shape + phase ]  | | 1    2 |      |
| |MO LEG | | [1][2][3][4]      | | [1][2][3][4][5][6] | | 3    4 |      |
| |OUTPUT | | ATK DEC SUS REL   | | SHAPE MODE UNIT RT | | 5    6 |      |
| |       | |                   | |                    | | 7    8 |      |
| +-------+ +-------------------+ +--------------------+ +--------+      |
| [==================== eighty-eight keys ============================ ] |
+----------------------------------------------------------------------+

The signal row is taller than the modulation row to match the metal chassis
reference. The MATRIX tab puts the matrix in the
oscillators' columns, and the TABLE tab puts the wavetable editor there. SUB,
NOISE and FILTER hold their places either side of it, and nothing in the lower
row moves.

+- FORGE -- [ OSC ][ TABLE ][ MATRIX ] ---------- preset -- LOAD SAVE --+
| +-SUB-+ +NOISE+ +--- MATRIX ----------------- 8 SLOTS --+ +-FILTER----+ |
| |LEVEL| | LVL | |  #   SOURCE      AMOUNT    DESTINATION| |[ response ]| |
| | (o) | | (o) | |  1 [ MACRO 2 ] [--|===  ]  [ A LEVEL ]| | TYPE ABSN  | |
| |     | |     | |  2 [ ENV 1   ] [ =|---  ]  [ A POS   ]| | CUT RES DR | |
| |     | |     | |  3 [ OFF     ] [--|---  ]  [ OFF     ]| |            | |
| +-----+ +-----+ +---------------------------------------+ +-----------+ |
+----------------------------------------------------------------------+
```

### Decisions locked in

| Question | Decision |
| --- | --- |
| How many envelopes | Four, as of M10b. ENV 1 is hardwired to the voice amplitude, exactly as Serum's ENV 1 is; ENV 2–4 are auxiliary modulators you drag onto knobs. They deliberately arrived after the modulation matrix rather than before it, because there was nothing for them to reach until it existed. |
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

**Closed at M10b.** A filter envelope is now an ordinary patch: point ENV 2 at
`CUTOFF` in a slot. It is better than the one that was removed, because the same
envelope also reaches the drive, either oscillator's position or anything else
the matrix carries, and because a patch that wants no filter envelope does not
pay for one.

## Milestones

Each milestone builds in Release, passes `ctest`, and leaves Forge loadable in
both the standalone app and Rhino.

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
  which it shares with the oscillators. Tabs switch those columns and nothing
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

### M7 — LFO 1 module with a live display — done

Built once, as a real source, after the matrix exists.

- Shape selection: sine, triangle, saw, square, sample-and-hold. Every shape is
  bipolar and uses the full swing, because a slot's depth is what decides how
  much of it reaches anything.
- `lfoWave` is a free function beside the shapes rather than a method on the
  engine, so the panel draws the very curve the voice is reading — the same
  arrangement `morph()` already has with the oscillator display.
- Rate, with tempo sync against the host. `SYNC` switches the module between a
  free rate in Hertz and a division of the host's tempo, and whichever one is
  not in charge greys out. Sync is resolved to a rate in Hertz before the patch
  is built, so the Core still never sees a tempo. A host reporting no tempo is
  stood in for at 120, so a synced LFO in a standalone runs at a musical rate
  rather than stopping dead.
- The module header carries the rate the LFO is **actually** running at. In sync
  that is a tempo division, which cannot be read off the greyed-out rate knob.
- The display draws one cycle across its width with the engine's own running
  phase riding it. One cycle rather than several, so the width of the display is
  the length of the cycle and the indicator's position is the phase, read
  directly. Two cycles left the indicator stuck in the left-hand half, because a
  phase only ever covers one of them.
- Sample-and-hold draws its held step as a flat line across the display rather
  than a row of invented steps. The jump is seen rather than drawn: the line
  lifts to a new height as each cycle turns over, which is what sample and hold
  looks like when watched. Drawing invented steps put the indicator on a curve
  the voice was not reading.
- A control can now declare `enabledBy` as well as `disabledBy`, since the rate
  and the division each need to grey out under the opposite setting of the same
  switch. A stepper sitting in a row with knobs now lines its label up with
  theirs instead of floating in the middle of its cell.

**Tests:** every shape is finite, stays inside plus or minus one, and uses the
range it is given; no two shapes are the same curve; sine and triangle join up
across the cycle while a saw and a square jump a full swing there, so neither can
be quietly smoothed away; sample and hold holds its step for a whole cycle; an
unsynced LFO runs at its knob; a synced one divides the host tempo, tracks a
tempo change rather than latching the first one it saw, and a whole-bar division
is four beats long; the published phase stays inside one cycle and advances.

### M8 — Interaction and preset polish — interaction done, presets outstanding

- **Resize behaviour at every supported size, with no module clipping.** The
  layout test now sweeps the whole allowed range in twenty-pixel steps rather
  than sampling the two corners — a thousand-odd sizes — and checks at each one
  that no module escapes the content area, that no two modules shown together
  overlap, that every control lands inside its own module, and that knobs stay
  usable. Integer division means the geometry can go wrong at one awkward size
  while both extremes are fine, which is exactly what an eye test misses. Every
  failure names the module and the size.
- **Knob interaction.** Scroll works, double-click returns a control to its
  default, and right-click offers the modulation menu; all three were already
  there and are now confirmed by hand rather than assumed. Fine drag is new:
  holding Shift or Ctrl stretches a whole range from 250 pixels of travel to
  1400, and stretches a modulation ring's drag the same way. Measured — the same
  sixty-pixel gesture on the cutoff moves it to 18.00 kHz plain and to 9.58 kHz
  fine.
- **Tooltips on every control.** Every control had carried one since M2, but
  there was no `TooltipWindow` anywhere in the editor, so not one of them had
  ever been shown. Adding it is the whole fix. The table moved out of the editor
  into `ui/ForgeTooltips.h` so the layout test can hold it to the same standard
  it holds parameter ids to: every declared control and every module enable must
  have a tooltip, and it must be a sentence rather than a placeholder. That
  caught the gaps — the module enables, and everything LFO 1 gained in M7.
  Tooltips are drawn dark enough to read against the panel.

**Still outstanding:** the bundled presets. There are none in the repository to
refresh — the line above was written when there were — so this is now "author a
small factory set that shows each module off", which wants an ear rather than a
build, and is left for whoever has one.

### M9a — ten shapes in the oscillator's table — done

The oscillator had four frames — sine, triangle, saw, square — and POSITION
crossfaded between them. That is a morph, not a table. It now holds ten, and
they can be chosen rather than approached.

- **The ten, in order:** `SINE`, `TRI`, `TRAP`, `SQR`, `PULSE` (quarter),
  `THIN` (tenth), `SAW`, `HUMP`, `ORGAN`, `VOX`. The order is as much of the
  design as the contents: POSITION crossfades whichever two frames it falls
  between, so what sits next to what is what the in-between positions sound
  like. Each frame is a relative of the one before it — a triangle flattens into
  a trapezoid into a square, a square narrows into a pulse and then into a
  sliver — rather than the list being sorted by name or by when it was thought
  of.
- The frames are analytic, so one costs a few operations and no memory. Only the
  two either side of the position are worked out, so the cost does not grow with
  the size of the table — which is the property that has to hold before a table
  can sensibly get bigger.
- Every frame is bipolar and reaches full scale. If one were quieter than the
  rest, sweeping POSITION across it would dip the oscillator's level, and a
  morph would have a hole in it.
- `waveAt` is a free function, so the tube draws the very curve the voice is
  reading. It was two separate copies of the same maths before, one in the
  engine and one in the panel, which could drift apart with only an ear to
  notice.
- **POSITION now reads out the shape** — `SAW`, or `SAW>HUMP` between two —
  instead of a percentage. A percentage never answered the only question anybody
  asks a wavetable knob, which is "where is the saw?".
- **A gesture lands on a frame.** Dragging or scrolling POSITION snaps to the
  ten; holding the fine modifier goes between them on purpose. Only the hand is
  affected — JUCE asks `snapValue` about a value a gesture arrived at, never
  about one from the host or the matrix, so a modulated position still sweeps
  the table smoothly. The mouse wheel needed its own handling: a notch moves a
  knob by less than a frame is wide, so snapping rounded every notch straight
  back and the wheel did nothing at all. One notch is now one frame.

**Consequences, all deliberate:** a POSITION saved before this names a different
shape now, because the table it indexes into has changed underneath it — the saw
is the exception, and only by luck, since it sat at two thirds with four frames
and sits at two thirds again with ten. The oscillator defaults moved onto real
frames, `SAW` and `TRI`, rather than part-way between two. And the aliasing is
untouched and now has more shapes to spoil: `PULSE` and `THIN` are the worst of
them, being nothing but edges.

**Tests:** every frame is finite, stays inside plus or minus one and reaches full
scale; no two frames are the same shape, checked against every other frame
rather than only its neighbour; a position on a frame reads that frame exactly
and is named after it; a position between two names both; and morphing is
continuous in position, so modulating POSITION cannot click.

### M9b — Real wavetables

Picking this up cold? [HANDOVER-M9B.md](HANDOVER-M9B.md) is the working note
that goes with this milestone: what the code now looks like, and what will bite.

M9a gave the oscillator ten frames to choose between, but they were still
formulas evaluated per sample. A formula cannot be read from a file and cannot
be drawn on, so this is where a table stops being computed and starts being
data. Three parts; the first is done.

#### M9b-1 — a real table behind the oscillator — done

- **`core/ForgeWavetable.h` is new.** A `Wavetable` holds frames of 2048 samples
  and the band-limited copies of each. `Oscillator` carries a `const Wavetable*`
  and the voice reads it; null means the built-in ten, so a `Core` still needs no
  setting up to make a sound and every existing call site still compiles.
- **The ten are now generated, not evaluated.** `waveShape` is still the same
  ten formulas and is still where a frame is authored, but it runs once at
  startup to fill `builtInWavetable()`. Nothing calls it from the render any
  more. `waveAt`, `waveLabel` and `waveFrameAt` keep their signatures and read
  that table, so the panel, the knob readout and the tests were untouched.
- **Band-limiting, which Forge has never had.** A frame with an edge in it is a
  harmonic series running past Nyquist, and everything above it folds back down
  as a whistle moving the wrong way. Every frame is now stored eleven times,
  each copy with half the harmonics of the one before and half the points to
  match, and a note reads whichever copy has the most harmonics that still fit.
  The copies are cut with a transform rather than a filter, so the cut is exact
  and leaves no ripple. Measured on a saw at 4186 Hz: the worst fold-back falls
  from 0.089 to 0.0008, about 41 dB. The whole set of levels costs a little over
  twice the table itself, not eleven times it.
- **Level 0 is the frame exactly as authored**, and it is what the panel draws.
  So M9a's guarantee holds in the form that matters — the tube draws the table,
  not a formula that might drift from it — but it is no longer literally the
  same samples the voice reads, because which copy the voice reads depends on
  the note. That is the price of not aliasing, and it is the right trade.
- **Reading between points is Catmull-Rom, not linear.** A straight line between
  two points of a 64-point frame is a different curve from the frame, and that
  difference is broadband noise. The frames are powers of two long, so the wrap
  is a mask rather than a branch.
- **The saw's jump moved to the middle of its frame.** It was `phase * 2 - 1`,
  which puts the single discontinuity exactly at the frame boundary where no
  display can show it — so the panel drew one diagonal, which is not what a saw
  looks like. It is now a rising saw that crosses zero at each end and takes its
  full swing across the centre, which is how Serum stores its own saw. Same
  harmonics, same sound, rotated half a cycle. The visible consequence is that
  SAW and its neighbours morph through different in-between shapes than before.
  LFO 1's saw is deliberately left alone: an LFO's ramp has to reset at the
  cycle boundary, because that is the point a synced LFO restarts on.
- `juce_dsp` is now linked, for the transform the band-limited copies need.

**Tests:** a band-limited sine is the same sine at the same amplitude at every
level, which is what catches a scaling mistake in the transform; each level
keeps the harmonics below its limit and has thrown away the ones above it;
level 0 is bit-for-bit what `waveShape` authored; the level a note is given
keeps its harmonics under Nyquist at all 128 notes; a high note folds back at
least four times less than the raw frame does; and the saw starts and ends its
frame at zero while swinging the full range across the centre.

#### M9b-2 — where a table comes from — done

- **The table's ownership and its hand-off.** `core/ForgeTableStore.h` is new
  and holds the whole of it. The Processor owns a `WavetableStore`; the audio
  thread picks up a table pointer once per block and uses it for the whole
  block; the message thread publishes a new one and only frees the old one once
  it can prove nothing is reading it. The proof is a counter the audio thread
  increments on the way into a block and again on the way out, so it is odd
  exactly while a block is running: even after a publish means the old table is
  unreachable and is freed on the spot, and odd means it is parked until the
  block that may hold it has finished. Both sides are sequentially consistent,
  because the argument rests on there being one total order over the publish and
  the counter read. Nothing on the audio thread allocates, waits or locks.
- **Reading a wavetable file.** An ordinary `.wav` of single-cycle frames end to
  end, from a file chooser or dropped on the editor. The frame size comes from
  Serum's `clm ` chunk when the file carries one — JUCE's wav reader does not
  surface it, so the RIFF is walked for it — and is 2048 otherwise. A file
  stored at another frame size is resampled onto Forge's with the same spline
  the oscillator reads a frame with. Chopping arbitrary recorded audio into
  frames is deliberately still not part of this.
- **Ten factory tables**, in [tables/](tables/), written by a script that lives
  beside them. They are the format everyone else uses, so they open in Serum and
  Vital too.
- **Presets and host state carry the table**, as a child node beside the
  parameter state rather than inside it: the frames deflated and base64'd, and
  written only when an oscillator's table has actually been drawn on or loaded
  over. A preset that names no table puts that oscillator back on the built-in
  ten, which is the rule an omitted parameter already follows. **The format
  version stays at 2** — deliberately, and against the letter of the M9b-1 note:
  the node is purely additive, every existing format-2 preset still opens
  unchanged, and bumping the version would refuse all of them to guard against
  an older build that nothing has shipped.
- **POSITION reads out against the table under it.** On the built-in ten it
  still names the shape, or the two it sits between. On a table somebody drew or
  loaded there are no shape names left to give, so it counts frames — `7 / 10` —
  and its detents follow that table's frame count.

#### M9b-3 — the table editor — done

A third tab beside OSC and MATRIX. The selected frame drawn large enough to draw
on, with the grid behind it and the area between the curve and the zero line
filled the way Serum and Vital both fill it; the table's frames in a strip
beneath it, each drawn the same way; freehand and line drawing; add, duplicate
and remove a frame; init and normalise; undo. Which oscillator is being edited
is a switch in the toolbar, so both tables are reachable from one tab.

The editor works on the frames as authored and asks the store to publish
afterwards, so what is drawn and what is played cannot come apart. A stroke
rebuilds only the frame it touched — the same table with one frame
re-transformed and the rest copied — which is what makes a stroke audible while
the hand is still moving. A test holds that cheap path to what rebuilding the
whole table gives.

The brush palette, the harmonic bars and the formula bar that Serum's editor
also carries are still explicitly not here.

### M9c — LFO 1 answers the keyboard — done

M7 built LFO 1 as a source with a shape and a rate. It free-ran and nothing
else: it never noticed a note, and the only way to set its rate in beats was a
switch called SYNC that read as an afterthought rather than as a unit.

- **MODE: TRIG, ENV or OFF.** TRIG restarts the shape on every new note and
  loops for as long as one is held. ENV restarts it too but stops on the last
  point of the shape and holds it, which turns any of the five shapes into a
  one-shot envelope of its own — a saw ends at the top, a triangle at the
  bottom, and whatever it drives stays there until the next note. OFF is what
  M7 had: it free-runs across notes, so a rate set in beats stays in step with
  the host from one phrase to the next.
- **TRIG is the default**, not OFF. An LFO that answers the keyboard is what a
  player expects of one, and it is the mode the other two are heard against.
  Presets written before this milestone therefore start retriggering, by the
  same rule every other omitted parameter follows.
- **A legato note in mono does not retrigger.** No key was lifted, so the LFO
  restarts exactly when the amp envelope does and not otherwise. One rule,
  expressed once, in `noteOn`.
- **A key-synced LFO only runs while something is sounding.** With no voice
  alive, TRIG and ENV wait at the start of the shape rather than free-running
  in the background — which is what makes the indicator mean anything: it sits
  where the next key press will start the shape from. A free-running LFO that
  the panel animated with nothing playing was showing a cycle no note had
  started and no note would join. OFF is unaffected; running with nothing
  playing is its whole job.
- **The release tail still counts as sounding.** A voice in release is audible,
  so the shape runs on through it. Cutting it dead the moment a key came up
  would jump whatever it drives while the tail is still there.
- **Stopping is the Core's business, not the panel's.** The Processor still
  resolves everything to a rate in Hertz and the Core still never sees a tempo;
  the mode is one more float on the patch, read through `lfoModeOf`. Taking the
  mode off ENV lets a stopped shape run on from where it stopped rather than
  leaving the panel showing a dead indicator.
- **RATE is one knob, and UNIT says what it counts in.** `lfoSync` is gone. HZ
  and BPM are two readings of one setting, so the two parameters behind them
  share a cell and only the one in charge is on screen — the knob does not move,
  the number under it changes from `0.50 Hz` to `1/4`. A switch called SYNC left
  a greyed-out knob sitting beside a live one and made the tempo reading look
  like a mode of the free one.
- **A control may now declare `sharesCell`.** It is the general form of that:
  the row is divided between its cells rather than between its controls, and a
  shared control hides rather than greys, because a greyed control would be
  sitting on top of the live one. `enabledBy` and `disabledBy` still decide
  which of the two it is.
- **The header names the division.** In beats the rate is a division of the
  host's tempo, which is the one reading the knob cannot give on its own, so
  the header reads `1/4 // 2.00 HZ`.

**Tests:** a key-synced LFO waits at the start until a note arrives, runs once
one is playing, and restarts on the next one; a note does not restart a
free-running LFO; TRIG keeps running through the release tail and comes back to
the start once the last voice has gone, and the panel is shown that same parked
phase; TRIG comes round again at the end of the cycle and keeps running; ENV
stops at the end of its shape, holds the value the shape ended on, stays stopped
however long it is left, and restarts on the next note; a legato note in mono does not
restart it while a mono note without legato does; every mode has a name of its
own. On the panel: the two controls in a shared cell are gated by one parameter
one each way round, and land on the same rectangle.


### M9d — voice stealing without a click — done

A run of notes longer than the polyphony left a tick on every note past the
limit, loudest with both oscillators on because both were cut at once. Two
things were wrong, and both had to be fixed to make it silent.

- **A new note took whichever voice a rotation had reached**, busy or not. So a
  third note in a two-voice patch silenced the note still under the player's
  finger while a finished voice sat beside it. It now takes a silent voice if
  there is one — still moving through them in turn, so successive notes do not
  all start from the same phases — and only when every voice is busy does it
  take one, choosing the quietest voice already in its release, or, if every key
  is still down, the quietest of those.
- **Taking a voice wiped it.** `voice = {}` reset the oscillator phases, the
  filter state and the envelope together, which stepped that voice's output
  straight to zero in a single sample. A voice that is still audible is now
  retuned instead of rebuilt: it keeps its phases, its filter state and the
  level its envelope has reached, and the attack starts again from that level.
  Amplitude, waveform and filter are continuous across the steal. The pitch
  jumps, which is the new note arriving rather than a click.
- A silent voice is still built from scratch, phase offsets and all, because
  there is nothing there to be continuous with.

**Tests:** the largest step from one sample to the next — which is what a click
is — measured over ten notes through four voices, against the same ten notes
through sixteen, where nothing is ever taken. On sine frames, so every
legitimate step is bounded by the pitch and anything larger came from the engine
cutting something off. Before: six times the reference. And the allocation
itself, as an invariant rather than an index check — while no more notes sound
at once than the patch has voices, raising the polyphony cannot change a sample.


### M9e — a voice is let go of, not cut off — done

With M9d's click at the start of a note gone, a second one was audible at the
end of one — loudest with the sub on, and on a patch with the cutoff near the
bottom of its range.

- **The amp envelope reaching zero is not the end of a voice.** Everything a
  voice makes is multiplied by that envelope and then passes through the
  filter, and a filter holds energy. At a low cutoff the voice is still ringing
  milliseconds after the envelope that fed it stopped — and the sub is what
  makes that ring loudest, a sine an octave down being exactly what a low
  cutoff passes. Dropping the voice at the envelope's zero truncated the ring,
  and a truncated ring is a click.
- **A finished voice is now faded out over 15 ms instead**, and the ring decays
  into the fade. Measured on the patch this was reported against: what was left
  at the cut went from 39 dB below the note's peak to 100 dB below it.
- **The fade is also what guarantees the voice comes back.** Waiting for the
  filter to fall quiet on its own would be at the mercy of the resonance; a
  fixed fade bounds it whatever the filter is doing.
- 15 ms because it covers half a cycle of the lowest cutoff the filter offers,
  which is the slowest thing it can be left ringing with.

**Tests:** on a patch with the cutoff near the bottom of its range and the sub
at full level, what is left of a voice at the moment it stops has to be more
than 66 dB below the peak of the note it came from. Cutting at the envelope's
zero leaves it 39 dB below, so the check fails against the old behaviour rather
than merely describing the new one.

Not the cause, though both were measured while looking for it: LFO 1 retriggering
under a note that is already sounding steps that note by about -75 dBFS, and the
corner it puts in the waveform is the largest in a run — but it is a corner, not
a step, because a cutoff change moves the filter's coefficient and not its state.
A per-voice LFO would remove it and is the right shape for M10's LFO 2–6; it is
not what was being heard here.


### M10a — six LFOs, each inside the voice — done

M7 built one LFO as a source. This makes six of them, and moves the ones that
answer the keyboard inside the voice, which is where the last of the clicks was.

- **An LFO in TRIG or ENV now runs inside each voice.** That is what those modes
  were always supposed to mean: a new note restarts *its own* copy of the shape
  and leaves the notes already sounding where they were. One shared cycle meant
  every key press jerked whatever the held notes were being driven by, part-way
  through them — measured at about -75 dBFS as a step, but a visible corner in
  the waveform, and the largest one in a run.
- **An LFO in OFF is still a single free-running cycle**, shared by every voice
  and by the panel. That is the whole of what OFF is for: it stays in step with
  the host across a phrase, so it cannot belong to a voice.
- **The "only runs while something is sounding" rule is gone**, because it is
  now simply what happens. A voice's LFO exists while the voice does; with
  nothing playing there is nothing to advance, and the panel is shown the start
  of the shape — where the next key press will begin it.
- **The panel follows the loudest voice**, exactly as ENV 1's display does and
  for the same reason: that is the note a player is listening to.
- **Six LFOs share one module, shown one at a time.** Six boxes side by side
  would not fit, and six that did would each be too small to read; Serum shows
  its eight the same way. Numbered buttons in the header choose which, the
  module's drag handle carries whichever is showing, and the banks that are not
  showing stay built and stay attached — so an LFO out of sight is still driven
  by the host and still running.
- **A row of controls may now be declared in banks.** The row is divided between
  one bank's cells and the rest stand on them, which is the same idea
  `sharesCell` already expressed for two controls and now generalised to six
  sets of five. Which bank is showing is the panel's business, not the host's:
  it says which LFO you are looking at, not what the synth is doing, so it is no
  more a parameter than which tab is open.
- **Only the LFOs anything reads are worked out per voice.** Six of them
  stepping through a sine for every voice would be five sixths of that work
  thrown away when one is routed. The phases still move either way, so an LFO
  nothing is pointed at yet is still one the panel draws running.
- **Two things moved, and old state is put right rather than left.** `lfoShape`
  and its four companions were named for the only LFO there was and are now
  `lfo1Shape` and so on; and five LFOs were inserted into the middle of the
  source list, so every saved slot source past LFO 1 moved up by five. A dropped
  parameter loads at its default and no harm is done, but a source index that
  quietly means something else is a slot pointed somewhere nobody asked for.
  `Processor::migrated` handles both, and keys off the presence of the old
  `lfoShape` so that running it twice does nothing the second time.

**Tests:** a second note leaves the first note's LFO running and does not jump
it; no two LFOs are the same cycle; each reports its own rate; each reaches the
matrix as a source of its own, checked by ear-equivalent — pointing it at the
sub and hearing the difference — for all six. On the panel: every bank declares
the same controls in the same order, no two banks name the same parameter, a
shared cell is shared inside one bank, and every bank of a source module names a
real source. And a state written before any of this: its LFO parameters come
back as LFO 1's, a slot on LFO 1 stays there while velocity, note and the macros
all land where they now live, and saving and reopening does not move them again.


### M10b — ENV 2–4, sources like any other — done

M5 built one envelope, hardwired to the voice's amplitude, and the decision
table above said the other three would arrive as auxiliary modulators once there
was a matrix to carry them. There is, and this is them.

- **Four envelopes, identical in every way but what reads them.** ENV 1 is still
  the amplitude and is still what says a voice is finished; ENV 2–4 reach a
  control through a slot or not at all. They are not a lesser kind of envelope
  with fewer stages or a coarser range: the same four knobs, the same ranges,
  the same defaults, because the difference between them is a matter of routing
  rather than of what an envelope is.
- **All four run in every voice, and all four answer the same key.** The note
  that starts ENV 1 starts them, the key lifting releases them, and a stolen
  voice carries all four across the steal from the levels they had reached — the
  same rule that already kept ENV 1 continuous. An auxiliary envelope that fell
  only when the amplitude did would be a shape with no release of its own.
- **They are updated whether or not anything reads them.** The LFOs are worked
  out only where they are wanted, because a phase moves on by itself and the
  wave can be evaluated on demand. An envelope's value *is* its state, so one
  skipped while nothing points at it would come back wrong the moment something
  did. Four of them is three extra additions per voice per sample.
- **The panel's ENV module became a bank of four**, exactly as the LFO module is
  a bank of six: numbered cards in the header, the drag handle carrying whichever
  is showing, the banks that are not showing still built, still attached and
  still running. The display, the stage in the header and the four knobs all
  follow the bank.
- **The zoom is per envelope.** The window is how long a shape is read against,
  and four envelopes are not the same length: a percussive ENV 3 read against the
  three-second window ENV 1 wants is a sliver, and one shared setting would hand
  you a display to re-zoom on every switch. Still a view setting, still absent
  from the state and from presets.
- **The header says what the envelope is when there is no stage to name.** Idle
  is not a stage — it is the envelope not running — so `stageName` now returns
  nothing for it and the panel answers instead: `AMP` for ENV 1, `SOURCE` for the
  three that go nowhere on their own.
- **Two things moved, and old state is put right rather than left.** `attack` and
  its three companions were named for the only envelope there was and are now
  `env1Attack` and so on; and three envelopes were inserted into the middle of
  the source list, so every saved slot source past ENV 1 moved up by three. This
  is the second such move — LFO 2–6 was the first — so `Processor::migrated` now
  says it once, as a rename and an insertion point, and runs the two oldest
  first. A state old enough to predate the LFO banks predates the envelopes too,
  whether or not it happens to name one, so the older mark implies the newer.

**Tests:** an envelope nothing points at renders bit-identically however its
knobs are set, which is the whole of "sources only"; each of the four opens a
filter it is pointed at; under one note ENV 1 sits in sustain while ENV 2 is
still climbing and ENV 3 has already fallen to nothing, which no single shared
shape could do; lifting the key releases ENV 2 from the level it had reached;
every envelope reads nothing once the voice has gone; and the offset published
for a knob's ring is the same reading ENV 2's own display draws. On migration,
both eras: a state from before either bank lands its slots where the two shifts
leave them, a state from between them has only the envelope shift applied, and
neither moves again when it is saved and reopened.

### M10c — the mixer, and two busses — done

Serum's mixer is the page where a patch is balanced: a channel per source, per
filter and per bus, each carrying where it goes, what it sends, where it sits
and how loud it is. Forge had the levels and two of the pans scattered across
the modules that owned them, no pan at all on the sub or the noise, no channel
for the filter, and no summing point anywhere. The MIX tab is where those become
one thing you can read across.

**What was decided, and why**

| Question | Decision |
| --- | --- |
| How much of the panel | The whole signal row. The mixer *is* the view of SUB, NOISE and FILTER, so those three stand down while it is open rather than sitting beside strips of themselves. This is the first tab that needs a module to be on some tabs and not others, which is why `Module::page` became a set of pages. |
| The per-source routing | Unchanged. `routeA`, `routeB`, `routeSub` and `routeNoise` are the same switches the FILTER module's A/B/S/N chips drive; the mixer draws each as a field that says FILTER or MAIN. One setting, two drawings, and no migration. |
| Muting a channel | The channel header's enable, which is the source's own — the same thing Serum's mixer header does. There is no separate mute and no `NONE` in the routing field, because switching the source off is what that would have meant. |
| A `DIRECT` output | Left out. Without an FX section DIRECT and MAIN are the same signal path, so the control could not be honestly explained, and the layout test requires every parameter be on the panel with a real tooltip. It arrives with the rack in M11. |
| What a bus is, before FX | A summing point with a level, a pan, and a destination of MAIN or the other bus. Audibly that is a gain — the racks that make a bus worth sending to are M11 — but the topology has to exist before a rack can hang on it, and a submix several sources share is a real thing to have meanwhile. |
| Where a bus is summed | Once, across every voice, rather than per voice. It makes no difference to a gain and it is what a rack will need: a reverb on a bus is one tail fed by every note, not a copy per note. |
| Two busses pointed at each other | Broken by the engine, not refused by the panel: the second of the pair goes to the main output instead. A setting the panel has to refuse is a setting a host can still automate into. |

**Two pan laws, deliberately**

A *source* pan spreads one source among the others and holds its power as it
moves, at the cost of 3 dB against the mono sum. A *channel* pan moves a sum
that is already balanced, so costing it that 3 dB would mean centring a channel
quietly turned it down; the channel law is the same curve normalised to unity at
centre. Both are in `ForgeCore.h` beside each other, and the mixer tests measure
the difference rather than asserting it.

**The one thing that changed under an existing patch**

SUB and NOISE were summed into both channels at full amplitude, while every
oscillator was summed at equal power — so those two were 3 dB hot against an
oscillator reading the same level. Giving them a pan settles that, and their
defaults rose by the same 3 dB so a fresh patch is unchanged. A patch saved
before the mixer has its sub and noise levels raised on the way in, which the
preset tests check both ways: raised once, and not raised again on the next
open. A level already at the top of its range stays there, which is the only
case this does not fully preserve.

**What is new**

- `MIX` tab, eight channels: SUB, OSC A, OSC B, NOISE, FILTER, BUS 1, BUS 2, MAIN.
- `Style::fader` — the first control taller than it is wide. It prints no value,
  like every other control here; the bubble appears beside the thumb rather than
  above the fader, which on a control this tall would cover its own label.
- `subPan`, `noisePan`; `filterPan`, `filterMix`, `filterLevel`; two sends on
  each of the five source and filter channels; `enable`, `dest`, `pan` and
  `level` on each bus.
- Five new modulation destinations, appended rather than inserted so every index
  already written into a preset keeps its meaning. The sends and the bus levels
  are deliberately not destinations: they are applied after the voices are
  summed, exactly as `output` is.
- Levels read in decibels rather than per cent, which is the unit a balance is
  actually set in. The values behind them are the same linear gains.

**Still open**

- A fader carries no modulation ring, so while the MIX tab is showing, a level
  being moved by the matrix does not show it. The same parameter's knob on the
  OSC tab does.
- Nothing meters. A channel strip with no signal on it is the obvious next
  thing, and wants the per-channel levels published the way the envelopes are.

### M11a — the effects racks — done

The busses M10c added were a routing topology with nothing at the end of them:
audibly a gain, because a send is only worth making if something is waiting
where it arrives. This is what was waiting.

Three racks — MAIN, BUS 1, BUS 2 — of four slots each, on a new `FX` tab that
takes the whole signal row the way `MIX` does. Six types: REVERB, DELAY, CHORUS,
DIST, EQ and FILTER.

**The decision the whole thing turns on**

Serum's rack holds any type in any slot, in any order, with duplicates. A host's
parameter list is fixed at construction, so a slot cannot declare a parameter per
control of whichever type it happens to hold — thirteen types across twelve slots
is several hundred parameters, nearly all dead at any moment.

So a slot declares a fixed set instead: a type, two mode fields, six general
0..1 knobs, a bypass, a mix and a level. What those knobs *mean* is the type's,
declared once in `ForgeFx.h` and read by three things that must agree:

| Reader | What it takes from the table |
| --- | --- |
| `ForgeFxDsp.h` | The arithmetic that turns 0.6 into 480 ms of delay line |
| `Editor::refreshFxSlots` | The label on the knob, and whether the knob exists at all |
| `Processor::fxKnobText` | The reading in the bubble, from the same helpers the DSP calls |

Nothing copies anyone else's arithmetic, which is what binds "480 ms" in the
bubble to the delay actually sounding. The engine test measures that rather than
asserting it.

The price is that a host's automation lane reads "MAIN 2 KNOB 3" rather than
"Reverb Damp". Serum pays the same price for the same reason, and it was taken
deliberately over a fixed chain of named effects.

**What that made the panel do**

- A fifth tab. `tabWidth` came down from 104 to 86, because five at the old
  width reached within a hair of the preset field at the narrowest window
  allowed — and the layout test now holds the strip clear of that field rather
  than only holding the tabs clear of each other.
- The rack is **one module of four rows and three banks**, so the bank machinery
  the envelopes and the LFOs already use is the MAIN / BUS 1 / BUS 2 chooser.
  Nothing new was needed for it beyond named cards: a bank is usually one of
  several numbered copies of one thing, and these three are not copies.
- A banked module with no drag handle draws its own title, which the rack is the
  first of — so its cards start past a title gutter, and the layout test holds
  such a title to three characters rather than letting a longer one overlap.
- **Visibility is decided in one place.** A knob its type does not use is taken
  off the panel, and the first attempt did that in `refreshFxSlots` — which the
  24 Hz `applyEnableStates` put straight back, once a frame. It belongs in
  `applyEnableStates` with every other reason a control is or is not on screen,
  exactly as the comment above `applyPage` already said.

**Where the racks sit in the signal**

Sends arrive at a bus, the bus's rack runs, then the bus's own fader and pan,
then its destination. Everything reaching the main output runs the MAIN rack and
only then the master level — which is the order Serum states: audio routed to
MAIN passes the modules, and then the master volume.

A rack is one process fed by every note, not a copy per note. That is why the
bus sum was already global as of M10c, and it is what a reverb needs: one tail
fed by every note.

**Modulation**

A rack knob and a slot's mix are modulation destinations, appended past the
named list and generated from the slot indices rather than written out —
eighty-four hand-written lines is eighty-four chances to mislabel one. A slot's
LEVEL is deliberately not one: it is a trim, and a rack whose every stage could
be swept in level is hard to keep at a sane loudness.

Because a rack runs after the voices, a per-voice source has to resolve to a
single voice, and Core takes the loudest — the voice every display already
follows. Serum allows the same and warns about the same consequence: a per-voice
envelope on an FX knob retriggers on every note.

**No allocation once audio is running**

A slot's type changes between one sample and the next, so each slot carries the
state of every type it could hold, sized at `prepare` for the longest line any of
them needs. A few megabytes across twelve slots, and it buys the thing that
matters: changing a slot from a filter to a reverb mid-note cannot touch the
heap.

**Still open**

- No rack presets, no reordering by drag, no copy or paste of a rack. Serum has
  all of these; a slot is set by its TYPE field for now.
- `DIRECT` is still absent. The rack is what it was waiting for, so it is the
  next thing rather than a later one.
- Seven of Serum's types are not here: Bode, Convolve, Flanger,
  Hyper/Dimension, and the three splitters. Each is now DSP and a row in
  one table rather than any new machinery.
- A slot has no display of its own — no delay filter curve, no EQ response. The
  filter module's own display is the model for what those should be.

### M11b — the rack, by eye — done

M11a left the rack working and unreadable: four slots deep and twelve controls
wide, every row the same violet, and the only thing saying which effect was
where was a small field reading "DELAY". A rack is read at a glance or not at
all.

**What each type now carries**

- **A colour.** Six hues far enough apart to tell apart, none of them the
  electric blue the signal path uses or the violet the modulators do, so an
  effect never reads as either. The slot wears it on its plate, on every knob in
  the row, and lit down the left edge of its shelf.
- **A mark.** The shape of what the effect *does* to a signal rather than a
  symbol standing for its name: a reverb's decaying burst, a delay's four fading
  repeats, a chorus's two copies walking apart, a distortion's flattened peaks,
  an equaliser's boost and cut, a filter's resonant corner. Drawn from paths
  into whatever box they are given, so they serve a plate at any panel size.
- **A name plate**, which replaced the TYPE stepper. `Style::plate` is the first
  control that is a board rather than a field: it carries the mark, the name and
  the colour, and clicking it opens the list. A slot's identity and its one
  structural choice are now the same object rather than a badge beside a field.
- **An opening setting.** A parameter has one default and a slot's knobs serve
  eight effect types, so the default cannot be right for all of them — 100% wet is what
  an equaliser wants and exactly what a reverb on the main output does not. What
  a type opens on lives beside the type in `ForgeFx.h` and the panel applies it
  when a type is chosen, which is what Serum's per-module default preset does.
  **Only from the panel**: a type arriving from a preset or from a host's
  automation lane lands with the values that came with it.

**Two things worth keeping in mind**

The plate drives a choice parameter, which `ButtonAttachment` cannot carry. The
slot's slider stays attached and is simply never shown nor added as a child: the
attachment is what a host reads and writes the type through, the plate draws the
value and the menu sets it. That is cheaper than a second path to the parameter
and it keeps host automation working untouched.

The equaliser opening flat is a checkable claim rather than a stated one, because
its gain sits at the centre of a signed range — the test reads `fxScaled` back
and holds it to 0 dB. A reverb and a delay are held to opening mostly dry for
the same reason: they are the two usually placed on MAIN.

### M11c — a display per slot — done

Every slot now draws itself in a strip between its mode fields and its knobs:
a reverb's decay envelope, a delay's repeats falling away across the two
channels, a chorus's two taps swinging across one cycle, a distortion's transfer
curve, an equaliser's response, a filter's corner.

**The rule they are all built on** is the one the FILTER module's display was
built on: a display is drawn from the arithmetic the engine runs, never from a
picture of the effect in general. The distortion curve is `fxShape` called per
pixel. The equaliser's response is the magnitude of the very biquads `setBand`
builds, evaluated on the unit circle. The delay's repeats are placed by
`fxDelaySeconds` and fall away by the same feedback the line is fed with. So a
display cannot claim one thing while the slot does another, and when a display
disagrees with what is heard, the display is not what is wrong.

The rack filter had to compute its own curve rather than borrow the filter
module's: Core damps at `1/(1+res*15)` and the rack at `2-res*1.96`, and a curve
drawn with the wrong one is exactly the lie this is meant to prevent.

**What the layout needed**

`Row` gained `displayWeight` and `displayAfter`. A module's own display sits
above its controls and there is one of it; a rack has four slots in one module
and each wants its own, so this is per row. The strip takes its share of the row
in the same weights the cells divide by, and `cellBounds` pushes everything past
it along — one total, two readers, and the layout test intersects the strip with
every control block in its row at every allowed size, because integer division
is precisely where the two would drift apart.

**What is checked, and how**

Where a curve can be compared against the function it claims to come from, it
is: an equaliser opening flat measures flat at six frequencies rather than
merely looking flat, a low shelf asked for +12 dB lifts what is under it and
leaves what is above it alone, every distortion shape leaves silence silent and
stays inside the box its curve is drawn in, hard clipping at no drive *is* the
faint diagonal the display draws behind every curve, and a synced delay lands on
the division its own readout names.

**Still open**

- A knob turned inside the rack repaints the whole rack box rather than the one
  slot. It is one `repaint` of a rectangle per step of a drag and has not been
  worth narrowing, but it is the obvious thing to narrow if the rack ever gets
  slower.
- Nothing meters. A slot's display says what the effect is set to, not what is
  going through it — no gain reduction, no live level. That wants per-channel
  levels published the way the envelopes are, which the mixer wants too.

### M11d — controls that fit what they are choosing — done

The mode fields were steppers you drag, which served both of the things they
have to be badly: dragging to reach PING-PONG from NORMAL is a gesture for a
continuous value, and neither a two-state switch nor an eight-item list is one.

**A selector draws itself from the count the type declares.** Two or three named
states are stacked with the live one lit; more than that is a name between two
arrows, where the arrows step and the name opens the list. Stacked rather than
in a row because the names are words of very different lengths — OFF against
PING-PONG — and a row either crops the long ones or gives the short ones room
they do not need. The cell is declared in the layout; which of the two it becomes is settled
at runtime, because what a mode steps through is the effect's business and the
layout cannot know it. The arrows grey at the end they cannot pass — a list that
does not wrap should say so before it is clicked.

`FxSelector` copies the choices out of the type table rather than pointing into
it. The type in a slot changes underneath the selector, and a pointer to the old
type's choices is a dangling read waiting for the next repaint.

**A knob a mode has made meaningless now greys out.** Three rules, each a fact
about the effect rather than about the panel, so they live beside the types in
`ForgeFx.h`: the distortion's FREQ and Q are dead while its filter is switched
off, and an equaliser band's gain is dead once that band is a pass shape — which
is what Serum says of its own. Greyed rather than hidden, because unlike a knob
the type does not have at all, these come back the moment the mode beside them
moves. The decision is made in `applyEnableStates` with every other reason a
control is or is not live, which is the lesson M11a learned the hard way.

**What is checked**

Every choice of every mode of every type is written through the panel's
arithmetic and read back through `fxModeOf`, because the parameter behind a mode
is a plain 0..1 and a disagreement between those two would show one state while
the engine ran another. Every choice is also held to having a name to draw and
every field to having a label. The three gating rules are checked both ways
round — a rule that greys a knob and never ungreys it looks exactly like one
that works — and every other type is held to leaving all of its knobs live, so a
rule added by accident to one of them is caught rather than merely unnoticed.

**The bug this shook out**

A mode field looked right only after some *other* click, or after switching
tabs. The panel read parameter values from the cached atomic beside them, and
that atomic is kept up to date by one of the parameter's own listeners — while
JUCE calls listeners in the reverse of the order they registered. A panel
attachment registers after the state does, so it was called first and read the
value from before the change it was being told about.

`Editor::value` now reads the parameter itself, which stores its value before it
tells anybody. It costs a lookup by name per read, which at the rate the panel
refreshes is nothing, and the atomic stays what the audio thread reads.

The race is reproduced deterministically in the tests rather than by eye: a
listener registered on a parameter records both readings at the moment it is
told, and the test pins the parameter's own reading while reporting what the
atomic held — which, at the time of writing, is still the old value. The same
hazard had been sitting under the name plate and the greyed knobs, where it
happened to be masked by the extra refreshes those paths trigger.

**Still open**

- The same treatment would suit the steppers outside the rack: the filter's
  TYPE is three choices and an LFO's UNIT is two, and both are dragged today.
  `FxSelector` is general enough to take them; it was kept to the rack because
  that is what was asked for.

### M12 — warp on both oscillators — done

The oscillators read their tables straight. Everything that makes a wavetable
synth sound like more than a sample player — the sync, the bends, the folds —
was missing, and the manual gives it a chapter of its own (pp. 49–54).

**Two stages per oscillator, a mode and a depth each,** in a row under the knobs
they belong to: a depth knob at each end with the two mode fields between them,
which is the arrangement the manual's warp figure has and the order the stages
are applied in. The row weighs the same as the knob row above it and divides
into the same six cells, so WARP 1 stands under POSITION and WARP 2 under LEVEL.

**Thirty-eight modes in nine families**, grouped in the menu the way the manual
groups them — OFF and SYNC are one item each because they are one mode each, and
ALT WARP, FILTER, DISTORTION, FM, PD, AM and RM open submenus. A field this long
draws as a name between two arrows, which is the presentation `FxSelector`
already had for a list too long to stack; the arrows step without opening
anything. At the foot of the menu, past a rule, the two stages change places —
the modes swap and the depths stay put, exactly as the manual describes.

**FM and PD are separate families**, which the first pass of this milestone got
wrong. It shipped four modes called FM that were phase modulation, which is what
the manual calls PD: "this is similar to FM except that the phase is modulated
instead of the frequency". They are now named for what they do and the four
places they occupy did not move, because a mode is stored as an index and moving
one moves it inside every preset already saved.

The difference is not cosmetic. PD pushes the read along the cycle, so the note
stays exactly where it was however deep it goes. FM reaches the phase increment
instead — the one thing every other warp deliberately leaves alone — so it bends
the note, and it is the only family whose depth knob can. Linear is proportional
and clamps at zero rather than running the frequency backwards, which the manual
names as the traditional "can't do thru-zero" FM; exponential sweeps in octaves,
which is why it is the brighter and harsher of the two and why it does not hold
the pitch where linear does. `warpPitchFactor` is the whole of that difference
and is the one thing outside the read chain that a warp touches.

AM rides the carrier and leaves it in the sound; RM replaces it, so the carrier's
own pitch goes and the two sidebands are what is left.

Three sources, against Serum's six: the other oscillator, the sub and the noise.
Serum names its two filters as well; Forge has one filter and it sits downstream
of both oscillators, so pointing an oscillator at it would be a loop. PD carries
a fourth that the other families do not — a stage reading its own last output —
which is what the manual's own list does.

**One implementation, two readers.** A warp is handed a `read` that turns a
phase into a sample, so the same function serves the voice — reading a
band-limited table — and the panel, reading the table as authored. That is also
how the two stages chain: the second is handed the first rather than a buffer
between them, which is what lets a stage that moves the phase move what the
stage in front of it is reading. One consequence falls out of it: a phase-domain
mode and a sample-domain mode commute, because a stage passes on a way to read
rather than something already read.

**A depth of nothing means nothing.** The phase-domain modes are neutral at the
bottom of the knob by their own mapping; the filters and the waveshapers get
there by crossfading from the wave as it was, which costs half a filter reading
as a shelf rather than as a corner and buys a knob that never does anything at
zero. Four modes go both ways instead and are neutral at twelve o'clock, exactly
as the manual says; `warpNeutralDepth` names which, and the panel returns the
knob there on a double-click so the middle of a bipolar warp is somewhere the
hand can actually get back to. MIRROR is the one mode that is never neutral,
which is what the manual says of it too.

**Aliasing is traded, not solved.** Forge does not oversample. Each mode instead
declares how much extra bandwidth it is about to ask for at the depth it is set
to, and the oscillator picks a band-limited copy of the table for a note that
much higher — the same trade Serum makes before it oversamples. It is why a deep
SYNC sounds softer than its ratio suggests. SYNC also crossfades across the last
two percent of the master cycle rather than jumping, which is what the manual
describes as the WARP Var fader and what turns a step into a join.

**What the panel draws.** The tube draws the warp the voice is running, for the
modes that are honestly a picture of a table. The three filters run against time
and the four FM modes read something outside the oscillator, so both families
take themselves off the trace instead of drawing something untrue — the same
line the manual draws when it says the 2D view shows Sync, Alt Warp and
Distortion.

**Where the four new destinations went.** Past the eighty-four generated rack
entries rather than beside the oscillator controls they belong with. A slot
stores its destination as an index, so moving one moves it inside every preset
already saved; the list is appended to and never inserted into, and that rule
does not bend for tidiness. The modes are deliberately not destinations.

**What the oscillator row paid for the third row.** The display, not the knobs.
The oscillators' share of their own body drops from 55% to 44%, which is what
keeps the waveform the largest thing on the panel while the knobs come down from
63 pixels to 52 at the size Forge opens at.

**What is checked**

Every mode is rendered at full depth on both stages, at the bottom, middle and
top of the keyboard, and held to producing finite, audible signal inside full
scale. Every mode is held to leaving the wave untouched at the depth it calls
neutral, measured against the mode's own `warpNeutralDepth` rather than against
zero, so a mode that moved its neutral would fail rather than quietly change.

The pitch is measured as the period the render repeats over, not as its tallest
partial: SYNC deliberately makes its own harmonic the loudest thing in the
signal, so the method `tuningSuite` uses would report the mode working as the
note moving. Thirteen modes are held to repeating at the note and to nothing
shorter repeating at all — the check that would catch a warp applied to the
phase increment instead of to the phase, which is the easy way to write one and
the wrong way, because it would make the depth knob a tuning control.

RECTIFY is held to leaving no constant offset behind, which is the check on the
blocker the asymmetric modes need. The two stages are held to running in the
order they are declared, measured on two modes that genuinely do not commute.
FM OSC is held to doing nothing while the other oscillator is off and to working
with its level all the way down, which is what the manual says of it. And the
four new destinations are held to landing where their indices say while the
racks stay exactly where they were.

**The three things the tests changed**

FM SELF at any useful depth was chaotic rather than periodic. The feedback index
now runs to a quarter of a cycle rather than four, which is the range a feedback
control is actually reached for, and the average of the last two outputs goes
back round rather than the last one alone — the standard zero at half the sample
rate. It is still chaotic at the top of the knob on a saw, and that is what the
knob is for; the test measures it on a sine, which is the shape feedback turns
into a saw.

A stage pointed at a source that is switched off is now turned off rather than
handed a modulator of zero. The difference is not the silence — that was already
right — it is that a live stage also asks the table for bandwidth it is not
going to use, so choosing a cross-modulation with its source off quietly went
dull for nothing.

The clamp on linear FM bites earlier than it reads: at full depth the frequency
is already at a standstill a quarter of the way down the modulator, so a good
part of every trough is spent there. That is what traditional FM is, and the
test now says so in the number rather than asserting vaguely that it still
moves.

**Still open**

- The unison spread the manual lists beside RANGE — spreading each warp depth
  across the stack — belongs with the unison panel rather than here.
- The two filters Serum offers as modulation sources. Forge's one filter is
  downstream of the oscillators, so it needs a tap taken before the voice sum
  before it could be one — which is a routing change rather than a warp.
- FLIP and QUANTIZE put a step in the waveform, and reading a duller copy of the
  table only takes the edge off it. They are the two modes that would most
  repay oversampling the oscillator, which nothing in Forge does yet.

### M13 — the arpeggiator — done

Serum's ARP module, pp. 244-266 of the manual, as one arpeggiator. The twelve
launchable slots, the arp banks and the custom pattern editor are deliberately
left for M13b and M13c; everything else on those pages is here.

**Where it lives, and why it is not a tab.** The panel gave up the keyboard's
bottom octave and put the **ARP** plate there, between the left-hand decal and
the keys — where Serum puts its own ARP switch, and where the hand already is.
The plate is sized as exactly the octave that came off, so every key is the
width it always was: the keys are still divided out of the span they had, and
only the range changed. The decal outboard of it was left alone on purpose;
that is where the pitch and modulation wheels go when they arrive, and its
lettering is the thing they replace.

The settings are an **overlay on ENV and LFO**, not a sixth tab. A patch is
adjusted at the macros while a pattern runs, and the oscillators and the filter
are what you want to keep watching while it does — so the arp covers the two
modules you are least likely to be reading at that moment and leaves GLOBAL and
the macros on either side of it. It opens over whichever tab is showing, and the
tabs are untouched.

It is still declared as a page. `Page::arp` is a bit like the five tabs but is
never what `page` is set to, and `everyPage` deliberately means *every tab*
rather than every page — so ENV and LFO, which declare `everyPage`, share no page
with the arp. That is what keeps the layout test's "no two modules shown
together overlap" check true through an overlay that is, by construction, on top
of two modules. What the overlay covers is worked out from the declarations
rather than named: `coveredByArp` asks which modules' grid cells the arp's own
panes occupy, so moving a pane a column moves what it hides with it.

**Six panes, one plate,** in the manual's own grouping, sharing a group plate the
way SUB and NOISE do. The first is called ARP rather than GLOBAL, which is the
manual's name for it: Forge already has a GLOBAL standing immediately beside it,
and two plates a centimetre apart carrying the same word is worse than departing
from the manual by one heading. It is also the pane wearing the arp's power
lamp, so its own name is the one that belongs on it.

**Eighteen shapes, and one list serving twice.** Serum's SHAPE field and its
transpose-range menu offer the same vocabulary — Up, Thumb Up, Converge, the
random four — and mean the same thing by it: an order to visit a set of things
in. So `arpOrder` is written over indices rather than over notes, and is called
once for the keys held down and once for the transposition stages, knowing which
it is doing neither time. The turning points are where these go wrong, and the
tests say so explicitly: Up/Down sounds the top note once and Up+Down sounds it
twice, which is the whole difference between the two pairs.

**The arp is not part of Core.** It stands in front of the voices rather than
inside one — it decides which notes Core is asked to play — so `Core` knows
nothing about it and the Processor owns and drives it. It emits through two
callbacks, which is what lets the whole of it be exercised by a pair of lambdas
with no `Processor`, no `Core` and no audio device: what the arp did is a list of
note numbers rather than a waveform to be measured, and an expectation can be
written down rather than measured out. `tests/ForgeTestsArp.cpp` is mostly that,
with one suite at the end going through a `Processor` because the one thing
lambdas cannot check is that the notes reach the voices.

Nothing in it allocates. The held keys, the order they are visited in and the
notes still sounding are fixed arrays, because `advance` runs once per sample.

**TRIP and DOT scale the division rather than being entries in it,** which is
what keeps the rate list seven long instead of twenty-one, and is what Serum
does. Both at once is a dotted triplet, which is a real if unusual rate, so
neither switch cancels the other.

**What the tests caught.** `arp.advance` was handed
`AudioProcessor::getSampleRate()`, which is set by the host calling
`setRateAndBufferSizeDetails` and is therefore zero whenever `prepareToPlay` is
called directly — as every test does, and as the standalone does. The arp
divided by it, stepped once per sample, and stacked every voice the synth had
into a continuous tone. The rate `prepareToPlay` was given is kept instead. The
check that found it is deliberately worded as *silence between the notes*
measured over a window shorter than one step: the first version of it averaged
over a window longer than a step, and passed on a build where the arp was doing
nothing recognisable at all.

**Known interaction.** Core allocates a voice per note number, so a key passed
through by THRU and the same pitch played by the arp are one voice rather than
two, and the arp's gate releases it. Untransposed, the arp is always playing a
note that is being held, so THRU reads as a shortened root under a plain pattern
and as intended under a transposing one. Giving the two paths separate voices
means keying allocation on something other than the note, which is a change to
the voice allocator rather than to the arp, and is not worth making until
something else wants it.

### M13b — the twelve arp slots — not started

The ARP module holds twelve arpeggiators per bank, launchable from the computer
keyboard or from MIDI, plus the bank field, `EDIT ALL`, and bank presets that
save and load. It is a clip-launcher-shaped feature in its own right, which is
why it is not in M13. `LAUNCH QUANT` already exists and already means what it
will mean then — it holds a started arp to the next division of the host's bar.

### M13c — the custom pattern editor — not started

`SHAPE` set to Pattern opens a graph editor: a piano roll of note events with
accent and strum lanes, pattern length, play mode, step mode and wrap, and
patterns that save and load. The manual (p. 253) says outright that it "offers
capabilities very similarly to the piano roll in the CLIP module".

**The intent is to reuse Rhino's own note editor rather than write a second
one.** That is not a straight lift: `StepGrid` takes a `Session&` and reaches
into it about a hundred times across four files, and `Session.h` pulls in
Tracktion — which Forge deliberately owns none of. Reusing it means first
extracting the part that is genuinely about a grid of notes from the part that
is about a Rhino track, the way `ClipGeometry.h` was extracted: a header with no
Session or engine dependency, holding what a note is, where it lands, and what a
drag does to it, with `StepGrid` and the arp editor as two readers of it. Doing
that is most of this milestone, and it improves the DAW side as much as it
enables the synth side — which is the only reason it is worth doing at all
rather than writing a small editor here.

### M14 — the noise module becomes an oscillator — done

NOISE was a power switch and a LEVEL knob standing in a tall empty column. It
becomes a small source with a character of its own, in the shape the rest of
the panel already uses.

**Four sources**, chosen from the field the filter's TYPE and the warp modes
wear: WHITE flat across the band, PINK at 3 dB an octave, BROWN at 6, and
GEIGER — sparse shaped clicks rather than a spectrum. Four is one past what
that field stacks, so it draws as a name between two arrows, which is the
previous/next-and-browse the module wants and cost nothing to get.

**TONE** tilts the selected source about 1 kHz. A first-order tilt rather than
a filter with a cutoff, because the plate has room for one knob and "darker or
brighter" is the question a noise source is actually asked. It returns its
input exactly at twelve o'clock — taken as an early return, so a patch that
leaves the knob alone is hearing the generator and not the generator plus an
ulp. What tilting does to the level of a flat spectrum is measured at prepare
and divided back out, so the knob changes the colour rather than the loudness.

**STEREO** is decorrelation, not width. At nothing both channels are the same
samples; at the top they share no state. The blend between is equal-power, so
the correlation falls off as the square root of what is left while the level
holds. Two generators per voice rather than three: the left channel is one of
them outright and the right is the equal-power mix, which is exact at both ends
of the knob and costs a third less state than a shared-plus-two-independent
arrangement would.

**Per voice.** The old generator was one stream on the Core shared out across
the voices. Every note now hisses on its own, which is what makes a chord
thicken rather than double, and it is seeded from a counter reset with the
engine — so two notes differ and the same phrase rendered twice is the same
file. The LFOs' sample-and-hold keeps the Core's own generator and is
unaffected, which also means switching the noise on no longer changes what an
S&H steps to.

Every source runs every sample whether or not it is selected. That is what lets
a source change cross over between two streams that are both already warm,
rather than fading one in from whatever its filter was left holding — six
milliseconds, the same ramp the module's power switch now uses.

PAN, LEVEL, the filter route and the two sends are untouched: they are the
mixer's, they already modulate, and the module shows the same parameters the
MIX tab's NOISE strip does. TONE and STEREO are appended to the destination
list after the filter's second field, so no index already written into a preset
moves. The source is deliberately not a destination, for the reason the warp
modes are not.

The DSP is [core/ForgeNoise.h](core/ForgeNoise.h), depending on nothing of
Forge's the way `ForgeFilter.h` does not, and `tests/ForgeTestsNoise.cpp` is a
new area: the slopes fitted through six octave bands of a rendered second, the
correlation between the channels at three widths, two voices adding as powers
rather than as amplitudes, and the step at a source change measured against the
steps either side of it.

### M14b — nineteen sources, in families — done

The four M14 shipped were the brief's Color group and nothing else. The rest of
its groups — Analog, Digital, Inharmonic, Organic, Transient — were written up
as needing recordings, and most of them do not: what makes a Juno's noise floor
sound unlike an SH-101's is a couple of corners and a little saturation, and
what makes vinyl sound like vinyl is a surface, some dirt and a rumble. All
fifteen new sources are generated. Nothing is sampled, nothing is licensed and
nothing claims to be a recording of hardware it is not.

**Six families.** COLOUR gains BLUE and VIOLET — pink and white differentiated,
which lifts a spectrum by 6 dB an octave — and GREY, weighted to sound flat
rather than measure flat. ANALOG is POLY, POLY HP, MONO, TAPE and HUM. DIGITAL
is BRIGHT, BIT and ALPHA. INHARMONIC is METAL. ORGANIC is VINYL and WIND.
TRANSIENT is GEIGER and the new CRACKLE.

POLY HP is a separate source rather than POLY with the tone knob up, which the
brief asks for explicitly: it keeps the roll-off above the corner instead of
brightening the whole, and the test holds it to that — more than 12 dB between
them at the bottom of the band and less than half of that at the top.

**The architecture had to change to take them.** M14 ran every source every
sample so that a source change crossed between two warm streams. Nineteen of
those would be nineteen times the arithmetic to hear one. A source is now a
*colour* through a *character*: white, pink and brown are the slow part and go
on running whatever is selected, and only the live character stage runs plus
the one fading out. Cost is two stages however long the list gets. A stage is
reset as it comes in, which is free because what feeds it never went cold.

**Two orders.** Values are appended to and never reordered, so GEIGER is still
3 and a preset keeps naming what it named. The panel lists by family instead,
and the two meet only at `noiseSourceAt` and `noiseSourcePosition`. A host's
lane reads the stored order and the long names; the plate reads the shown order
and short ones, because two columns of twenty-four will not hold "Vintage Poly
HP".

**Levels are measured, not derived.** Nearly every source is white through
something, and what that something costs depends on its poles, on where a
difference of two decays peaks and on how hard a saturator is leaned on. Each
is rendered for a few seconds at prepare and scaled to white's level. It
depends only on the sample rate, so it is cached and shared.

Two things were settled by measuring rather than by choosing. CRACKLE at 320
events a second — a reasonable-looking number — overlapped about as much as
GEIGER's longer tails and came out just as peaky; it is 1200 now. And the check
that told them apart was crest, which measures peakiness rather than density
and had them the wrong way round: crackle's clicks are a fifth as long, so it
is *more* peaky despite there being twenty-five times as many. The test counts
events now.

Still deliberately absent: GEIGER's and CRACKLE's density as a control. A knob
beside SOURCE that means whatever SOURCE is, the way the filter's second knob
does, is the right shape for it and is worth adopting once more than two
sources want one.

### M14c — sample sources — not started

Serum's noise oscillator also plays short samples, and the module was built to
take them: the source field is a list that can grow, the per-voice state is
already where a sample cursor would live, and the parameter set survives a
control being hidden. What it needs is the machinery — decoding off the audio
thread, a stable asset id in the preset, loop and one-shot, START and RAND,
PITCH and FINE, interpolation, a loop crossfade, user import and a recoverable
missing-sample state — and a curated factory library to point it at.

The library is the part that is not code. Recognisable analog noise means
recordings of the hardware or properly licensed ones; nothing of Serum's is
usable. Until those exist the browser would be categories with nothing in them,
which is why this is separated out rather than half-built.

The event sources' densities are fixed, which is the one control still left
out. A knob beside SOURCE that means whatever SOURCE is — the filter's
arrangement — is the right shape for it, and it belongs with this work rather
than before it.

## Out of scope for now

These are the north star, not this plan. They come after the synth is finished.

- **M11e — the rest of the rack.** Rack and module presets, reordering by drag,
  copy and paste between racks, and the seven Serum types M11a left out. Plus
  the `DIRECT` output, which needed effects to bypass before it could mean
  anything.
- **M15 — Second filter,** with the serial/parallel routing Serum exposes.
  (Renumbered: M13 is the arpeggiator, which landed first.)
- **M14 — Preset browser** with tags and search.
- **Later still:** MPE, granular sources, spectral oscillators. The noise
  module's sample sources are nearer than those and have a milestone of their
  own — M14c above.

## Building and testing

```powershell
cmake -S instruments/rhino-forge -B instruments/rhino-forge/build -G "Visual Studio 17 2022" -A x64
cmake --build instruments/rhino-forge/build --config Release --parallel 2 -- /p:BuildInParallel=false
ctest --test-dir instruments/rhino-forge/build -C Release -j 8 --output-on-failure
```

One CTest case per area, all from one binary, selected by argument. The name of
the case is the name of the file it runs and of the argument that selects it,
so working on one module means rebuilding one translation unit and running one
case: `ctest -R forge_fx` while the effects are being changed, plain `ctest` at
a milestone. `RhinoForgeTests --list` prints them. No two areas share a
`Processor`, so `ctest -j 8` runs them at once.

See the **Tests** section of [README.md](README.md) for where the registry
lives and what is shared between areas.

The standalone build at
`build/RhinoForge_artefacts/Release/Standalone/Rhino Forge.exe` is the quickest
way to look at a change.

## Compatibility notes

- Rhino discovers Forge by plugin name in `SessionExternalPlugins.cpp`; it never
  names a Forge parameter. Parameter changes therefore cannot break Rhino's
  hosting, only the Forge state stored inside an already-saved `.rhinoedit`.
- Retired parameters are dropped from saved state rather than kept as ballast,
  and parameters a preset predates load at their defaults. A preset saved at one
  milestone therefore still opens at the next while the control set is moving.
  This applies to host state too, so a Rhino project holding older Forge state
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
| M7 LFO 1 | **done** — ready to test by ear |
| M8 Polish | interaction **done**; a factory preset set still to author |
| M9a Ten shapes in the oscillator table | **done** — ready to test by ear |
| M9b-1 A real table, band-limited | **done** — ready to test by ear and by eye |
| M9b-2 Loading a table from a file | **done** — ready to test by ear and by eye |
| M9b-3 The table editor | **done** — ready to test by hand |
| M9c LFO modes and rate unit | **done** — ready to test by ear |
| M9d Voice stealing without a click | **done** — ready to test by ear |
| M9e Voice tail, not a truncated filter | **done** — ready to test by ear |
| M10a Six LFOs, per voice | **done** — ready to test by ear |
| M10b ENV 2–4 | **done** — ready to test by ear |
| M10c The mixer, and two busses | **done** — ready to test by ear and by eye |
| M11a The effects racks | **done** — ready to test by ear |
| M11b The rack, by eye | **done** — ready to test by eye |
| M11c A display per slot | **done** — ready to test by eye |
| M11d Controls that fit what they choose | **done** — ready to test by hand |
| M12 Warp on both oscillators | **done** — ready to test by ear |
| M13 The arpeggiator | **done** — ready to test by ear and by hand |
| M13b The twelve arp slots | not started |
| M14 The noise module becomes an oscillator | **done** — ready to test by ear |
| M14b Nineteen sources, in families | **done** — ready to test by ear |
| M14c Sample noise sources | not started |
| M13c The custom pattern editor | not started |
