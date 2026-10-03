---
title: Forge spectral oscillator
type: component
summary: An oscillator mode that resynthesises a loaded sample with a phase vocoder, so pitch and scan position move independently (M16).
tags: [forge, oscillators, spectral, samples]
sources: []
updated: 2026-10-03
---

# Forge spectral oscillator

SPECTRAL is the second oscillator MODE: M16, built on 2026-10-02 and 2026-10-03, with the plan in `SPECTRAL.md`. It analyses a sample into STFT frames and resynthesises them with an overlap-add phase vocoder (`core/ForgeSpectral.h`). SCAN, CUT and MIX take the cells of POSITION, DETUNE and BLEND. MODE uses Serum's numbering (spectral is 4), so the modes not yet built will need no migration.

## Engine decisions (SPECTRAL.md, "The architecture")

- **Pitch shifts inside the spectrum.** Each peak's region moves whole, with its phases locked to the peak (Laroche–Dolson). A bin-by-bin shift lost 11 dB an octave down. This keeps SCAN independent of pitch and makes every note cost the same.
- **Unison is summed in the spectrum.** Each channel takes one inverse transform. Each member still costs 4 KB of phases, so `spectralUnisonMax` is 6.
- **Per-voice state lives on the heap.** It is about 70 KB per oscillator and 3.5 MB per Core, allocated in `initialise`. It cannot live inside `Core`, which is held by value and built on the stack by the tests.
- **Analysis** uses a 2048-point Hann window and a 512-sample hop. The root is MIDI 60, labelled C3 on Forge's keyboard.

## Samples

- **Import:** `Processor::importSample` decodes the file on the message thread, averages it to mono, caps it at 960,000 samples (20 s at 48 kHz) and analyses it. `Sample` (`core/ForgeSample.h`) peak-normalises it, and `SampleStore` passes it to the audio thread through `WavetableStore`'s odd/even guard.
- **Sample rate:** the file's own rate scales both pitch and scan; without it, a 44.1 kHz file played sharp.
- **Saving:** presets and host state carry the sample as a `SAMPLE` node of 16-bit FLAC in base64, with no path. The spectrogram is recomputed on load ([Forge presets and state](forge-presets-and-state.md)).

## Loops and markers

Five loop modes are built: ONE-SHOT, FWD LOOP, REV LOOP, FWD/REV and MANUAL, in which SCAN places the playhead between START and END. Serum's TAILED is declared, so MANUAL's index never moves, but plays as FWD LOOP.

**Each mode reads exactly one pair of markers**, all proportions of the sample: `<osc>Start`/`End` (defaults 0 and 1) or `<osc>LoopStart`/`LoopEnd`. `SpectralSpan` (`core/ForgeSpectral.h`) chooses by `spectralLoopReachesLoop(mode)`. In a loop mode (FWD LOOP, REV LOOP, FWD/REV, TAILED) the run is the whole sample and START/END are ignored. In ONE-SHOT and MANUAL the run is START..END, ordered and at least one frame. The loop is clamped inside the run.

- **Two pairs, one shown (commits `dbd223d`, `bbc2b85`).** Showing both pairs made ONE-SHOT and FWD LOOP look alike, so `dbd223d` removed START/END, which left ONE-SHOT and MANUAL unable to trim. At the user's request `bbc2b85` restored them under the same ids and solved the confusion by showing only the pair the mode reads. A state saved between the two commits lacks them, and `Processor::migrated` adds them at their defaults ([Forge presets and state](forge-presets-and-state.md)).
- **Only the mode's pair is drawn or grabbed.** `spectralMarkerAt` and `paintSpectralMarkers` (`src/ForgeEditorSpectral.cpp`) offer START/END in ONE-SHOT and MANUAL: accent-coloured lines with foot tabs, the outside dimmed. In the loop modes they offer the blue loop bracket instead: either end, or the top bar to move it whole. Never both, and the other pair is hidden rather than faded, because a faint marker still looks draggable. `markerParameters(SpectralMarker)` lists what each drag moves, and gestures open and close only on those.
- **No value fields.** The strip under the plot holds only the LOOP selector. The user does not want numeric or draggable fields there; markers are set on the plot.
- **Tested by pointer.** `loopMarkersSuite` (`tests/ForgeTestsSpectral.cpp`) sends the editor mouse events in every mode to check what is offered and drawn, and that each drag moves only its own parameters. A drag lands on a pixel, so a marker aimed at 0.15 reads something like 0.14841. To check that a later drag left it alone, compare with the value read after the first drag, not with the target.

## The display

A spectral oscillator draws in a flat, square well (`ui::drawSpectralWell`, `ui/ForgeDisplays.h`), not the CRT tube the wavetable keeps (`src/ForgeEditorPaint.cpp`). On the bowed tube the spectrogram's corners were cut off and its edges ran under the bezel. `Editor::spectralWellFor` spans the display's full width above the loop strip, and `spectralPlotFor` is that well inset by `ui::spectralWellWall` (1 px), so the spectrogram fills it. Tests derive the plot geometry from that constant.

## Two silent bugs

- **First frame:** comparing it with itself quantised the pitch, and a 440 Hz sample played 110 cents sharp.
- **Transients:** resetting phase on every hop over a transient, not just on arrival, made a frozen playhead buzz.

## Unfinished, and a doc conflict

Not built yet: the LO/HI markers, the mask editor, the unison gear, and the SCAN menu (transients are always on).

`SPECTRAL.md` says warp applies to the output stream. In the code, `renderSpectralOscillator` ignores both warp stages, so the warp row does nothing in spectral mode (checked 2026-10-03). A warp elsewhere with a spectral oscillator as its OSC source most likely gets a constant, since it reads that
oscillator's wavetable at a phase nothing advances (traced in the code, not measured;
[Hazards found while seeding the wiki](known-hazards.md)).

`levelSuite` (`ctest -R forge_spectral`) holds level within 1.5 dB. If spectral "sounds quiet", measure `spectralRead` on the user's actual file at several notes ([Forge engine (Core)](forge-engine.md)).

## Related

- [Forge oscillators and wavetables](forge-oscillators.md)
- [Forge warp](forge-warp.md)
- [Forge presets and state](forge-presets-and-state.md)
- [Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)
