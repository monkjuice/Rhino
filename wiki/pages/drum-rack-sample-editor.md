---
title: Drum Rack sample editor
type: component
summary: How a Drum Rack sample pad plays its file (one-shot, classic or slice), kept as undoable pad content rather than automatable controls, cut at transients or into equal parts, and spread onto pads of their own.
tags: [rhino, devices, drums, dsp, ui]
sources: []
updated: 2026-10-07
---

# Drum Rack sample editor

The selected pad's side of the [Drum Rack](drum-rack.md)'s face, added on 2026-10-07 (commit `755b4cc`) when the user asked, with a screenshot of Live, for one-shot, classic and slice. It works on a sample pad whose file was read; a synth pad ignores it and is always struck and left to ring.

## Playback

A pad's settings are `DrumRackEngine::Playback`: a `PlayMode` (`oneShot`, `classic`, `slice`), the part played (`start`, `end`, fractions of the file), fades, an envelope, `loop`, and how Slice cuts (`sliceBy`, `divisions`, `sensitivity`). All of it is played by `DrumRackEngine::Voice`:

- **One-shot** plays the part with linear fades at both edges, ignoring note-offs.
- **Classic** follows the key. An attack ramp; while held, the pad's own Decay control falls toward Sustain (`sustain + (1 - sustain) * env`); at note-off a Release falls 60 dB in its seconds. With Loop on the part repeats, crossfading its end into its start over 10 ms, or a quarter of a short part. A part that ends without looping gets a 1 ms declick.
- **Slice** plays as one-shot. It is the mode a pad is cut in.

A classic pad struck from the face holds 1 s and lets go. `previewPart(pad, start, end)` auditions one slice as a one-shot. An unshaped one-shot still plays its file sample for sample, because every stage multiplies by exactly 1 when idle; keep that true when adding a stage.

The engine keeps a pad's playback as a dozen atomics written one at a time, so a strike that lands mid-write plays a mix of old and new for one note. That is accepted, and stated in `DrumRackEngine.h`.

## Content, not controls

**Decision:** playback is pad content written through the undo manager, like the sample and the name, and is not automatable. 128 pads of a dozen settings would be thousands of parameters, on top of the 768 that already made reading the list expensive. The six per-pad controls stay automatable.

It is stored as `PAD` properties (`mode`, `start`, `end`, `fadeIn`, `fadeOut`, `attack`, `sustain`, `release`, `loop`, `sliceBy`, `divisions`, `sensitivity`), each written only where it differs from a plain one-shot of the whole file. Kits and drum presets carry the same names as optional attributes. A file is refused when it names an unknown mode, a part that ends before it starts, a loop other than 0 or 1, divisions outside 2-64, or a `sliceBy` other than `transients` or `divisions`.

**Drag protocol** (`DeviceEditorPanelDrumSampleControls.cpp`): a sample knob or a marker writes the playback without undo as it moves (`setPadPlayback(note, p, false)`). On release it puts the original back, again without undo, and writes the final value through `Session::editDeviceSettings`. So one undo takes back the whole drag, and the session announces once ([What a change costs the interface](ui-cost-of-a-change.md)).

## Slicing

`core/DrumSlicer.*` returns the starts of a part's slices as fractions of the file, the part's start always first, at most 64. It reads the whole part, so it runs on the message thread; the device keeps the last answer per note, sound and playback (`padSlices`).

- **Transients.** Energy of 10 ms windows 5 ms apart, in dB. A hit is a window this many dB louder than the one two before it, a local maximum of that rise, at least 60 ms after the last hit, and above a floor under the loudest window. Sensitivity moves the threshold from 16 to 4 dB and the floor from 30 to 60 dB down. A cut goes 2 ms before the hit's first sample to reach a third of its peak, so a slice keeps its attack.
- **Divisions** cut equal parts.

**Spreading** (TO PADS, or *Slice to pads* in the pad menu) is `Session::spreadDrumSlices(track, slot, pad, int* spread)`, one undo step named "Slice <sound> to pads". `DrumRackDevice::spreadSlices` puts slice *i* on note `pad + i`, stopping at 127. Each is the same sample as a one-shot of its slice with a 4 ms fade-out, the pad's six controls and choke group, named "<sound> <i+1>", with mute and solo cleared. Synth, empty and unreadable pads are refused. The user chose slices on pads of their own over slices played from the notes above the one pad.

## On the face

`DeviceEditorPanelDrumSample.cpp` paints this side and `DeviceEditorPanelDrumSampleControls.cpp` holds its knobs ([Device rack and device editors](device-rack.md)).

- Mode buttons beside the picture stack CLASSIC, 1-SHOT, SLICE, Live's order.
- The picture is the whole file with the part between its start and end markers lit, the envelope drawn over it, numbered slice cuts in Slice, a LOOP toggle in Classic, and a playhead (`DrumRackEngine::playhead`).
- Under it: the six pad knobs, a divider, then the sample's cells. 1-Shot: start, end, fade in, fade out. Classic: start, end, attack, sustain, release. Slice: start, end, a Hits/Parts chooser, sensitivity or slice count, and TO PADS.
- The envelope line is `shapeAt`, a closed form of the voice's fades, Decay and envelope written in the face. That departs from [Displays draw from the DSP](displays-draw-from-the-dsp.md), and no test compares it with rendered audio (as of 2026-10-07).
- It drew nothing at first, invisibly, until the face was snapshotted ([A juce::Path holding only a start point is empty](juce-path-isempty-ignores-a-lone-point.md)).

## Tests

`DrumRackTest.cpp`'s `checkPlayback` and `checkSlicing`, playback round trips and refusals in `checkFiles`, and the marker drag, knob drag, modes and spread in `runDrumRackFaceTest` ([Drum Rack](drum-rack.md)).

## Related

- [Drum Rack](drum-rack.md)
- [Device rack and device editors](device-rack.md)
- [Displays draw from the DSP](displays-draw-from-the-dsp.md)
- [The native device standard](native-device-standard.md)
- [What a change costs the interface](ui-cost-of-a-change.md)
- [Real-time audio rules](real-time-audio-rules.md)
