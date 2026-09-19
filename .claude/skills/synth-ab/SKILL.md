---
name: synth-ab
description: Record and measure what Forge and another synth (usually Serum) actually sound like, instead of arguing from the DSP. Use whenever the user says a patch copied between them sounds different - thinner, wider, duller, more enveloping, less depth - or asks to A/B two synths, capture the loopback, or check whether a parameter change landed. Covers the Windows loopback capture, the spectral analysis, and the traps that have produced wrong answers here before.
---

# A/B-ing Forge against another synth

A claim about what a synth *sounds* like is settled by measuring rendered audio.
Reading the DSP cannot settle it, and neither can asking the user to describe
the sound more precisely — the words that start these sessions ("enveloping",
"depth", "thin") map onto measurements, and the measurement is the answer.

This is the same method as `tuningSuite()` in `ForgeTests.cpp`, pointed at a
capture instead of an offline render, which is what lets it cover a plugin whose
source we do not have.

## Before recording

Four things, all of which have cost a session before:

1. **Close the Forge standalone if the take is from a DAW.** It hears the same
   MIDI keyboard and plays the same note into the same capture. One take was
   identified as Forge, not Serum, purely from its spectrum.
2. **Check the DAW's audio driver.** On Realtek ASIO the loopback records pure
   silence. Ableton's `Preferences.cfg` lists the devices it *enumerated*, not
   the one selected, so only a take can answer this. `analyze.py` flags a
   near-silent capture with this cause.
3. **Match the settings deliberately, and write down what they are.** Note
   number, oscillator octaves, macro positions, and anything with RAND in it.
   A comparison of two different patches is worth nothing.
4. **Take both captures in the same session** at the same output level, so the
   band percentages are comparable.

## Record

```bash
python .claude/skills/synth-ab/scripts/capture.py --out serum.wav --label "Serum: hold C3"
```

A small always-on-top window appears with a Stop button. The user plays the
note, then presses Stop. **Do not use a fixed-length window by default** — no
one can read a message and play a note inside 25 seconds, and the take is lost.
`analyze.py` finds the note by its envelope, so recording long costs nothing.

- `--seconds N` for a headless fixed-length take (only when nothing needs
  playing, e.g. a running loop).
- `--list` to re-check the device names.
- The device is `Stereo Mix (Realtek(R) Audio)`, live and stereo-transparent: a
  hard-panned test reads side/mid 1.000, so a mono reading is real.
- Stopping writes `q` to ffmpeg's stdin. Killing it instead leaves a WAV header
  Python's `wave` module refuses to open.

## Measure

```bash
python .claude/skills/synth-ab/scripts/analyze.py serum.wav forge.wav
```

Prints both takes side by side: fundamental and nearest note with cents, the
partial series relative to the fundamental, spectral centroid, energy per band,
side/mid width per band, L/R correlation, attack and decay.

- `--peaks 12` also lists the strongest peaks with their ratio to f0 — this is
  how FM/PD sidebands get read.
- `--window 0.5:2.0` overrides the envelope search when a take has several notes
  in it.

## Reading the numbers

The point of the table is that it turns the user's word into a specific row.

| They said | Look at |
| --- | --- |
| enveloping, wide, three-dimensional | side/mid per band, L/R correlation |
| depth, weight, fullness | 20–100 and 100–500 Hz band energy, h1 vs h2 |
| thin, small | band energy below 500 Hz, and whether f0 itself is attenuated |
| bright, harsh, dull | spectral centroid, 2k–8k share, partial count |
| moving, alive, static | tail/head rms, and take two captures — a patch with RAND scatters |
| punchy, soft | attack to 90% |

Two cautions, both learned here:

- **Serum's upper sideband scatters ~24% between takes of one setting** when
  RAND is at 100, because carrier/modulator phase is random per note. Forge's
  start phases are a deterministic hash and hold to 1%. Judge a match on the
  lower sideband, the centroid and the band energies — never on the upper
  sideband alone.
- **A missing odd series means f0 landed an octave low**, on a sub or a
  sideband, not on the played note. `analyze.py` says so when it happens; take
  the warning rather than the note name.

## Identifying a capture before trusting it

Partial ratios and exact detune reconstruct which synth and which MIDI note a
take was. A saw's second harmonic sits at 0.50 of the fundamental, so a measured
0.46 is a harmonic and a measured 0.99 is a note sitting on top of one. A
capture whose OSC B sat +0.864 st sharp matched
`macro1 (0.072) x mod depth (0.5) x 24 st` in the saved preset, which is what
proved that take was Forge and not Serum.

## Then what

A measured difference is a number to aim at, not a verdict. Two have been closed
this way already:

- PD warp depth was ~9x Serum's at the same displayed percentage — fixed as a
  scale constant, `warpPdCycles = 0.42f` in `core/ForgeWarp.h`.
- A row copied knob-for-knob sat an octave out because Serum's matrix rows have
  a POL switch — Forge gained a per-row `BI` chip.

Re-record after the change at genuinely matched settings and confirm the number
moved. Then write the measurement down in the commit message; a percentage with
a spectrum behind it is the part that is hard to reproduce later.

## Environment

- `ffmpeg` / `ffplay`:
  `C:\Users\monk\Downloads\ffmpeg-master-latest-win64-gpl-shared\bin`, on PATH
  under Git Bash. `capture.py` falls back to that path if PATH lacks it.
- System Python 3.13 has `numpy` and `tkinter`. No virtualenv needed.
- Write captures to the session scratchpad, not into the repo.
