---
title: Multiband Dynamics
type: component
summary: Live 12's Multiband Dynamics on the device SDK - three bands split by Linkwitz-Riley crossovers that sum to an all-pass, each compressed or expanded above and below two thresholds by Rhino's own ratio convention (over 1 narrows the range, under 1 widens it), boosts capped at +24 dB, no latency, and a display of each band's static curve drawn by the function the gain computer runs.
tags: [rhino, devices, dsp, dynamics]
sources: []
updated: 2026-11-07
---

# Multiband Dynamics

Built on 2026-11-07 (commit `31a2e09`) when the user asked for "a multiband compressor", after Ableton Live 12's device of that name (Live 12 manual, ch. 28.26, pp. 603-607; the defaults read off its p. 603 screenshot).
- **Catalog:** id `MultibandDynamics`, type `rhino.multiband-dynamics.v1`, category Dynamics beside the [Compressor](compressor.md).
- **Device:** `native/src/devices/audio/MultibandDynamicsDevice.*` declares 39 controls on the SDK and draws the display. The face is generated, the bands a "Bands" tab group (Low, Mid, High), about 860 px wide. Controls are addressed by position, so append only.
- **DSP:** `MultibandDynamicsEngine`, `native/src/core/dsp/MultibandDynamicsEngine.*`, plain C++ over `BiquadFilter.h` and `EqFilter.h`'s designs.

No latency, no tail, no sidechain. The SDK cannot declare a sidechain yet ([Sidechains](sidechains.md)), so Live's sidechain section is left out.

## The split

Two fourth-order Linkwitz-Riley crossovers, each two RBJ Butterworth biquads (Q = 1/√2) a side. Low is LP1 and the rest HP1; mid is LP2 of the rest and high HP2 of it. The low band also passes the second-order all-pass at the upper crossover, which is what an LR4 pair sums to, so the three bands sum to an all-pass: flat within 0.0001 dB from 30 Hz to 15 kHz, measured.
- Every filter runs every sample whichever bands are split off, so a band switched in starts from the signal. Low Band and High Band each remove a split; with both off, the mid band is the whole signal. Switching a split is not crossfaded and may click.
- The upper crossover is held no lower than the lower one. Both glide in octaves (a 20 ms one-pole per sample) and are redesigned every 16 samples counted across blocks, so the output does not depend on block size.

## Controls

Per band: Active, Solo, Input and Output (±12 dB), Below Threshold and Ratio, Above Threshold and Ratio (thresholds -80 to 0 dB, defaults -60 and -20; ratios 0.25 to 100, default 1:1), Attack (0.1 ms to 1 s) and Release (1 ms to 3 s), defaulting to Live's 50/300 ms Low, 10/200 Mid and 5/100 High. Global: Low Band, Low Crossover (30-3000 Hz, 120), High Band, High Crossover (300-15000 Hz, 2500), Soft Knee and RMS (both on; RMS off reads peaks), Output (±12 dB), Time (10-1000%, scales every attack and release) and Amount (0-100%, scales every ratio's slope).

## The gain computer

**Rhino's ratio convention.** A ratio R takes a slope of (1 - 1/R) × Amount from the level's distance past its threshold. Above the upper threshold R > 1 compresses downward and R < 1 expands upward; below the lower one R > 1 compresses upward and R < 1 expands downward. So over 1 always narrows the dynamic range and under 1 widens it. 100 reads `inf:1` and means slope 1; other readings are conventional, `4.00:1`. Live writes "1 : x" and its manual does not say which way x runs, so that notation was not copied.
- The lower threshold is held at or below the upper one (`curveOf`).
- Soft Knee is 6 dB wide, quadratic, centred on each threshold. Live does not document its width; 6 dB is Live Compressor's default knee.
- **Caps** (`maximumBoostDb`, `maximumCutDb`, `floorDb`): neither threshold raises a band more than 24 dB or cuts it more than 120 dB, and no level is read below -120 dB. Upward compression would otherwise lift the noise floor without limit, and upward expansion is unbounded.

## Detector and smoothing

One level per band for both channels (stereo linked): peak is the larger |x| with instant attack and a 20 ms decay; RMS is the mean of the channels' squares through a 30 ms one-pole. Each threshold's gain is then eased on its own, with Attack while it moves further from 0 dB in its own direction and Release while it comes back, as Live describes (for Below, attack is the signal falling past the threshold). The 20 ms and 30 ms detector constants are guesses, not Live's.

Active off crossfades the band over 10 ms to its split signal, its Input and Output bypassed too; Solo mutes the other bands over 10 ms. Input, Output and the device's Output glide over 20 ms.

## The display

`describe` draws one trace per band that is split off from `MultibandDynamicsEngine::transferDb`, the static curve the gain computer runs (Input, both thresholds with caps and knee, Output, device Output; an inactive band is its level plus device Output), plus a `1:1` line ([Displays draw from the DSP](displays-draw-from-the-dsp.md)). Input spans Live's -80 to 0 dB, a point per dB, and a level is drawn as (dB + 40) / 40, clamped to [-1, 1.5].

The generated display centres traces on zero and scales them to the loudest, which is why [Amp](amp.md) draws no response. Here the 1:1 line spans -1 to 1, so the scale holds unless a band's curve rises above 0 dB out, and then shrinks by at most a third. The first trace (Low) is in the face's accent and the rest faint. The picture is cached while the settings are unchanged, because a dragged knob asks for it every frame.

## Why the trims are ±12 dB

The conformance check feeds ±0.25 noise at 25 random settings and fails any output above 64. A scratchpad Monte Carlo of 3000 random settings: ±12 dB trims gave none over 64 (worst 46.9, 99th percentile 9.9), ±18 dB 0.47% (worst 164), ±24 dB 1.97% (worst 1880). So ±12 passes by design, not by the test's seed.

## Presets

`library/Presets/MultibandDynamics/`: Gentle Glue, Upward and Downward, and De-esser (the low split off, the high band at 4:1 from -30 dB above a 5 kHz crossover, peak detection). Like the other factory presets, each lists all 39 controls in declaration order, written by a scratch script that is not kept ([Device presets (.rnd)](device-presets.md)).

## Tests

`native/src/tests/MultibandDynamicsTest.cpp` (`checkMultibandDynamics`), called by `runDeviceConformance` in `tests/DeviceConformance.cpp` after the Compressor meter check. It measures rendered audio with a Hann-windowed single-frequency transform against arithmetic ([Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)):
- the split sums to the input within 0.01 dB at five frequencies; Solo, a compressed low band leaving 6 kHz untouched;
- each direction of both ratios, inf:1, the +24 dB cap, Amount 0, RMS against peak, an inactive band, Time at 10%; and the rendered level equals `transferDb` within 0.1 dB, so the face's curve is the audio's level;
- the device: no latency or tail, which traces it names with and without Low Band, the Upward and Downward preset's curves either side of 1:1, and the face laid out and painted. `RHINO_DEVICE_SNAPSHOTS` also writes `MultibandDynamics Upward and Downward.png`.

It runs in `--device-test`, not `--self-test`: put there first, the `--self-test` case hit its 30 s limit before reaching the check, with the user's RhinoDAW open ([Build and test Rhino](build-and-test-rhino.md)). `native_device_conformance` checks 20 devices, all six standard checks on this one with nothing pending; with the check moved, all six CTest cases passed on the working tree on 2026-11-07, conformance in 9.3-10.8 s.

## Related

- [Built-in devices](built-in-devices.md)
- [Compressor](compressor.md)
- [The native device standard](native-device-standard.md)
- [Oversampler and BiquadFilter](oversampler.md)
- [Device presets (.rnd)](device-presets.md)
- [Displays draw from the DSP](displays-draw-from-the-dsp.md)
