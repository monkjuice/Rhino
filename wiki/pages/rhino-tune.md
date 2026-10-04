---
title: Rhino Tune
type: component
summary: Vocal pitch correction with YIN tracking and PSOLA shifting, real reported latency, and correction smoothed on the offset.
tags: [rhino, devices, dsp, pitch]
sources: []
updated: 2026-10-03
---

# Rhino Tune

Vocal pitch correction modelled on Live's Auto Shift. The device, `native/src/devices/audio/AutoTuneDevice.*` (`rhino.autotune.v1`), only hands parameters to `AutoTuneEngine`; tracking, correction and shifting are plain C++ in `native/src/core/`, which `--self-test` drives with a synthesised vowel.

## The DSP

- **`PitchTracker`** is YIN, its difference function computed through a small radix-2 transform in the file rather than directly, which would cost hundreds of millions of operations a second. The cumulative-mean normalisation reports the fundamental rather than the loudest partial, and taking the *first* lag under the threshold, not the deepest, stops octave-low answers on a rich voice.
- **`PsolaShifter`** is pitch-synchronous overlap-add, not a phase vocoder: one grain per glottal pulse, so formants stay put, and resampling a grain without moving the marks shifts formants alone. It is monophonic by construction, which is why the device says vocals.
- **`ScaleQuantizer.h`** is a pure header, checkable exhaustively. Toggling a key leaves a mask no named scale matches, and the chooser reads Custom.
- Correction is smoothed on the **offset** between detected note and target, never on the target. Vibrato swings the offset at 5-7 Hz, too fast for a slow retune to follow, while the drift underneath is slow enough to remove; smoothing the target irons vibrato flat.

## Latency

Real, and reported through `getLatencySeconds`: half the analysis frame, which centres the estimate in its window, plus 3.5 of the longest period the range tracks. At 48 kHz that is 34 ms for High (150-1600 Hz) and 120 ms for Bass (45-500 Hz). Live mode cuts the framing term to an eighth of the frame, at the price of slips at note onsets.

Pitfall: the graph reads latency only when it is built. Switching Range or Live calls `changed()` and the face restarts a running transport; otherwise delay compensation keeps the old figure and the voice drifts out of time.

## Parameters and face

Thirteen automatable parameters cover correction, transposition, formants, vibrato and mix; root, scale, note mask, range and the two switches are properties. The face, `native/src/DeviceEditorPanelAutoTune.cpp`, draws and hit-tests its meter, keyboard, choosers and range lamps by rectangle ([Device rack and device editors](device-rack.md)). After a vocoder it tunes the result; before one it feeds it a tuned voice ([Device chain order](device-chain-order.md)).

## Tests

`native/src/tests/AutoTuneTest.cpp` measures pitch with a windowed transform scanned across a band, which never looks for a period, so its agreement with the tracker is not built in. Two checks were wrong first: one asserted C# corrects to C in C major, a tie that a hundredth of a semitone flips; another measured a formant shift by the tallest partial, which jumped harmonics while the shift worked. Prefer a statistic over the whole spectrum ([Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)).

## Related

- [Built-in devices](built-in-devices.md)
- [Rhino Vocoder and sidechains](rhino-vocoder.md)
- [Device rack and device editors](device-rack.md)
- [Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)
- [Real-time audio rules](real-time-audio-rules.md)
