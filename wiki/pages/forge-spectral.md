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

Five loop modes are built: ONE-SHOT, FWD LOOP, REV LOOP, FWD/REV and MANUAL, in which SCAN places the playhead anywhere in the sample. Serum's TAILED is declared, so MANUAL's index never moves, but plays as FWD LOOP.

Playback always spans the whole sample: `SpectralSpan` (`core/ForgeSpectral.h`) runs from frame 0 to the last frame and clamps the loop inside. The only markers are the loop's, `<osc>LoopStart` and `<osc>LoopEnd`, proportions of the sample dragged on the spectrogram: either line anywhere down its height, or the top bar to move the loop whole. The strip under the plot holds only the LOOP selector.

- **One pair, not two (commit `dbd223d`).** START/END trim markers and LS/LE fields used to sit beside the loop's pair, and switching between ONE-SHOT and FWD LOOP made the two pairs look alike. The loop's pair was kept, so ONE-SHOT and MANUAL can no longer trim to a region. `Processor::migrated` drops the retired `osc{A,B,C}Start`/`End` parameters from old presets and host state, so the format was not bumped ([Forge presets and state](forge-presets-and-state.md)).
- **Markers appear only where they act.** `spectralLoopReachesLoop` is false for ONE-SHOT and MANUAL. In those modes the markers are not drawn and cannot be grabbed. They are hidden rather than faded, because a faint marker still looks draggable. `loopMarkersSuite` (`tests/ForgeTestsSpectral.cpp`) sends the editor mouse events to check the cursor and the plot's pixels in every mode, and drags an end and the bar.

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
