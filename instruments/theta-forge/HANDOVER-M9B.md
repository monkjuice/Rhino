# Handover: Forge M9b, real wavetables

**Written for the next agent picking this up, on a machine that has not seen this
work.** It covers one milestone in one instrument. The repository-wide handover
about the September 2026 file split is [../../HANDOVER.md](../../HANDOVER.md) and
is unrelated to this.

All three parts are done. The plan for them is in [PLAN.md](PLAN.md); this
document is the part that does not belong in a plan — what the code now looks
like, why it is shaped that way, and what will bite you.

## Start here

```powershell
cmake -S instruments/theta-forge -B instruments/theta-forge/build -G "Visual Studio 17 2022" -A x64
cmake --build instruments/theta-forge/build --config Release --parallel 2
ctest --test-dir instruments/theta-forge/build -C Release --output-on-failure
```

Three cases, all green as of this handover. The standalone at
`build/ThetaForge_artefacts/Release/Standalone/Theta Forge.exe` is the quickest
way to look at anything. On this machine CMake is not on `PATH`; it is at
`C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin`.

Read [../../AGENTS.md](../../AGENTS.md) before touching Theta itself. Two rules
bind this work in particular: **never read `native/.deps/` or
`research/sources/`**, and **no allocation, locking or filesystem work in
`renderSample`**.

## What is where

| File | What it holds |
| --- | --- |
| `core/ForgeWavetable.h` | `Wavetable` — immutable, band-limited, what the voice reads. And `WavetableEdit` — the frames as a person changes them. |
| `core/ForgeTableStore.h` | **New in M9b-2.** `WavetableStore`: who owns a table and how one is handed to the audio thread. |
| `ui/ForgeTablePanel.h` | **New in M9b-3.** The canvas, the frame strip and the editor's buttons. |
| `ui/ForgeVisuals.h` | `wavePath` and `fillWaveArea`, shared by the small tube and the big canvas. |
| `src/ForgeProcessor.*` | Owns the store. Reads a `.wav`. Writes the table into presets and host state. |
| `src/ForgeEditor.*` | Places the panel, keeps it in step with the store, owns undo's keystroke. |
| `tables/` | Ten factory tables and the script that writes them. |

## The things that will bite you

**1. `waveShape` is not on the render path.** It looks like the oscillator, and
it is still where a built-in frame is *authored*, but it runs ten times at
startup to fill `builtInWavetable()` and never again. If you change a formula and
nothing sounds different, you are listening to a table built before your change —
restart rather than reloading the plugin.

**2. Level 0 is sacred.** It is the frame exactly as authored, untouched by the
transform. `WavetableEdit` holds the same samples, and the panel draws *those* —
so what you see is what the table holds, on the tube and on the canvas alike. A
test asserts the two agree with `waveAt` while a table is untouched.

**3. The voice does not read level 0.** It reads whichever band-limited copy the
note allows, chosen in `Wavetable::levelFor`. So the panel and the voice read
*different data*, deliberately. Do not "fix" this by drawing the level the voice
reads: it would change with every note played.

**4. A published table is freed the moment nothing can reach it — usually
immediately.** This caught the test that was written to check it. If you hold a
`const Wavetable*` across a `publish()` you are holding a dangling pointer, and
with no audio block in flight it will already have been freed. Sample what you
need into a vector instead. The only thing allowed to hold one across a publish
is a `processBlock` that is running, and that is the whole point of the design
below.

**5. The saw is rotated half a cycle from where you expect.** `waveShape(6, 0)`
is `0`, not `-1`. This was the point of the M9b-1 change — see below — and the
LFO's saw in `lfoWave` was deliberately **not** changed to match. Do not "make
them consistent".

**6. The preset format version is still 2, on purpose.** See below.

## How the hand-over is made safe

This was M9b-1's open question and it is now `WavetableStore` in
`core/ForgeTableStore.h`. The reasoning is written out in full at the top of that
file; the shape of it:

- One atomic per oscillator carries the table the audio thread should read. The
  audio thread loads it **once per block**, into the `Patch`, and uses that one
  pointer for the whole block. So a pointer it loaded during a block can only be
  in use until that block ends.
- `processBlock` brackets itself with `WavetableStore::ScopedBlock`, which
  increments a counter on the way in and again on the way out. The counter is
  therefore **odd exactly while a block is running**.
- After publishing, the message thread reads that counter once. Even means no
  block is running, and any block starting from here on must load the pointer
  *after* the store that has already happened — so the old table is unreachable
  and is freed on the spot. Odd means one block may be holding it, and it is
  parked until the counter reaches the next value.
- Everything in that argument is `memory_order_seq_cst`, on both sides. It rests
  on there being a single total order over the publish and the counter read; a
  weaker ordering permits exactly the interleaving that would free a table out
  from under a running block. Do not relax these to `acquire`/`release` because
  they look expensive. They run once per block and once per edit.

No mutex, no reference count on the audio thread, no deferred-delete thread. The
audio thread does two increments and a load.

**Where this is thin:** a suspended plugin frees immediately, which is the common
case while editing, so the parked list stays at zero or one entry. But nothing
bounds it if a host somehow stalls mid-block forever. `collect()` is called on
every publish and from the editor's timer.

## Drawing, and why a stroke is cheap

`WavetableEdit::draw` is the only thing that ever writes a sample. Freehand calls
it once per mouse move, so a fast drag leaves no gaps; the line tool calls it
once over a whole gesture. They are the same operation.

After a stroke the panel calls `publishFrame`, not `publish`. `publish` rebuilds
every frame — a forward transform and ten inverse transforms each. `publishFrame`
copies the live table and re-transforms **one** frame, which for a ten-frame
table is about a 170 KB copy and eleven transforms, and that is what makes a
stroke audible while the hand is still moving.

`tableEditSuite` holds the cheap path to what the exact path gives, at several
levels and phases. **If you change either one, that test is what catches you** —
without it, drawing would sound different from loading the same table back, and
only an ear would notice.

## What a table costs, and the ceiling

A frame is 8 KB at level 0 and about 17 KB with its band-limited copies. The
editor caps a table at `maxEditableFrames`, which is **64**. That is the number
to raise first if this gets more use:

- A 256-frame Serum table is thinned evenly on import rather than cut short, so
  it still sweeps from its first shape to its last — but it is not the table the
  author wrote. Raising the cap to 256 costs 4.3 MB per oscillator in memory,
  which is fine, and about 3.7 MB of base64 in a preset, which is not obviously
  fine.
- The frame strip divides its width evenly, so past about thirty frames the
  cells stop being wide enough to number and past sixty-four they would stop
  being wide enough to tell apart. A strip that scrolls or zooms is the other
  half of raising the cap.

## Why the preset format version stayed at 2

M9b-1's note said to decide this deliberately, and M1's rule is that a wrong
version is refused rather than migrated. The decision is **2**, and the reasoning
is that the rule is about incompatible changes and this is not one:

- The table is an additive child node beside the parameter state. Every existing
  format-2 preset still opens, unchanged, and loads correctly — it simply names
  no table, and an oscillator that is named no table goes back to the built-in
  ten, which is exactly what it already did.
- Bumping to 3 would refuse every preset written before this change, to guard
  against an older build reading a newer preset. No older build has shipped.

The asymmetry that remains: a build predating M9b-2 opening a preset written
after it would load the patch and ignore the table, so it would sound wrong
rather than refuse. That is the accepted cost.

Host state travels by the same path and the same node, so a project reopens on
the table it was saved with. `migrated()` strips the node, so the live parameter
state stays parameters only — a test asserts that.

## Band-limiting, in enough detail to change it safely

Unchanged since M9b-1. Eleven levels per frame. Level 0 is the frame as authored
with all 1024 harmonics. Level *k* keeps `1024 >> k` harmonics and is stored at
`max(2048 >> k, 64)` points — so roughly two points per harmonic all the way
down, and the whole set costs about 2.1× the table rather than 11×.

Each level is cut with an FFT: transform the frame, zero every bin above the
limit **and its mirror** (a real signal's spectrum is mirrored; missing the
mirror is the classic way to get this wrong and produces a quiet, wrong,
complex-valued mess), transform back, then decimate by taking every *n*th point.
Decimation is exact because there is nothing left above the new Nyquist to fold.

`levelFor(hz, sampleRate)` walks the levels and returns the first whose top
harmonic fits under Nyquist. It is called once per oscillator per sample, from
`renderOscillator`, using `hz * 1.03` — the top of the unison stack rather than
its centre, so the sharpest voice in the stack decides and none of them aliases.

Measured improvement, from the test: a saw at 4186 Hz folded back at 0.089 before
and 0.0008 after, about 41 dB.

**If you touch the transform, the test that will catch you** is "a band-limited
sine is the same sine at every level". A sine is one harmonic, so every level has
to return it unchanged and at the same amplitude. Scaling, sign and mirror
mistakes all fail it immediately.

## Why the saw moved

The user asked for this directly, comparing Forge against Serum: *"the saw in
forge is a single triangle, the saw in serum is one triangle and right next to it
an inverted triangle."*

A saw has one discontinuity per cycle. Forge's `phase * 2 - 1` puts it exactly at
the frame boundary, so a display drawing phase 0 to 1 never shows it and the saw
reads as a plain diagonal. Serum stores its saw with the jump in the middle
instead — visible in their own editor's formula bar as `x<0?-1-x:1-x`, which runs
0 → −1, jumps, +1 → 0.

Forge now does the same, rising rather than falling: zero at each end, full swing
across the centre. Same harmonics, same sound, and the panel finally draws
something that looks like a saw.

## Filling a wave against its zero line

Serum and Vital both do it, and the user asked how. `fillWaveArea` in
`ui/ForgeVisuals.h` does it with **one path and the non-zero winding rule**,
which is the part worth knowing:

The trace already ends on the right-hand edge. Running it back along the zero
line and closing it makes a figure that crosses itself wherever the wave crosses
zero — and the humps above the line wind in the opposite direction to the humps
below it. Under the non-zero rule both are filled and the space outside them is
not, which is exactly the region between the curve and the line, however many
times it changes sides. Filling each half separately would need every crossing
found first; this needs none of them.

The gradient is deliberately gentle. The user has seen it and likes it as it is.

## Where the next work starts

Nothing has been written toward any of this.

1. **Export.** The editor can load a table and cannot write one out. The format
   is already understood from both ends; `tables/make-tables.py` is the RIFF, and
   `declaredFrameSize` is the `clm ` reader.
2. **Raise the frame ceiling**, with the strip that a larger table needs. See
   "What a table costs" above.
3. **The brush palette.** Serum's second column of shapes is the thing most
   obviously missing next to a bare pen and line.
4. **Morph and process across frames** — Serum's `SINGLE / ALL / MORPH` — which
   is what turns a set of drawn frames into a table that sweeps.
5. **Undo is the panel's, not the plugin's.** It is a stack of whole tables in
   `TablePanel`, bounded by weight, and it is lost when the editor closes.
   Nothing else in Forge has undo; if that changes, this should join it rather
   than stay separate.

## State of the work

- All three CTest cases pass; the plugin and standalone both build in Release.
- The editor has been driven by hand on this machine: both tools draw, the frame
  strip follows, the tube on the OSC tab follows, and POSITION switches from
  naming shapes to counting frames when a table is drawn on.
- **Not verified by ear.** No note has been listened to through an edited table
  on this machine. The tests assert that a flattened frame silences the
  oscillator, which proves the table reaches the voice, but not that it sounds
  right.
- The factory tables have not been listened to either. Every one of them is
  checked by test for frame count, finiteness and full scale, and that is all.
