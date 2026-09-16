# Handover: Forge M9b, real wavetables

**Written for the next agent picking this up, on a machine that has not seen this
work.** It covers one milestone in one instrument. The repository-wide handover
about the September 2026 file split is [../../HANDOVER.md](../../HANDOVER.md) and
is unrelated to this.

M9b-1 is done, committed and pushed. M9b-2 and M9b-3 are not started. The plan
for all three is in [PLAN.md](PLAN.md); this document is the part that does not
belong in a plan — what the code now looks like, why it is shaped that way, and
what will bite you.

## Start here

```powershell
cmake -S instruments/theta-forge -B instruments/theta-forge/build -G "Visual Studio 17 2022" -A x64
cmake --build instruments/theta-forge/build --config Release --parallel 2
ctest --test-dir instruments/theta-forge/build -C Release --output-on-failure
```

Three cases, all green as of this handover. The standalone at
`build/ThetaForge_artefacts/Release/Standalone/Theta Forge.exe` is the quickest
way to look at anything.

Read [../../AGENTS.md](../../AGENTS.md) before touching Theta itself. Two rules
bind this work in particular: **never read `native/.deps/` or
`research/sources/`**, and **no allocation, locking or filesystem work in
`renderSample`**.

## What M9b-1 changed

One new file, three edited.

| File | What happened |
| --- | --- |
| `core/ForgeWavetable.h` | **New.** The `Wavetable` type, its band-limited levels, and the interpolator. |
| `core/ForgeCore.h` | The ten shapes became a generator rather than the render path; `Oscillator` gained a table pointer; the saw's jump moved; `renderOscillator` reads the table. |
| `tests/ForgeTests.cpp` | New `bandLimitSuite`, plus saw-shape checks inside `waveTableSuite`. |
| `CMakeLists.txt` | `juce_dsp` linked, the new header listed. |

Nothing in `ui/` changed at all. That is worth knowing: the panel draws the new
table through the same `waveAt(position, phase)` it always called.

## The five things that will bite you

**1. `waveShape` is no longer on the render path.** It looks like the oscillator,
and it is still where a frame is *authored*, but it now runs ten times at startup
to fill `builtInWavetable()` and never again. If you change a formula and nothing
sounds different, you are listening to a table built before your change — restart
rather than reloading the plugin.

**2. Level 0 is sacred.** It is the frame exactly as authored, untouched by the
transform, and it is what the panel draws. A test asserts it matches `waveShape`
to 0.0005. When frames start coming from files and brushes, keep this property:
what the display shows has to be what the table holds, or the editor lies to the
person using it.

**3. The voice does not read level 0.** It reads whichever band-limited copy the
note allows, chosen in `Wavetable::levelFor`. So the panel and the voice now read
*different data* — deliberately, and it is the one M9a guarantee that M9b-1
loosened. Do not "fix" this by drawing the level the voice reads: it would change
with every note played.

**4. The table pointer's lifetime is not yet managed.** `Oscillator::table` is a
raw `const Wavetable*`, null everywhere today, so nothing can dangle yet. **M9b-2
is where this becomes real and it is the dangerous part of that milestone.** The
audio thread will be reading a table while the message thread wants to replace
it. Do not reach for a mutex and do not free the old table on the spot. The
shape that fits what is already here: the Processor owns the tables, publishes a
pointer the audio thread picks up once per block, and keeps the replaced table
alive until the audio thread has demonstrably moved past it — `Processor` already
publishes per-block state through `std::atomic` for the meters, so the mechanism
is familiar. Whatever you choose, write down why it is safe.

**5. The saw is rotated half a cycle from where you expect.** `waveShape(6, 0)`
is `0`, not `-1`. This was the point of the change — see below — and the LFO's
saw in `lfoWave` was deliberately **not** changed to match. Do not "make them
consistent".

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

This does change what the neighbouring morph positions sound like, since POSITION
crossfades SAW against THIN and HUMP either side of it. That was accepted.

## Band-limiting, in enough detail to change it safely

Eleven levels per frame. Level 0 is the frame as authored with all 1024
harmonics. Level *k* keeps `1024 >> k` harmonics and is stored at
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

## Where M9b-2 starts

Nothing has been written toward it. In rough order:

1. Decide the table's ownership and hand-off, per point 4 above. Do this first —
   everything else in M9b-2 is downstream of it.
2. Read a `.wav` into a `Wavetable`. JUCE's `AudioFormatManager` does the file;
   the frame size is the `clm ` chunk when present and 2048 otherwise. Building
   the levels is already done for you by the constructor.
3. Per-oscillator tables: `oscAPosition`'s readout and the POSITION knob's
   `gestureSteps` (`src/ForgeEditor.cpp:240`) are both still wired to the
   built-in ten via `waveShapeCount`. They will need to ask the oscillator's own
   table how many frames it has.
4. Preset storage. `Processor::savePreset` writes a `ValueTree` of parameters
   only; a table is not a parameter. It wants a child node holding the samples,
   and `migrated()` needs to keep working when that node is absent. Preset format
   version is currently 2 — decide deliberately whether embedding a table makes
   it 3, and remember M1's rule that a wrong version is refused rather than
   migrated.

A caution on size: a 256-frame table is 2 MB of level 0 and about 4.3 MB with its
levels, per oscillator. Embedding that in a preset as XML text is not free.
Measure before assuming base64 in a `ValueTree` is acceptable.

## State of the work

- Committed and pushed to `origin/main`.
- All three CTest cases pass; the plugin and standalone both build in Release.
- No known defects introduced. The one accepted regression in fidelity is point 3
  above, and the one accepted change in sound is the saw's rotation.
- Not verified by ear or by eye on this machine. The user tests these milestones
  by hand — assume nothing has been listened to yet.
