---
title: Rhino FM
type: component
summary: A four-operator FM synth in eight routings, written on the device SDK, whose modulation index is checked against Bessel functions and whose face shows its operators as tabs, its carriers in a routing diagram, and the waveform it makes.
tags: [rhino, devices, instruments, dsp, fm]
sources: []
updated: 2026-10-05
---

# Rhino FM

Rhino's built-in synth since 2026-10-05, replacing Rhino Wave ([Built-in devices](built-in-devices.md)). It was chosen as FM because it complements the other two: 4OSC is subtractive and Forge is the deep wavetable synth. It is for bells, electric pianos and hard basses. It is the first instrument written on the device SDK, and nothing in `Session` or the UI names it ([The native device standard](native-device-standard.md)).

The DSP is `native/src/core/FmEngine.*`, standard library only, so a test can play it without an engine. The device, `native/src/devices/instruments/RhinoFmDevice.*`, turns its controls into `FmEngine::Settings` and its MIDI into calls, and describes its face's display. It is about 160 lines.

## Sound

- **Four sine operators a voice, sixteen voices.** An operator plays the note's frequency times its ratio (0.5 to 16, in halves) and its detune (±50 cents). It has a level and its own envelope: a linear attack, then a decay and a release each given as the time to fall 60 dB.
- **Eight routings**, the classic four-operator set, named on the Algorithm chooser as `4>3>2>1` through `4 | 3 | 2 | 1`. Read `>` as "modulates", `+` as modulators summed into one input, and `|` as outputs heard side by side. A modulator always has a higher number than the operator it feeds, so a voice computes 4 down to 1 in one pass.
- **The scale.** A modulator at full level and Depth 100% swings its carrier's phase by `modulationCycles` (1.5 cycles), an index of about 9.4. Level is squared first, so the low end of the knob has the fine control. Depth (0-200%) scales every modulator, which makes it the patch's brightness control.
- **Feedback** is operator 4 into itself, averaged over the last two samples as a DX7 does. At full feedback the operator is close to a saw, with the second harmonic at 0.53 of the fundamental, and it stays periodic.
- **Velocity** scales every operator's level by `1 - s + s × velocity`. On a modulator that is brightness, and on a carrier loudness.
- **Mono** plays one voice. An overlapping note glides there without a new attack (Glide is the time constant), and letting it go glides back to the key still down. Poly takes back the voice a key already has, then a free voice, then the oldest released voice, then the oldest. The sustain pedal (CC 64) holds released notes, and the wheel bends ±2 semitones.
- **Level.** A voice's carriers are averaged and scaled by `headroom` (0.25), so one note at full level is -12 dBFS. The bottom of the Output knob is silence.
- **Defaults** are an electric piano: two stacks (`4>3 | 2>1`), operator 3 detuned 6 cents against operator 1, and a 14:1 tine on operator 4 that dies away in 0.18 s.

## Carriers and modulators

No operator is the carrier by nature. The algorithm decides it: a **carrier** is an operator that is heard, and a **modulator** is one that only bends the phase of the operators it feeds. In the default `4>3 | 2>1`, operators 1 and 3 are carriers and 2 and 4 modulate them. In `4 | 3 | 2 | 1` all four are carriers and the synth is additive. The Voice section holds settings for the whole voice; it is not a carrier. The face says which is which (below), because a face that left it out prompted exactly that question.

## The face

The face is generated ([Device rack and device editors](device-rack.md)), from two things the device declares on the SDK:
- **The four operators are tabs of one place.** Each operator's controls are declared with `.section("Op n", "Operators")`, so one operator shows at a time. That took the face from 35 controls across about 1200 px to 14 controls in 854 px. A tab with a dot is a carrier.
- **A display, from `describe`:**
  - **The routing as a diagram.** Carriers are solid, sit on the bottom row and are wired to the output bus. Modulators are drawn in outline, one row above what they feed, and operator 4's feedback is a loop. The bar along each block is its level. The outlined block is the operator whose tab is open. Clicking a block opens its tab, and the title row names its role, such as `OP 2 MODULATES 1`.
  - **Two cycles of the note the controls make.** `FmEngine::picture` plays an A4 through an engine of its own at a rate where a cycle is 128 samples. It gives two pictures. **Peak** has every envelope fully open. **Sustain** has each envelope settled at its sustain level, so the default piano's Sustain is a flat line: it dies away. Both are scaled together, so the quieter one looks quieter. Output gain is left out.

A picture costs about 68 µs. The device keeps the last one and answers from it while the settings are unchanged. A knob dragged on this face refreshes the panel in about 0.5 ms and repaints it in about 0.65 ms, as medians on a software image. A repaint confined to one knob costs about 0.11 ms, because the display and the tabs are skipped when the repaint does not reach them (measured 2026-10-05).

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
- **The face's picture, measured as sound:**
  - a lone carrier draws two cycles of a sine, holding over 99.9% of its energy in the fundamental, at a played note's RMS;
  - half sustain draws the held cycle half as tall;
  - with a modulator at ratio 4, the fundamental keeps J0(β)² of the peak picture's energy;
  - the held picture is a sine again once that modulator has decayed.
- **What the face is told:** the carriers and links of `4>3 | 2>1`, and operator 4 in `4>(3|2|1)` with feedback "modulates 1, 2 and 3, feeds back".

`--device-test` holds the device to the standard, and it passes every check with nothing pending. Sixteen voices of the default patch cost about 3% of real time at 48 kHz (measured 2026-10-05).

## Related

- [The native device standard](native-device-standard.md)
- [Built-in devices](built-in-devices.md)
- [Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)
- [Real-time audio rules](real-time-audio-rules.md)
- [Rhino Forge](forge.md)
