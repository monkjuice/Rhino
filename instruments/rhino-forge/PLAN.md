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
| +-----+ +-----+ +------------------+ +------------------+ +-----------+ |
| +GLOBAL+ +-- ENV -------------+ +-- LFO -------------+ +-MACROS-+      |
| |POLY GL| | [ ADSR curve    ] | | [ shape + phase ]  | | 1    2 |      |
| |MO LEG | | [1][2][3][4]      | | [1][2][3][4][5][6] | | 3    4 |      |
| |OUTPUT | | ATK DEC SUS REL   | | SHAPE MODE UNIT RT | | 5    6 |      |
| |       | |                   | |                    | | 7    8 |      |
| +-------+ +-------------------+ +--------------------+ +--------+      |
| [==================== eighty-eight keys ============================ ] |
+----------------------------------------------------------------------+

Both rows are the same height. The MATRIX tab puts the matrix in the
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
- Seven of Serum's types are not here: Bode, Compressor, Convolve, Flanger,
  Hyper/Dimension, Phaser, and the three splitters. Each is now DSP and a row in
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
  seven types, so the default cannot be right for all of them — 100% wet is what
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

## Out of scope for now

These are the north star, not this plan. They come after the synth is finished.

- **M11e — the rest of the rack.** Rack and module presets, reordering by drag,
  copy and paste between racks, and the seven Serum types M11a left out. Plus
  the `DIRECT` output, which needed effects to bypass before it could mean
  anything.
- **M12 — Second filter,** with the serial/parallel routing Serum exposes.
- **M13 — Preset browser** with tags and search.
- **Later still:** MPE, sample and granular sources, spectral oscillators.

## Building and testing

```powershell
cmake -S instruments/rhino-forge -B instruments/rhino-forge/build -G "Visual Studio 17 2022" -A x64
cmake --build instruments/rhino-forge/build --config Release --parallel 2
ctest --test-dir instruments/rhino-forge/build -C Release --output-on-failure
```

Three CTest cases run from one binary, selected by argument, as Rhino's own
tests do: `forge_layout` checks the panel geometry and that the declared layout
and the parameter list agree, `forge_presets` checks saving, loading and
reconciliation, and `forge_engine` renders audio and checks the module enables,
the filter bypass, and that output stays finite and inside full scale.

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
