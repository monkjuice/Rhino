---
title: Rhino EQ
type: component
summary: Eight-band EQ whose drawn curve comes from the same coefficients as the audio, over a spectrum computed off the audio thread.
tags: [rhino, devices, dsp, eq]
sources: []
updated: 2026-10-05
---

# Rhino EQ

Eight bands, the eight filter types of Live's EQ Eight, and a live spectrum behind the curve. The device, `native/src/devices/audio/RhinoEqDevice.*` (catalog id `Equaliser`, type `rhino.eq.v1`), is a thin shell; nearly everything else is in `native/src/core/`, where `--self-test` measures it without an edit.

## The DSP

- **`EqFilter.h`** is a pure header with no JUCE: the eight types (Low Cut 48 and 12, Low Shelf, Bell, Notch, High Shelf, High Cut 12 and 48), their RBJ biquad coefficients, and `eqBandMagnitudeDbAt`. The curve is evaluated from the coefficients the samples go through ([Displays draw from the DSP](displays-draw-from-the-dsp.md)).
- The 48 dB/octave cuts are an eighth-order Butterworth, four biquads with constant stage Qs, so the band Q does not reach them; the 12 dB cuts are one biquad and can resonate. The face greys out what a type ignores rather than hiding it, so a band keeps its settings across a type change.
- **`EqEngine`** smooths frequency in the log domain and gain and Q linearly, per block, recomputing coefficients only for a band that moved. Switching a band on or off, or its type, is crossfaded sample by sample with the outgoing filter running on a copy of its state; hence the two pairs of delays in `StageState`. State is double precision, because a 20 Hz Butterworth high pass at 48 kHz is audibly noisy in float.
- **`SpectrumAnalyser`** splits at the thread boundary. `SpectrumTap` only copies a mono sum into a ring on the audio thread; `SpectrumReader` windows it, runs a 2048-point FFT and folds 160 log bins in the panel's timer, so a spectrum can never cost a dropout.

## Parameters and properties

The 24 per-band numbers plus output and Scale are automatable. Scale rides every band, so the curve is drawn from the scaled bands (`EqEngine::responseDbAt`). Band on/off, type and the analyser mode (Off, Pre, Post) are properties, so automation cannot switch a type. The face writes them through `Session::editDeviceSettings`, so each is an undo step of its own and is announced; before commit `63bb3b2` such a write joined whatever the user did before it. The selected band is stored on the device too, because the rack rebuilds its panels on unrelated edits, but without undo: it is a view, so undo passes it by and selecting a band does not mark the document changed ([Device rack and device editors](device-rack.md)).

## The face

`native/src/DeviceEditorPanelEq.cpp`: the display is the control. Drag a band, wheel to widen it, double-click to switch it off, right-click for its type; three knobs follow the band in hand and two are global. The response is a cached `juce::Path`, rebuilt only when a parameter moves.

## Tests

`native/src/tests/EqTest.cpp` sweeps a sine through `EqEngine` and compares the measured level with `responseDbAt`: two independent routes to the same coefficients, so agreement is evidence rather than something built in. Its click check (no two consecutive samples more than 0.2 apart) failed on its first run, which is why type changes crossfade ([Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)). It also checks that a face setting undoes on its own, leaving the edit before it, and that undo passes a band selection by.

Not yet matched to EQ Eight: stereo, L/R and M/S modes, adaptive Q, oversampling.

## Related

- [Built-in devices](built-in-devices.md)
- [Displays draw from the DSP](displays-draw-from-the-dsp.md)
- [Real-time audio rules](real-time-audio-rules.md)
- [Device rack and device editors](device-rack.md)
- [Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)
