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

## Loops

There are five loop modes: ONE-SHOT, FWD LOOP, REV LOOP, FWD/REV and MANUAL; in MANUAL, SCAN sets the playhead's position. Playback runs from START to END, both dragged on the spectrogram, and loops between LS and LE in the strip below. All four are proportions of the sample. Serum's TAILED is declared but plays as FWD LOOP.

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
