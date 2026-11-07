---
title: juce::String with no decimal places prints all of them
type: gotcha
summary: juce::String(value, 0) does not round to a whole number - with zero (or fewer) decimal places JUCE formats the number in full, so Multiband Dynamics printed "226.413 ms" and the SDK's readings "523.25 Hz" until 2026-11-07; round with juce::roundToInt instead, as the SDK's placed() now does; the audio clip editor's milliseconds and the Vocoder's attack still make the call.
tags: [rhino, juce, ui, devices]
sources: []
updated: 2026-11-07
---

# juce::String with no decimal places prints all of them

`juce::String(float or double, numberOfDecimalPlaces)` pads to that many places when the count is positive. Given 0 it does not round to an integer: it formats the number in full. A formatter written as `juce::String(ms, ms < 10 ? 2 : ms < 100 ? 1 : 0)` therefore prints "5.00 ms" and "50.0 ms" but "226.413 ms".

Multiband Dynamics' Attack and Release did exactly this in `31a2e09`. Since 2026-11-07 its `timeText` (`devices/audio/MultibandDynamicsDevice.cpp`) rounds 100-999 ms with `juce::String(juce::roundToInt(ms))`, and `MultibandDynamicsTest.cpp` requires 5.00 ms, 50.0 ms, 226 ms (from 226.413) and 1.50 s ([Multiband Dynamics](multiband-dynamics.md)).

A whole value hides the bug, so a test has to set a fractional one.

## The SDK's readings, fixed

A search on 2026-11-07 found the same call in the SDK's default reading, `devices/sdk/NativeParameter.cpp`: `ParamUnit::hertz` from 100 to 999 Hz, a unitless control whose range spans more than 20 with a fractional step (`placesFor` answers 0), and `signedText`. So Multiband Dynamics' crossover readouts, and any SDK control in those ranges, could show every digit. The same day (commit `e00a76a`) all three went through a helper, `placed(value, places)`, which rounds with `juce::roundToInt` when `places` is 0. The probe in `tests/DeviceConformance.cpp` requires a cutoff's `text(523.25)` to read "523 Hz" and a 0-100 unitless control's `text(42.7)` to read "43".

## Still making the call

Found by the same search, not yet checked on screen or fixed:
- `ui/audioclip/AudioClipPanel.cpp`, `secondsText` under one second.
- `devices/audio/VocoderDevice.cpp`, the attack reading from 10 ms.

Messages and test failures that print a figure with 0 places (`SessionWarp.cpp`, `SessionDjSources.cpp`, several tests) have the same effect on fractional values.

## Related

- [Multiband Dynamics](multiband-dynamics.md)
- [A JUCE parameter cannot print a value outside its range](juce-parameter-text-clamps-to-range.md)
- [The native device standard](native-device-standard.md)
