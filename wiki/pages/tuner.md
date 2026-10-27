---
title: Tuner
type: component
summary: Live 12's Tuner on the device SDK - a meter that leaves the audio bit-identical, tracking 30 Hz to 2 kHz with Rhino Tune's YIN tracker and publishing a reading and a history as atomics to a hand-built face with Classic (Target or Strobe) and Histogram views, whose switches are the device's own controls.
tags: [rhino, devices, dsp, pitch, ui]
sources: []
updated: 2026-10-27
---

# Tuner

Modelled on Live 12's Tuner (manual pp. 649-652). Added on 2026-10-27 (uncommitted at the time of writing).
- **Catalog:** id `Tuner`, type `rhino.tuner.v1`, category Utilities.
- **What it is:** a measuring tool, not [Rhino Tune](rhino-tune.md), which corrects pitch.

## The device

`native/src/devices/audio/TunerDevice.*` is a `NativeAudioEffect` with no latency and no tail. Like Utility, it declares `singlePassThrough` and answers `getNumOutputChannelsGivenInputs` with its input count, so a mono chain stays mono ([Built-in devices](built-in-devices.md)).

Its controls are read by position (`referenceParameter` and the rest), so they are append only:
- Reference, 410-480 Hz in 1 Hz steps;
- View: Classic or Histogram;
- Classic Mode: Target or Strobe;
- Readout: Hertz or Cents.

View, Classic Mode and Readout are view state, declared as the device's own choice controls. They are saved with the device, and each click on the face is one undo step through `writeParameter`, as a generated chooser's is.

## The engine

`native/src/core/dsp/TunerEngine.*` works in five steps:

1. **Mix to mono.** It sums the channels.
2. **Track.** It feeds the mono signal to `PitchTracker`, prepared over a band of its own, 30 Hz to 2 kHz, low enough for a five-string bass's low B. For that it uses the tracker's `prepare(rate, lowestHz, highestHz)`; the frame is the shortest power of two that holds two periods of the lowest frequency.

   It also switches on the tracker's `setMultiplePeriodRefinement(true)`; nothing else does. After the usual interpolation, the tracker walks downhill on the raw difference function from the furthest multiple of the period the window reaches, interpolates there and divides back down. It keeps that result only if it agrees with the first estimate within half a sample. Dividing back down divides the interpolation's error by the multiple. Without it a saw at E6 (1318.5 Hz, a 36-sample period) read +1.45 cents; with it, the worst of the test's 54 cases is 0.04 cents.
3. **Pace.** It feeds at most a hop at a time and watches `analysisCount()`, so no analysis is overwritten before it is published.
4. **Smooth.** A new analysis within 40 cents of the reading is averaged in, in the log of the frequency. Past that, the reading jumps, because a new note was played. The last pitch is held for 0.3 s after the input loses one.
5. **Publish.** Relaxed atomics carry a reading and a 512-entry history ring, about 5.5 s at 48 kHz, to the face. An unvoiced analysis enters the history as 0.

## The face

`native/src/ui/rack/DeviceEditorPanelTuner.cpp` is `Face::Tuner`, chosen by the catalog id `Tuner` in `DeviceEditorPanel::setTarget`. It is 520 px wide (`tunerFaceWidth`).

**Layout.** View switches run down the left, the display sits in the middle, and the Reference knob, the face's one child control, is on the right ([Device rack and device editors](device-rack.md)).

**Views.** Green means within 3 cents, shading to red by about 25 cents off, in every view.
- **Classic, Target:** a ball on an arc of ±50 cents with a target circle, the note and octave, flat and sharp arrows, and the Hz or cents readout.
- **Classic, Strobe:** 36 lights whose pattern runs right when sharp and left when flat. It runs faster the further off the note is (capped at 40 cents) and stands still in tune.
- **Histogram:** the last 4 s against the 7 semitones around the note. The plot centres on the note being played and stays where it was between notes. Unlike Live's, it cannot be dragged to scroll or zoom; it is always automatic.

**Frames.** The face runs on the rack's 24 Hz face timer. `tickTuner` repaints only the display, and only when what it would draw has changed, or while the histogram still has something in its window to scroll. A tuner hearing nothing asks for no frames ([What a change costs the interface](ui-cost-of-a-change.md)).

## Tests

`native/src/tests/TunerTest.cpp` (`checkTuner`) checks:
- **Accuracy.** Sines and saws at nine notes from MIDI 23 to 88, each in tune and at +7 and -13 cents, 54 cases in all, read within 1 cent. The worst measured is 0.04 cents.
- **Reference.** A reference other than 440 moves the note, not the reading.
- **Hold.** The note stays through a moment's silence and goes once the silence lasts.
- **History.** The readings come back in order.
- **The device.** It leaves the audio bit-identical, adds no latency or tail, and keeps a mono input mono.
- **The face.** It has one knob, and its clicks switch view, mode and readout, each as one undo step.

All six CTest cases passed on the final tree.

`RHINO_DEVICE_SNAPSHOTS=<folder>` writes the face in four states, as `Tuner target.png`, `Tuner strobe.png`, `Tuner histogram.png` and `Tuner cents.png` ([Seeing the UI without taking the screen](headless-ui-snapshots.md)).

## Related

- [Rhino Tune](rhino-tune.md)
- [Built-in devices](built-in-devices.md)
- [Device rack and device editors](device-rack.md)
- [Real-time audio rules](real-time-audio-rules.md)
- [Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)
