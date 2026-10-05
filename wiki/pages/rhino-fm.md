---
title: Rhino FM
type: component
summary: A four-operator FM synth in eight routings, written on the device SDK, whose modulation index is checked against Bessel functions.
tags: [rhino, devices, instruments, dsp, fm]
sources: []
updated: 2026-10-05
---

# Rhino FM

Rhino's built-in synth since 2026-10-05, replacing Rhino Wave ([Built-in devices](built-in-devices.md)). It was chosen as FM because it complements the other two: 4OSC is subtractive and Forge is the deep wavetable synth. It is for bells, electric pianos and hard basses. It is the first instrument written on the device SDK, and nothing in `Session` or the UI names it ([The native device standard](native-device-standard.md)).

The DSP is `native/src/core/FmEngine.*`, standard library only, so a test can play it without an engine. The device, `native/src/devices/instruments/RhinoFmDevice.*`, turns its controls into `FmEngine::Settings` and its MIDI into calls. It is about a hundred lines.

## Sound

- **Four sine operators a voice, sixteen voices.** An operator plays the note's frequency times its ratio (0.5 to 16, in halves) and its detune (±50 cents). It has a level and its own envelope: a linear attack, then a decay and a release each given as the time to fall 60 dB.
- **Eight routings**, the classic four-operator set, named on the Algorithm chooser as `4>3>2>1` through `4 | 3 | 2 | 1`. Read `>` as "modulates", `+` as modulators summed into one input, and `|` as outputs heard side by side. A modulator always has a higher number than the operator it feeds, so a voice computes 4 down to 1 in one pass.
- **The scale.** A modulator at full level and Depth 100% swings its carrier's phase by `modulationCycles` (1.5 cycles), an index of about 9.4. Level is squared first, so the low end of the knob has the fine control. Depth (0-200%) scales every modulator, which makes it the patch's brightness control.
- **Feedback** is operator 4 into itself, averaged over the last two samples as a DX7 does. At full feedback the operator is close to a saw, with the second harmonic at 0.53 of the fundamental, and it stays periodic.
- **Velocity** scales every operator's level by `1 - s + s × velocity`. On a modulator that is brightness, and on a carrier loudness.
- **Mono** plays one voice. An overlapping note glides there without a new attack (Glide is the time constant), and letting it go glides back to the key still down. Poly takes back the voice a key already has, then a free voice, then the oldest released voice, then the oldest. The sustain pedal (CC 64) holds released notes, and the wheel bends ±2 semitones.
- **Level.** A voice's carriers are averaged and scaled by `headroom` (0.25), so one note at full level is -12 dBFS. The bottom of the Output knob is silence.
- **Defaults** are an electric piano: two stacks (`4>3 | 2>1`), operator 3 detuned 6 cents against operator 1, and a 14:1 tine on operator 4 that dies away in 0.18 s.

## Staying clean at the top

Two rules stop the highest notes from aliasing:
- An operator whose frequency reaches 45% of the sample rate is silent, not folded back down.
- Above `sampleRate / 24` (2 kHz at 48 kHz), each modulator's depth falls in proportion to the note's frequency. That is the job a DX7's keyboard level scaling was usually given by hand.

## Tests

`native/src/tests/FmTest.cpp`, run by `--self-test`, measures rather than reads:
- **Pitch** by zero crossings, to 0.01%.
- **A lone operator is a pure sine**: harmonics below -90 dB.
- **The index against Bessel functions.** With the modulator at four times the carrier, |H5|/|H1| must be J1(β)/J0(β). The check runs at three indices, and again at C8 for the depth taper. Scaling the index 10% wrong fails it.
- **Brightness** as the spectral centroid, rising with modulator level.
- **Feedback**: saw-like and periodic.
- **Envelopes, velocity, polyphony, the pedal, mono glide and the Nyquist guard.**

`--device-test` holds the device to the standard, and it passes every check with nothing pending. Sixteen voices of the default patch cost about 3% of real time at 48 kHz (measured 2026-10-05).

## Related

- [The native device standard](native-device-standard.md)
- [Built-in devices](built-in-devices.md)
- [Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)
- [Real-time audio rules](real-time-audio-rules.md)
- [Rhino Forge](forge.md)
