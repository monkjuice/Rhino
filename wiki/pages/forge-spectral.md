---
title: Forge spectral oscillator
type: component
summary: An oscillator mode that resynthesises a loaded sample with a phase vocoder, so pitch and scan position move independently (M16).
tags: [forge, oscillators, spectral, samples]
sources: []
updated: 2026-10-03
---

# Forge spectral oscillator

SPECTRAL is the second oscillator MODE, built on 2026-10-02 and 2026-10-03. It analyses a sample into STFT frames and resynthesises them with an overlap-add phase vocoder (`core/ForgeSpectral.h`). SCAN, CUT and MIX take the cells of POSITION, DETUNE and BLEND. MODE uses Serum's numbering (spectral is 4), so the modes not yet built will need no migration.

## Engine decisions

- **Pitch shifts inside the spectrum.** Each peak's region moves whole, with its phases locked to the peak (Laroche–Dolson). A bin-by-bin shift lost 11 dB an octave down. This keeps SCAN independent of pitch and makes every note cost the same.
- **Unison is summed in the spectrum.** Each channel takes one inverse transform. Each member still costs 4 KB of phases, so `spectralUnisonMax` is 6.
- **Per-voice state lives on the heap.** It is about 70 KB per oscillator and 3.5 MB per Core, allocated in `initialise`. It cannot live inside `Core`, which is held by value and built on the stack by the tests.
- **Analysis** uses a 2048-point Hann window and a 512-sample hop. The root is MIDI 60, labelled C3 on Forge's keyboard.

## Samples

- **Import:** `Processor::importSample` decodes the file on the message thread, averages it to mono, caps it at 960,000 samples (20 s at 48 kHz) and analyses it. `Sample` (`core/ForgeSample.h`) peak-normalises it, and `SampleStore` passes it to the audio thread through `WavetableStore`'s odd/even guard.
- **Sample rate:** the file's own rate scales both pitch and scan; without it, a 44.1 kHz file played sharp.
- **Saving:** presets and host state carry the sample as a `SAMPLE` node of 16-bit FLAC in base64, with no path. The spectrogram is recomputed on load ([Forge presets and state](forge-presets-and-state.md)).

## Loops and markers

Five loop modes are built: ONE-SHOT, FWD LOOP, REV LOOP, FWD/REV and MANUAL, in which SCAN alone places the playhead in the whole sample. Serum's TAILED is declared, so MANUAL's index never moves, but plays as FWD LOOP. The manual describes TAILED as playing from halfway through the sample to the end and looping that tail as the note decays; whether to build it is an open question for the user (as of commit `61c515b`).

**Serum's semantics (User Guide pp. 77-78; p. 109 defers to them), followed since `61c515b`.** START is where the note begins. LoopStart/LoopEnd (LS/LE) are the repeating region, independent of START. ONE-SHOT plays START to END once. FWD LOOP plays forward from START to LE, then repeats LS→LE. REV LOOP plays forward from START to LE, then loops LE→LS backwards (the note does not start backwards). FWD/REV plays forward from START to the loop, then ping-pongs LS↔LE. The manual's one drag rule is that the loop markers cannot be dragged outside the playback start/end markers. Rhino enforces only the START side, because a loop mode shows no END.

**Markers**, all proportions of the sample: `<osc>Start`/`End` (defaults 0 and 1) and `<osc>LoopStart`/`LoopEnd`. Three predicates in `core/ForgeSpectral.h` say which a mode reads, and `SpectralSpan`, the hit test and the painter all ask them: `spectralReadsStart` (every mode but MANUAL), `spectralReadsEnd` (ONE-SHOT only) and `spectralReadsLoop` (FWD LOOP, REV LOOP, FWD/REV, TAILED). `SpectralSpan::onset` is where a forward voice begins. ONE-SHOT travels START..END from START. A loop mode travels the whole sample from START, and its loop is clamped only to the sample, never to START or END, so END before LE does not cut the loop. MANUAL travels the whole sample. With SCAN negative, ONE-SHOT starts at END and a loop mode at the sample's end. `spectralAdvance` was already correct and did not change in `61c515b`.

- **History (commits `dbd223d`, `bbc2b85`, `ab7fd45`, `b24d30a`, `61c515b`).** Showing both pairs made ONE-SHOT and FWD LOOP look alike, so `dbd223d` removed START/END. `bbc2b85` restored them under the same ids, for ONE-SHOT and MANUAL only. `ab7fd45` took them off MANUAL. `b24d30a` put START/END into the loop modes but clamped the loop inside START..END. That was the bug: START dragged past LS moved the loop you heard. `61c515b` separated START from the loop as the manual does and dropped END from the loop modes. Lesson: read the manual (location in [Development environment](development-environment.md)) before inventing marker semantics. A state saved between `dbd223d` and `bbc2b85` lacks START/END, and `Processor::migrated` adds them at their defaults ([Forge presets and state](forge-presets-and-state.md)).
- **MANUAL reads no marker (commit `ab7fd45`).** The user wanted MANUAL to show only where the scan is. Its position is `spectralManualPosition(scan)`, `jlimit(0, 1, scan * 0.25 + 0.5)` of the whole sample. The voice (`core/ForgeCore.h`) and the panel both call it, so the drawn playhead is where the voice reads. Before `ab7fd45` MANUAL swept START..END.
- **Only what the mode reads is drawn or grabbed.** `spectralMarkerAt` and `paintSpectralMarkers` (`src/ForgeEditorSpectral.cpp`) offer START and END in ONE-SHOT, and START plus the blue loop bracket (no END) in the loop modes. START and END are accent-coloured lines with foot tabs (`markerTab`, 6 px); the area before START is dimmed, and after END in ONE-SHOT only. Drag either end of the bracket, or drag its top bar to move it whole. The bracket is drawn and hit-tested at `orderedLoop(ls, le)`, just min/max, because the engine reads a reversed pair the right way round. MANUAL offers nothing. A marker the mode does not read is hidden rather than faded, because a faint marker still looks draggable. `markerParameters(SpectralMarker)` lists the parameters each drag moves, and gestures open and close only on those.
- **Coinciding markers are told apart by height.** LS and START both default to 0, and moving START up to LS is how to begin on the loop, so they often coincide. In the top bar zone the loop wins. In the foot-tab zone (the bottom `markerTab` + 2 px) START and END win. In between, START and END are checked first.
- **Drag limits.** In ONE-SHOT, START and END keep their order within 0..1, at least `markerMinimumGap` (0.5% of the sample) apart. In a loop mode, START stops at min(LS, LE) and LS stops at START, so they never cross; LE is free up to the sample's end, since END does not constrain a loop mode. The bar drag stops at START and at the sample's end instead of squeezing the loop.
- **No value fields.** The strip under the plot holds only the LOOP selector. The user does not want numeric or draggable fields there; markers are set on the plot.
- **Tested by pointer.** `loopMarkersSuite` (`tests/ForgeTestsSpectral.cpp`) sends the editor mouse events in every mode. It checks what is offered and drawn (START but not END in the loop modes), that each drag moves only its own parameters, and that a loop start sitting on START is grabbed as the loop on the bar and as START at the foot. It also checks that START cannot pass the loop's start and that LE can be dragged past END. For MANUAL it checks that no marker is offered and that, with no note playing, a bright column sits at `spectralManualPosition(SCAN)` along the top rows and moves when SCAN does (a 440 Hz sine leaves those rows dark). A drag lands on a pixel, so a marker aimed at 0.15 reads something like 0.14841. To check that a later drag left it alone, compare with the value read after the first drag, not with the target. Engine tests in the same file follow each loop mode's first pass hop by hop: forward without interruption from START to LE, then FWD LOOP jumps to LS while REV LOOP and FWD/REV turn back. They also check that START inside the loop leaves the loop where it is, that END before LE does not cut the loop, and the backward starts (END for ONE-SHOT, the sample's end for loop modes).

## The display

A spectral oscillator draws in a flat, square well (`ui::drawSpectralWell`, `ui/ForgeDisplays.h`), not the CRT tube the wavetable keeps (`src/ForgeEditorPaint.cpp`). On the bowed tube the spectrogram's corners were cut off and its edges ran under the bezel. `Editor::spectralWellFor` spans the display's full width above the loop strip, and `spectralPlotFor` is that well inset by `ui::spectralWellWall` (1 px), so the spectrogram fills it. Tests derive the plot geometry from that constant.

The playhead (`src/ForgeEditorPaint.cpp`) is normally drawn only while a note sounds (`processor.scanPosition > 0`): at rest the reading is zero, and a line pinned to the left edge looks like a marker. MANUAL always draws it, at `spectralManualPosition(<osc>Scan)` while no voice reports a position and at the voice's position while a note plays, which carries any modulation of SCAN. A reported 0 counts as no voice. The x is clamped so position 1 stays inside the plot.

## Two silent bugs

- **First frame:** comparing it with itself quantised the pitch, and a 440 Hz sample played 110 cents sharp.
- **Transients:** resetting phase on every hop over a transient, not just on arrival, made a frozen playhead buzz.

## Unfinished and a known warp gap

Not built yet: the LO/HI markers, the mask editor, the unison gear, and the SCAN menu (transients are always on).

`renderSpectralOscillator` ignores both warp stages, so the warp row does nothing in spectral mode (checked 2026-10-03). A warp elsewhere with a spectral oscillator as its OSC source most likely gets a constant, since it reads that
oscillator's wavetable at a phase nothing advances (traced in the code, not measured;
[Hazards found while seeding the wiki](known-hazards.md)).

`levelSuite` (`ctest -R forge_spectral`) holds level within 1.5 dB. If spectral "sounds quiet", measure `spectralRead` on the user's actual file at several notes ([Forge engine (Core)](forge-engine.md)).

## Related

- [Forge oscillators and wavetables](forge-oscillators.md)
- [Forge warp](forge-warp.md)
- [Forge presets and state](forge-presets-and-state.md)
- [Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)
