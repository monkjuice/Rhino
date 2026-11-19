---
title: Multiband Dynamics
type: component
summary: Live 12's Multiband Dynamics on the device SDK - three bands split by Linkwitz-Riley crossovers that sum to an all-pass, each compressed or expanded above and below two thresholds by Rhino's own ratio convention (over 1 narrows the range, under 1 widens it), boosts capped at +24 dB, no latency, and a face of its own after Live's display, where each band's thresholds and ratios are dragged as blocks over meters of what goes in and out.
tags: [rhino, devices, dsp, dynamics, ui]
sources: []
updated: 2026-11-07
---

# Multiband Dynamics

Built on 2026-11-07 (commit `31a2e09`) when the user asked for "a multiband compressor", after Ableton Live 12's device of that name (Live 12 manual, ch. 28.26, pp. 603-607; the defaults read off its p. 603 screenshot). The same day (commit `14bd151`) it got a face of its own so the bands can be worked from the display ([The face](#the-face)).
- **Catalog:** id `MultibandDynamics`, type `rhino.multiband-dynamics.v1`, category Dynamics beside the [Compressor](compressor.md).
- **Device:** `native/src/devices/audio/MultibandDynamicsDevice.*` declares 39 controls on the SDK and reports each band's levels for the face's meters (`takeBandLevels`). The face finds controls by position through `lowBandParameter` ... `amountParameter` and `bandParameter(band, BandControl)`, which a test holds to the declared ids, so append, never reorder.
- **DSP:** `MultibandDynamicsEngine`, `native/src/core/dsp/MultibandDynamicsEngine.*`, plain C++ over `BiquadFilter.h` and `EqFilter.h`'s designs.
- **Face:** `native/src/ui/rack/DeviceEditorPanelMultiband.cpp` (where everything stands, the drawing and the meters) and `DeviceEditorPanelMultibandGestures.cpp` (what a point is, presses, drags and tooltips), sharing `DeviceEditorPanelMultibandInternal.h`; its arithmetic in the pure header `ui/rack/MultibandDisplayGeometry.h`.

No latency, no tail, no sidechain. The SDK cannot declare a sidechain yet ([Sidechains](sidechains.md)), so Live's sidechain section is left out.

## The split

Two fourth-order Linkwitz-Riley crossovers, each two RBJ Butterworth biquads (Q = 1/√2) a side. Low is LP1 and the rest HP1; mid is LP2 of the rest and high HP2 of it. The low band also passes the second-order all-pass at the upper crossover, which is what an LR4 pair sums to, so the three bands sum to an all-pass: flat within 0.0001 dB from 30 Hz to 15 kHz, measured.
- Every filter runs every sample whichever bands are split off, so a band switched in starts from the signal. Low Band and High Band each remove a split; with both off, the mid band is the whole signal. Switching a split is not crossfaded and may click.
- The upper crossover is held no lower than the lower one. Both glide in octaves (a 20 ms one-pole per sample) and are redesigned every 16 samples counted across blocks, so the output does not depend on block size.

## Controls

Per band: Active, Solo, Input and Output (±12 dB), Below Threshold and Ratio, Above Threshold and Ratio (thresholds -80 to 0 dB, defaults -60 and -20; ratios 0.25 to 100, default 1:1), Attack (0.1 ms to 1 s) and Release (1 ms to 3 s), defaulting to Live's 50/300 ms Low, 10/200 Mid and 5/100 High. Global: Low Band, Low Crossover (30-3000 Hz, 120), High Band, High Crossover (300-15000 Hz, 2500), Soft Knee and RMS (both on; RMS off reads peaks), Output (±12 dB), Time (10-1000%, scales every attack and release) and Amount (0-100%, scales every ratio's slope). Times read in Live's three figures (5.00 ms, 50.0 ms, 226 ms, 1.50 s); `31a2e09` printed "226.413 ms", and the crossovers could show every digit from 100 to 999 Hz through the SDK's own Hz reading. Both were fixed on 2026-11-07, the crossovers by the SDK's `placed` ("523 Hz") ([juce::String with no decimal places prints all of them](juce-string-zero-decimals-prints-all.md)).

## The gain computer

**Rhino's ratio convention.** A ratio R takes a slope of (1 - 1/R) × Amount from the level's distance past its threshold (`MultibandDynamicsEngine::slopeOf`, public so the face draws from it). Above the upper threshold R > 1 compresses downward and R < 1 expands upward; below the lower one R > 1 compresses upward and R < 1 expands downward. So over 1 always narrows the dynamic range and under 1 widens it. 100 reads `inf:1` and means slope 1; other readings are conventional, `4.00:1`. Live writes "1 : x" and its manual does not say which way x runs, so that notation was not copied.
- The lower threshold is held at or below the upper one (`curveOf`).
- Soft Knee is 6 dB wide, quadratic, centred on each threshold. Live does not document its width; 6 dB is Live Compressor's default knee.
- **Caps** (`maximumBoostDb`, `maximumCutDb`, `floorDb`): neither threshold raises a band more than 24 dB or cuts it more than 120 dB, and no level is read below -120 dB. Upward compression would otherwise lift the noise floor without limit, and upward expansion is unbounded.

## Detector and smoothing

One level per band for both channels (stereo linked): peak is the larger |x| with instant attack and a 20 ms decay; RMS is the mean of the channels' squares through a 30 ms one-pole. Each threshold's gain is then eased on its own, with Attack while it moves further from 0 dB in its own direction and Release while it comes back, as Live describes (for Below, attack is the signal falling past the threshold). The 20 ms and 30 ms detector constants are guesses, not Live's.

Active off crossfades the band over 10 ms to its split signal, its Input and Output bypassed too; Solo mutes the other bands over 10 ms. Input, Output and the device's Output glide over 20 ms.

## The face

`Face::Multiband`, chosen by catalog id, `multibandFaceWidth` 608 px ([Device rack and device editors](device-rack.md)). Left to right: LOW/HIGH split switches over their crossover readouts, each band's power (Active) and S (Solo), SOFT KNEE and RMS at the foot; each band's Input knob; the display between its threshold and ratio readouts; Attack and Release readouts; each band's Output knob; the device's Output, Time and Amount. A row per band, High at the top as in Live.

**The display.** Across Live's -80 to 0 dB the Below block runs from the left end to the lower threshold and the Above block from the upper threshold to 0 dB. A block's edge is its threshold, dragged sideways (80 dB across the 176 px column). Dragging the block up or down is its "volume", which is its ratio, "up is louder" as in Live (`MultibandDisplayGeometry.h`):
- volume 0 is 1:1. Compression takes the half of the travel it turns the block's signals towards (down in Above, up in Below) with its slope as the distance, so half way is 2:1 and the end infinite; expansion takes the other half, scaled so 0.25:1 (slope -3) reaches the end.
- 100 px moves a block from 1:1 to either end. Ctrl does the same on every band, Alt to both of the band's thresholds or both blocks, Shift a quarter as far. Ctrl and Alt are read at the press, Shift on every move. A threshold dragged without its partner stops at it. Double-click a block for 1:1.
- Each block's volume is drawn as a line above (green, louder) or below (amber, quieter) the row's middle with the ground shaded, over the meter bars so a level never hides it, and a faint middle line at 1:1.

**Readouts** (threshold, ratio, attack, release, crossovers) are drawn boxes dragged up and down across the control's own skewed travel (150 px for the whole range, Shift a quarter), double-clicked to the default, and right-clicked for the automation menu, opened at the mouse. The switches are drawn too, each click one undo step through `writeParameter`. The knobs follow their declared step and skew. Every drawn control carries its automation A on its corner. The cursor shows what a drag does: left-right over an edge, up-down over a block or readout, a hand over a switch.

**One undo step per drag.** The press opens a Session parameter gesture on every control it will move, the drag sets them with `setDeviceParameter`, and the release ends them. A double-click first ends the gestures its second press opened, or they stay open. Travel is clamped to what keeps every moved control in range, so turning back answers at once. The face re-reads each moved control from Session straight away (`readMultibandParameter`) and never holds a reference into `parameters` across a Session call, because the rack retargets the panel synchronously when it hears of the change.

**Meters.** Through each row a thin bar is the level coming in after Input (what the thresholds compare against) and a thick cyan bar the level going out after both thresholds and Output (and Solo), both read the detector's way. The out level is the detected level times the gain the audio applied (the band's factor over its Input), not a model ([Displays draw from the DSP](displays-draw-from-the-dsp.md)). `levels()` keeps the loudest of each `process` call; the device raises atomics, and `takeBandLevels` exchanges them with 0, the face being the one reader. The face's 24 Hz timer runs only while the device is enabled, feeds `MeterBallistics`, discards a reading gathered while nobody looked (the first frame, or over 0.5 s), and repaints only the meter column when a bar's pixel changes.

**What it replaced.** In `31a2e09` the face was generated, about 860 px with the bands as a "Bands" tab group, and `describe` drew each band's static curve from `MultibandDynamicsEngine::transferDb` with a `1:1` trace to hold the generated display's scale-to-the-loudest still. `describe`, its cache and the engine's public `staticGain` and `transferDb` went with it; the engine's `Gain` is private.

## Why the trims are ±12 dB

The conformance check feeds ±0.25 noise at 25 random settings and fails any output above 64. A scratchpad Monte Carlo of 3000 random settings: ±12 dB trims gave none over 64 (worst 46.9, 99th percentile 9.9), ±18 dB 0.47% (worst 164), ±24 dB 1.97% (worst 1880). So ±12 passes by design, not by the test's seed.

## Presets

`library/Presets/MultibandDynamics/`: Gentle Glue, Upward and Downward, and De-esser (the low split off, the high band at 4:1 from -30 dB above a 5 kHz crossover, peak detection). Like the other factory presets, each lists all 39 controls in declaration order, written by a scratch script that is not kept ([Device presets (.rnd)](device-presets.md)).

## Tests

`native/src/tests/MultibandDynamicsTest.cpp` (`checkMultibandDynamics`), called by `runDeviceConformance` in `tests/DeviceConformance.cpp` after the Compressor meter check. It measures rendered audio with a Hann-windowed single-frequency transform against arithmetic ([Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)):
- the split sums to the input within 0.01 dB at five frequencies; Solo; a compressed low band leaving 6 kHz untouched; each direction of both ratios, inf:1, the +24 dB cap, Amount 0, RMS against peak, an inactive band, Time at 10%;
- the meters: in reads the tone's peak after Input within 0.1 dB, out equals in with nothing done, and at 4:1 out is the measured sent level within 0.2 dB;
- the geometry header's round trips (dB and x, ratio and volume) and grips;
- the device: no latency or tail, its index constants against the ids, the time readings, meters lit by a tone and spent once taken (the onset reading is discarded first, since a sine's start rings into every band), and the Upward and Downward preset's values;
- `runMultibandFaceTest`, a friend of the panel: everything drawn and every visible knob and label on the face and disjoint, then real mouse events for Solo and its undo, an edge drag and its single undo, Shift, Ctrl's one undo for three bands, Alt, the lower threshold stopping at the upper, both blocks to 2:1, a double-click back to 1:1, a readout drag and LOW. `RHINO_DEVICE_SNAPSHOTS` writes `MultibandDynamics face.png`, its meters faked for the picture.

It runs in `--device-test`, not `--self-test`: put there first, the `--self-test` case hit its 30 s limit before reaching the check, with the user's RhinoDAW open ([Build and test Rhino](build-and-test-rhino.md)). `native_device_conformance` checks 20 devices, all six standard checks on this one with nothing pending; with the check moved, all six CTest cases passed on the working tree of `31a2e09`, conformance in 9.3-10.8 s. With the face (`14bd151`) all six passed again, conformance in 9.0-10.4 s and `native_device_correctness` in 27.7-28.4 s.

## Related

- [Built-in devices](built-in-devices.md)
- [Compressor](compressor.md)
- [Device rack and device editors](device-rack.md)
- [Tuner](tuner.md) and [Drum Rack](drum-rack.md), the other SDK devices with faces of their own
- [juce::String with no decimal places prints all of them](juce-string-zero-decimals-prints-all.md)
- [The native device standard](native-device-standard.md)
- [Oversampler and BiquadFilter](oversampler.md)
- [Device presets (.rnd)](device-presets.md)
- [Displays draw from the DSP](displays-draw-from-the-dsp.md)
