# Spectral synthesis in Forge

An oscillator in Forge reads a wavetable. This document is the plan for giving
it a second thing it can be: a **spectral** oscillator, which analyses a sample
into frames of frequency content and resynthesises it, so the sound can be
scanned, filtered and bent in the frequency domain rather than in the waveform.

Serum is the north star, as it is for the rest of Forge. The chapter this is
built from is **Using Spectral Synthesis**, pages 104-122 of the Serum 2 User
Guide. Page references below are to that guide.

This is numbered **M16** rather than appended to [PLAN.md](PLAN.md) because it
is large enough to have its own shape, and because PLAN.md's out-of-scope list
already spends M14 and M15 twice over. PLAN.md line 1565 files "spectral
oscillators" under *Later still*; this document is what moves it.

## What the manual specifies

Spectral is one of five **modes** an oscillator can be in, chosen from a menu in
its header: Wavetable, Multisample, Sample, Granular, Spectral (p. 104). The
mode changes what the oscillator *is*, not merely what it is playing, and it
changes the module's whole control set with it.

In spectral mode the oscillator carries, top to bottom (pp. 105, 116):

- a **sample chooser** — factory tonal, factory non-tonal, Serum wavetables, or
  a file from disk;
- the tuning strip, `OCT / SEM / FIN / CRS`, unchanged from the other modes;
- a **spectrogram**, which is most of the module. Drag vertically to zoom
  (p. 106). Sample **start and end** markers are dragged on the body of it
  (p. 106); **frequency LO and HI** markers are dragged down its right-hand edge
  (p. 107), and both are modulation destinations you reach by dropping a source
  on them;
- a strip under it: the **loop mode** field, a gear that opens the unison
  settings, and **UNISON** (pp. 109, 111-113);
- two rows of controls: `SCAN / CUT - FILTER - MIX / PAN` over
  `WARP 1 / WARP 2 / LEVEL` (p. 116). `FILTER` is a wide thin well between CUT
  and MIX that draws the spectral filter mask and opens its editor when clicked.

**SCAN** sets the speed and direction the playhead moves through the
spectrogram, with a menu carrying its range (200/400/800%), reverse, key track,
tempo lock, sample-length-to-BPM, **phase lock** and **transients** (p. 116).
Key track is the one that matters most to the engine: with it off "the scan rate
is fixed regardless of the key played", which is only possible if scanning and
pitch are genuinely independent.

**CUT**, **FILTER** and **MIX** are the spectral filter: a cutoff, a drawable
gain-against-frequency mask with points and curves (pp. 117-119), and a wet/dry
blend (p. 121). The mask can instead be a factory preset (p. 120) or a wavetable
used as a curve, and is not editable in that last case.

**Frequency LO/HI** bound the spectrum that is resynthesised, with two toggles
on their right-click menu (p. 107): *Smooth*, a fourth-order Butterworth at each
boundary instead of a hard edge, and *Post Warp*, which moves the bounding to
after the spectral warps rather than before.

The **loop menu** is One-shot, Fwd Loop, Rev Loop, Fwd/Rev Loop, Tailed and
Manual, plus Relative Loop, Link Loop Length and Exit Loop on Release (p. 109).
**Manual** replaces the playhead with a red X|Y dot and repurposes SCAN as its
horizontal position, so playback position is modulated rather than running
(pp. 109, 114). The dot's vertical axis drives one of eight targets - level,
either warp, the filter's cutoff or mix, or either frequency bound (p. 115).

## Where Forge starts from

Forge has none of this, and that is the honest size of the job.

An oscillator is wavetable-only and there is no mode concept at all: no header
menu, no per-mode control set, nothing in the engine that branches on what kind
of oscillator this is. There is no sample loading and no sample playback —
[PLAN.md](PLAN.md) has sample sources (M14c) as not started. The one audio file
Forge reads today is a wavetable `.wav` of single-cycle frames, in
`Processor::importTable`, and it deliberately refuses to go looking for cycles
in arbitrary recorded audio.

What Forge does have that this builds straight onto:

- **`WavetableStore`** (`core/ForgeTableStore.h`) is a worked-out, lock-free
  hand-over of an immutable asset from the message thread to a voice, with a
  parity guard that decides when the old one can be freed. A spectral sample is
  the same problem and gets the same answer, not a second invention.
- **Pages** already hide and show a module's controls without rebuilding them.
  A mode is the same mechanism pointed at a different question, so the control
  set can change with the mode without the module framework growing a second
  idea.
- **`juce::dsp::FFT`** is already used, in `ForgeWavetable.h`, for band-limiting
  — on the message thread, at load, which is exactly where spectral analysis
  belongs too.

## The architecture, and the three decisions behind it

### Resynthesis is a phase vocoder, and the pitch shift happens inside it

The engine is a standard overlap-add phase vocoder. Analysis happens once when
a sample loads: an STFT over the file gives frames of magnitude and phase. A
voice keeps a read position in that spectrogram; when its output buffer runs
dry it interpolates a frame at that position, applies the frequency bounds, the
filter and the cutoff, advances the phases, inverse-transforms, windows and
overlap-adds.

The vocoder's **output** always runs at real time, one analysis hop per
synthesis hop. Its **input** advances through the spectrogram at whatever SCAN
asks for — that is the whole of the time-scaling, and it is the only structure
in which the manual's Key Track option means anything.

**Pitch is a shift inside the spectrum, not a resampling of the output.** Each
hop the spectrum is cut into regions, one per peak, and every region is moved
**whole**, by a whole number of bins, to where its peak's measured frequency
times the ratio lands; every bin in a region is locked to its peak's phase, and
only the peak's phase is carried from hop to hop (Laroche and Dolson's peak
shifting with scaled phase locking). The obvious alternative — resynthesise at
the original pitch and read the result faster — is what the first draft of this
document described, and it is wrong twice over. It drags the scan rate along with the pitch, so key track
would have to be a division cancelling it back out; and it makes the cost of a
note scale with how high it is, because a note an octave up consumes output
twice as fast and so needs twice the transforms. Shifting in the spectrum costs
the same at every note and leaves SCAN independent of pitch for nothing.

What it buys is the thing that is actually hard to get: a sample can be held at
a fixed point in its own time while being played as a chromatic instrument.

Moving regions whole rather than reading each output bin from wherever
`bin / ratio` lands is not a refinement; the bin-by-bin version was the first
one built and it lost level. A partial is not one bin — the window spreads it
across four, with the bins either side alternating in phase against the centre
— and that shape *is* the window coming back out. Read bin by bin, it was
crushed to half its width an octave down and stretched to double an octave up,
the overlap-add stopped summing to one, and the level went with it: 11 dB lost
an octave down on a real loop, 7 dB an octave up. Moved whole, the same loop
stays within about a decibel of itself across the two octaves. Where two
shifted regions land on one bin, the bin carries the phase of whichever is
louder there; letting the later one win cost a bright tone 3.6 dB an octave
down, because a harmonic would carry on next hop from its neighbour's phase.

Two things about the source are settled when it is read rather than left to
the knobs. A sample is **peak-normalised** when it is analysed, so its loudest
moment meets full scale where a wavetable's loudest frame already is — a
recording arrives at whatever level it was bounced at, and the loop this was
found on peaked at −5 dB with an RMS of −28. And the file's own **rate** scales
both the pitch and the scan, because a bin of a 44.1 kHz analysis is narrower
than the same bin resynthesised at 48 kHz: without it a CD-rate file played a
semitone and a half sharp and ran 9% fast.

### Unison is summed in the spectrum, so a stack costs one transform

A 2048-point inverse transform every 512 samples is about 94 per second per
voice. Sixteen voices across three oscillators is manageable; multiplying it by
a stack of twelve is not.

It does not have to be multiplied. Every member of the stack is resynthesising
the same spectrogram position at a slightly different ratio, so each member's
contribution is accumulated into **one** complex spectrum — two, in fact, left
and right, which is what lets the members be spread across the image — and the
transform is taken once. A member costs a pass over the bins rather than a
transform of its own.

What a member does need is a synthesis phase per bin of its own: members are at
different pitches, so their phases advance at different rates, and that
difference is the whole of what detune is. That is four kilobytes each, which is
why `spectralUnisonMax` is 6 rather than the wavetable stack's 12 — see the
memory note below.

The honest cost of this arrangement: Serum's **START** and **SPAN** give each
member a different *scan* offset, and its **WARP 1/2** spread gives each a
different warp depth. Neither survives a shared spectrogram position, because
both need the member to be reading somewhere else in the sample. They are
deferred below rather than approximated.

### The per-voice state is on the heap, and this is not optional

A voice's spectral state — a synthesis phase per bin per unison member, the
overlap-add buffers and the hop waiting to be read out — is about seventy
kilobytes per oscillator. Sixteen voices across three oscillators is roughly
three and a half megabytes per `Core`.

`Core` cannot absorb that. It is a header-only type held **by value** inside
`Processor`, and the test suites construct it **on the stack** —
`rhino::forge::Core core;` appears throughout `tests/`, and `oscillatorSuite()`
holds eight `Processor`s alive on one frame. The test binary already links with
`/STACK:8388608` because per-voice filter state once pushed it over a megabyte;
twenty-eight more megabytes of spectral state across those eight would overflow
it on the first run, and CTest reports a Windows stack overflow as a bare
`SegFault` with no output, which is a miserable thing to debug twice.

Therefore: `Core` holds **one heap-allocated block** of spectral voice state,
sized at `prepare()` and never on the audio thread, with the voice holding an
index into it. `sizeof(Core)` does not move. Nothing about this is a performance
decision — it is what keeps the existing tests running.

## The milestones

### M16a — an oscillator has a mode — done

`OscMode { wavetable, spectral }`, with room left for the three Serum modes
Forge is not building yet. A parameter per oscillator, a selector in the module
header where the `MORPH` badge is now, and controls that declare which modes
they appear on so the module shows and hides them exactly as pages already do.

Wavetable mode must come out of this bit-identical. The existing oscillator,
warp, table and voicing suites passing unchanged is the proof, and `--render`
against a worktree of the previous revision is the stronger one.

### M16b — a sample, loaded and owned — done

`Sample` (`core/ForgeSample.h`) is the immutable analysed form: the STFT, the
spectral flux each frame carries so a transient can be recognised, and the audio
itself for the display to draw. `SampleStore` (`core/ForgeSampleStore.h`)
publishes it to the audio thread with `WavetableStore`'s guard pattern rather
than a new one. `Processor::importSample` decodes any format JUCE can read,
mixes it to mono and analyses it, on the message thread.

A sample is chosen by clicking the oscillator's display, which opens Load
Sample / Clear Sample. The display is the chooser because the module has no free
cell for a field — see the mode decision in `ui/ForgeModules.h` — and because
the picture *is* the sample. The header names what is loaded, in the slot the
envelope names its stage in.

A sample travels **inside** the preset and inside host state, the way a drawn
table already does, so a patch carrying one is still a patch when it moves
between machines. There is deliberately no path stored and no missing-sample
state to recover from: a reference to a file on disk is the thing that breaks,
and Forge already promises its tables do not work that way.

What differs from a table is the encoding, because the sizes are not comparable.
A table is a few tens of kilobytes and deflates well; a sample is seconds of
arbitrary recording, where deflate buys almost nothing because audio is not
repetitive text. So it is written as **FLAC at 16 bits** and base64'd on top —
roughly a quarter of what the raw float would cost. Ninety-six decibels of range
is far below the floor the spectrogram is even drawn at, and the alternative
triples what every preset carrying a sample weighs. The spectrogram is *not*
stored: it is several times the size of the audio it came from, and re-analysing
at load is a few tens of milliseconds on the thread that is already opening a
file.

The suite round-trips a sample through host state and checks it comes back
audible, named, in spectral mode, and **smaller than the raw float would have
been** — that last one because a regression to uncompressed audio would be
invisible until a patch carrying ten seconds of it weighed twenty-five megabytes.

**Still outstanding:** there is no factory set under `samples/`, so every sample
is one you went and found.

This is also the substrate M14c needs for the noise module's sample sources, so
it is shared investment rather than spectral-only cost.

### M16c — the spectral engine — done

`core/ForgeSpectral.h` is the vocoder; `Core::renderSpectralOscillator` is what
tells it what to do. Pitch, SCAN in both directions, the frequency bounds with
their Butterworth skirts, CUT and MIX, transient preservation, and unison summed
in the spectrum.

Verified by measuring rendered audio, in the way `tuningSuite()` already does —
`ctest -R forge_spectral`. The root note comes back at the sample's own pitch,
an octave either way transposes by an octave, SCAN at zero, forward and
backward all render the same pitch, SCAN at two reaches the second half of a
sample in half the time, CUT takes a tone out, an oscillator with no sample is
silent, and a spectral oscillator changes nothing about a wavetable one beside
it. The level is held to within 1.5 dB of the sample at the root and an octave
either way, on a sine and on a bright harmonic tone; a sample is normalised on
analysis; and a 44.1 kHz file plays at its own pitch and its own speed at 48.

Two bugs that suite caught and that are worth knowing about, because both were
silent and neither was visible in the code:

- **The first frame had no previous frame to measure against.** Differencing it
  against itself looks harmless and is not: a measured advance of zero makes the
  unwrap snap every bin to a whole turn per hop, quantising the pitch to
  multiples of four bins. A 440 Hz sample came out at 468.75, 110 cents sharp,
  on any frozen playhead. The first frame now measures forwards.
- **A transient reset the phase on every hop that read it, not on arrival.** A
  playhead held still on an attack — SCAN at zero, an ordinary setting — reset
  to the same phase every hop, which is a periodic signal at the hop rate rather
  than the tone that is there.

The **loop modes** are built: ONE-SHOT, FWD LOOP, REV LOOP, FWD/REV and
MANUAL, travelling between four markers — START and END for the run, LS and LE
for the loop inside it — in `spectralAdvance`. The parameter declares Serum's
whole list, TAILED included, for the reason MODE does; TAILED is the one not
built, because the manual's description of it is the one in the menu that does
not say what the loop markers do, and it plays as FWD LOOP if a preset names it.
A voice is placed on its starting marker on its first hop rather than at
note-on, because note-on does not yet know which way SCAN points: forwards
starts at START, backwards at END, and "arriving at the loop" means reaching its
far end in the direction of travel. REV LOOP and FWD/REV both turn at that far
end, so for them the arrival is the turn; FWD LOOP is simply in the loop as soon
as it is inside it. A ONE-SHOT that reaches its end stops reading and lets the
overlap-add drain, so the last of the sample fades over the three hops it was
always going to take. The markers are proportions of the whole sample, so they
mean the same thing whatever is loaded.

Checked hop by hop off the voice: every mode starts on the right marker, a
one-shot ends at END and is then silent (also through the Processor's own
parameters), FWD LOOP stays inside the loop and wraps, REV LOOP runs backwards
round it, FWD/REV goes both ways about equally without ever jumping, and MANUAL
stays exactly where SCAN puts it.

**Still outstanding:** transients are always on, and the SCAN menu that would
turn them off does not exist yet. Its Phase Lock is a declared setting that
nothing reads — every region is locked to its peak regardless, because that is
what keeps the level, so whatever the option ends up meaning it is not that.

### M16d — the spectrogram, and the controls the manual draws — part done

The oscillator's display draws the spectrogram of whatever it is reading, on a
logarithmic frequency scale, each row read as its loudest bin rather than its
average — averaging is what makes a spectrogram look like fog. It is cached
against the sample's revision and the module's size and blitted on every other
frame, because the panel repaints whole at 24 Hz and a spectrogram is a pass
over every bin of every frame.

The **playhead** rides on it, reading where the loudest voice has got to — the
same voice the envelope and the LFO displays follow, and for the same reason.
It crosses from the audio thread the way the envelope's level does, published
once per block rather than per sample, and is drawn only while a note is
sounding: at rest the reading is zero, and a line pinned to the left edge of a
still picture reads as a marker somebody put there rather than as a position.

The **loop strip** is seated along the foot of the display — the LOOP field, LS
and LE, the last two wearing their captions inside the field because a seated
row has no label line — and the **markers** are drawn on the spectrogram:
START and END as lines down its height with what lies outside them dimmed, the
loop as a blue bar along its top that is lit while the mode loops and faint
while it does not. Either line, either end of the bar, or the bar whole can be
dragged; anywhere else on the picture is still the sample menu. One hit test
serves the cursor, the press and the drag, so what can be picked up is exactly
what is drawn.

That settles the question this section used to leave open: whether a strip under
the display is worth it to both modes, or the geometry learns about modes. It is
the second, and only as far as it has to. A seated row may now say it exists in
one mode (`Row::modeBy`); its strip is carved where it would be in any mode, and
after every strip that is always there, so no other row's rectangle depends on
the mode and none of the thirty-odd callers of the row geometry had to learn it.
The **plot** is the one thing that does change, and `displayPlotBounds` takes
the mode from the few places that draw into it. A wavetable oscillator keeps the
whole of its picture. The layout suite holds both halves: in its own mode the
plot gives the strip room, in every other mode the plot is exactly what it was.

START and END are markers and not fields because the strip is not wide enough at
the smallest window to give five fields a readable number each, and because the
manual puts them on the picture too (p. 106). `displayParameters` names them, so
the rule that every parameter appears somewhere on the panel still holds.

**Still outstanding:** the LO/HI markers down the right edge, and the unison
gear. The frequency bounds exist in the engine and are reachable only from code
until those markers are drawn. The `FILTER` well between CUT and MIX is part of
the mask editor and is deferred with it. In MANUAL the SCAN knob still reads as a
percentage of speed rather than as a position.

## Deferred, and why

Not half-built, not approximated — absent, with a note saying so.

- **The filter mask editor** (pp. 117-119). The mask *data* and its application
  are built in M16c; what is deferred is the dialog with its points, curves,
  multi-select, grid and Alt-constrained dragging. Until it exists the mask is
  flat and CUT and MIX do the work.
- **Filter presets and wavetable-as-filter** (p. 120). Both are ways of filling
  the mask, so both want the mask editor to exist first.
- **X|Y manual mode** (pp. 114-115) and its eight Y-axis targets. Manual
  *playback* is in M16c because it is one branch in the scan; the dot, the Y
  assignment and the modulation routing to it are UI and matrix work.
- **TAILED**, and the loop menu's toggles — Relative Loop, Link Loop Length,
  Exit Loop on Release — plus the loop crossfade (p. 110) and the reversed loop
  a modulated LE behind LS is meant to give. Today LE behind LS is read as the
  same loop the right way round.
- **The spectral unison extras** — STACK, RANGE, SPAN, START and the WARP 1/2
  spread (pp. 111-113). START and SPAN need per-member vocoders; see the unison
  decision above.
- **Post Warp** ordering for the frequency bounds (p. 107), which is entangled
  with the question below.
- **Sample, Granular and Multisample modes** (p. 104). The mode concept and the
  sample substrate are most of what they need, so they get much cheaper once
  this lands.
- **Switch to Wavetable** (p. 122) and **Import PNG** (p. 105).

## The open question: what warp means in spectral mode

Forge has thirty-eight warp modes in nine families, and they are not all
meaningful here. The families that bend *where in the cycle a table is read* —
nine of them — have no cycle to bend when the source is a continuous
resynthesised stream. The waveshapers and the cross-modulation families (FM, PD,
AM, RM) apply to the output stream perfectly well.

Serum's own answer is a separate set of *spectral* warps, which is what its
Post Warp option is ordering against. Forge has to choose between offering only
the warp families that transfer, offering a spectral set of its own, or applying
warp to the resynthesised stream and accepting that a third of the list is
musically inert.

This is not settled, and M16c should not quietly settle it by accident. Until it
is decided, warp in spectral mode applies to the output stream and the
table-read families are left out of the menu rather than left in it doing
nothing.

## Building and testing

Unchanged from [PLAN.md](PLAN.md). While working on this, the areas to run are
`oscillator`, `table`, `warp` and `voicing` for the regressions M16a must not
cause, plus a new `spectral` area added the way the others were — the `.cpp`, a
declaration in `tests/ForgeSuites.h`, a row in `tests/ForgeTestMain.cpp` and a
name in `forge_test_areas` in `CMakeLists.txt`.

```powershell
cmake --build instruments/rhino-forge/build --config Release --target RhinoForgeTests --parallel 2 -- /p:BuildInParallel=false
ctest --test-dir instruments/rhino-forge/build -C Release -R forge_spectral --output-on-failure
```
