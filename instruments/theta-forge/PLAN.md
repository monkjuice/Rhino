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
+- FORGE ------------------------------------- preset -- LOAD SAVE -+
| +-- OSC A -----------+ +-- OSC B -----------+ +- SUB -+ +- NOISE -+|
| | [ waveform ]       | | [ waveform ]       | | (o)   | | (o)     ||
| | OCT SEM FIN        | | OCT SEM FIN        | +-------+ +---------+|
| | POS UNI DET BLD    | | POS UNI DET BLD    |                      |
| | PAN LEVEL          | | PAN LEVEL          |                      |
| +--------------------+ +--------------------+                      |
| +-- FILTER ----------------+ +-- ENV 1 -------------------------+   |
| | source  [A][B][S][N]     | | [ ADSR curve ]                   |   |
| | CUTOFF RES DRIVE MIX     | | ATTACK DECAY SUSTAIN RELEASE     |   |
| +--------------------------+ +----------------------------------+   |
| +-- LFO 1 ---------------------------------------------------------+|
| | [ shape + phase ]   RATE   >CUTOFF   >POSITION   >PITCH          ||
| +------------------------------------------------------------------+|
| +-- GLOBAL -- POLY MONO LEGATO GLIDE ------------------- OUTPUT ----+|
+--------------------------------------------------------------------+
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
would be thrown away by M8.

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

> `DRIVE` already sits in the `FILTER` module, but the DSP still applies it to
> the summed output. M4 moves it into the filter path so the grouping becomes
> true. Nothing else on the panel is placed somewhere its signal does not go.

### M3 — Per-oscillator architecture

- Split shared `unison` / `detune` into `oscAUnison` / `oscADetune` and
  `oscBUnison` / `oscBDetune`.
- Give each oscillator its own `Level` and `Pan`. Oscillator A stops being
  "whatever is left over after B".
- Give each oscillator `Octave`, `Semitone` and `Fine` tuning, replacing the
  single `oscBTune`.
- Add `Blend` per oscillator (unison centre-versus-spread balance).
- Each oscillator draws its own display from its own position.

**Tests:** per-oscillator tuning produces the expected frequency ratio; unison
count changes voice sum without changing perceived level; pan law holds.

### M4 — One filter, with source routing

- `filterRouteA`, `filterRouteB`, `filterRouteSub`, `filterRouteNoise` toggles in
  the filter header, the way Serum's `S A B C N` buttons work.
- Routed sources pass through the filter; unrouted sources bypass it straight to
  the voice sum.
- `Drive` moves out of the global path and into the filter module.
- Filter type selection (low-pass / high-pass / band-pass) if it stays cheap.

**Tests:** an unrouted source is unaffected by cutoff; a routed source is; all
four routes off equals filter bypassed.

### M5 — ENV 1 module with a live display

- One ADSR, hardwired to amplitude.
- The display draws the actual curve from the current A/D/S/R values and marks
  the live stage while a note sounds.

**Tests:** envelope stage transitions at the expected sample offsets; release
from a partial attack starts from the value actually reached.

### M6 — LFO 1 module with a live display

- Shape selection: sine, triangle, saw, square, sample-and-hold.
- Rate, with tempo sync against the host.
- The three depth knobs (`> CUTOFF`, `> POSITION`, `> PITCH`) stay until the
  modulation matrix replaces them with drag-to-knob routing.
- The display draws the shape with a running phase indicator.

**Tests:** each shape is bounded and periodic; tempo sync tracks a BPM change;
depth of zero is bit-identical to the unmodulated render.

### M7 — Interaction and preset polish

- Resize behaviour at every supported size, with no module clipping.
- Knob interaction: scroll, fine drag, double-click to default, right-click menu.
- Tooltips on every control.
- Refresh both bundled presets against the new parameter set and add a small
  set of init variants that show each module off.

## Out of scope for now

These are the north star, not this plan. They come after the synth is finished.

- **M8 — Modulation matrix.** Drag any modulator onto any knob, with visible
  depth rings. Brings back ENV 2–4, LFO 2–6, velocity, note, and macros 1–8 as
  real assignable sources.
- **M9 — Real wavetables.** Loadable tables, a table editor, and frame
  interpolation replacing today's four-frame analytic morph.
- **M10 — FX rack.** Chorus, distortion, delay, reverb, compressor, EQ, in a
  reorderable chain.
- **M11 — Second filter,** with the serial/parallel routing Serum exposes.
- **M12 — Preset browser** with tags and search.
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
| M3 Per-oscillator architecture | not started |
| M4 Filter routing | not started |
| M5 ENV 1 | not started |
| M6 LFO 1 | not started |
| M7 Polish | not started |
