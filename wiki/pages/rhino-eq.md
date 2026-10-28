---
title: Rhino EQ
type: component
summary: Eight-band EQ whose drawn curve comes from the same coefficients as the audio, over a spectrum computed off the audio thread.
tags: [rhino, devices, dsp, eq]
sources: []
updated: 2026-10-28
---

# Rhino EQ

Eight bands, the eight filter types of Live's EQ Eight, and a live spectrum behind the curve. The device, `native/src/devices/audio/RhinoEqDevice.*` (catalog id `Equaliser`, type `rhino.eq.v1`), is a thin shell; nearly everything else is in `native/src/core/dsp/`, where `--self-test` measures it without an edit.

## The DSP

- **`EqFilter.h`** is a pure header with no JUCE: the eight types (Low Cut 48 and 12, Low Shelf, Bell, Notch, High Shelf, High Cut 12 and 48), their RBJ biquad coefficients, and `eqBandMagnitudeDbAt`. The curve is evaluated from the coefficients the samples go through ([Displays draw from the DSP](displays-draw-from-the-dsp.md)).
- The 48 dB/octave cuts are an eighth-order Butterworth, four biquads with constant stage Qs, so the band Q does not reach them; the 12 dB cuts are one biquad and can resonate. The face greys out what a type ignores rather than hiding it, so a band keeps its settings across a type change.
- **`EqEngine`** smooths frequency in the log domain and gain and Q linearly, per block, recomputing coefficients only for a band that moved. Switching a band on or off, or its type, is crossfaded sample by sample with the outgoing filter running on a copy of its state; hence the two pairs of delays in `StageState`. State is double precision, because a 20 Hz Butterworth high pass at 48 kHz is audibly noisy in float.
- **`SpectrumAnalyser`** splits at the thread boundary. `SpectrumTap` only copies a mono sum into a ring on the audio thread; `SpectrumReader` windows it, runs a 2048-point FFT and folds 160 log bins in the panel's timer (`tickEqSpectrum`, 24 Hz), so a spectrum can never cost a dropout. Until 2026-10-26 the spectrum froze whenever the EQ had an automation lane and the song played (so always on an audio track), because the rack's automation follow restarted that timer before it came due ([Starting a running juce::Timer restarts its count](juce-timer-restart-resets-countdown.md)).

## Parameters and properties

The 24 per-band numbers plus output and Scale are automatable. Scale rides every band, so the curve is drawn from the scaled bands (`EqEngine::responseDbAt`). Band on/off, type and the analyser mode (Off, Pre, Post) are properties, so automation cannot switch a type. The face writes them through `Session::editDeviceSettings`, so each is an undo step of its own and is announced; before commit `63bb3b2` such a write joined whatever the user did before it. The selected band is stored on the device too, because the rack rebuilds its panels on unrelated edits, but without undo: it is a view, so undo passes it by and selecting a band does not mark the document changed ([Device rack and device editors](device-rack.md)).

A fresh EQ has bands 1 to 4 on, all at 0 dB, with band 4 selected (`bandDefaults` in `RhinoEqDevice.cpp`): a Low Cut 48 at 20 Hz, a low shelf at 120 Hz and bells at 250 Hz and 750 Hz; bands 5 to 8 are off. Until 2026-10-27 only band 4 was on and band 1 sat at 80 Hz. Band 1 moved to 20 Hz because, switched on at 80 Hz, every new EQ would cut the bass; at 20 Hz it removes only rumble and the EQ stays flat across the audible range. The user chose that over making band 1 a flat low shelf and over keeping the 80 Hz cut on. An unset property falls back to these defaults, so a document saved before then reopens with bands 1 to 3 on as well, and band 1 at 20 Hz if its frequency was never moved; nothing compensates ([No backward compatibility for .rhinoedit](no-rhinoedit-back-compat.md)), and no EQ presets ship under `library/Presets/`.

## The face

`native/src/ui/rack/DeviceEditorPanelEq.cpp`: the display is the control. Drag a band, wheel to widen the selected band, double-click a handle to switch its band off, right-click for its type; three knobs follow the band in hand and two are global. The response is a cached `juce::Path`, rebuilt only when a parameter moves.

A band that is off is not drawn on the display, as in EQ Eight (Live 12 manual p. 577), so it cannot be clicked, dragged or double-clicked; until 2026-10-27 it was a hollow ring and a double-click toggled it. It stays in the band column on the right, where its LED switches it back on, as do its right-click menu and the TYPE chooser (choosing a type switches an off band on). A press and a double-click share one hit test, `eqHandleAt`, which skips bands that are off and takes the nearest handle within reach, not the first in band order. The wheel still sets the selected band's Q while that band is off.

A double-click's second press begins the band's frequency and gain gestures, as any press on a handle does. Until 2026-10-28 the double-click handler cleared `eqDragging` without ending them, and the release that followed found nothing held. The two gestures then stayed open for good, which silenced every later knob announcement and would have held the undo timer off. The handler now ends them. Since the same date, Ctrl+Z moves the EQ's knobs and sound back, not only their stored values ([Undo and the history](undo-history.md#after-an-undo-or-a-redo)).

## Tests

`native/src/tests/EqTest.cpp` sweeps a sine through `EqEngine` and compares the measured level with `responseDbAt`: two independent routes to the same coefficients, so agreement is evidence rather than something built in. Its click check (no two consecutive samples more than 0.2 apart) failed on its first run, which is why type changes crossfade ([Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)). It also checks that a face setting undoes on its own, leaving the edit before it, and that undo passes a band selection by. `checkDevice` holds the fresh defaults: exactly bands 1 to 4 on, band 1 a Low Cut 48 at 20 Hz, and 0 dB at 50 Hz, 1 kHz and 10 kHz. `checkEditorFace` puts off band 6 at 3 kHz +12 dB and computes its handle's spot from the face's geometry (the display left of the band column under the 24 px name bar, 10 Hz to 22 kHz logarithmic, ±18 dB linear); a click there must not select it and a double-click must not switch it on, and once the band is switched on the same click must take it, which proves the spot.

Not yet matched to EQ Eight: stereo, L/R and M/S modes, adaptive Q, oversampling.

## Related

- [Built-in devices](built-in-devices.md)
- [Displays draw from the DSP](displays-draw-from-the-dsp.md)
- [Real-time audio rules](real-time-audio-rules.md)
- [Device rack and device editors](device-rack.md)
- [Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)
- [Starting a running juce::Timer restarts its count](juce-timer-restart-resets-countdown.md)
- [No backward compatibility for .rhinoedit](no-rhinoedit-back-compat.md)
- [Undo and the history](undo-history.md)
