---
title: Clip envelopes
type: component
summary: Each audio clip carries its own Gain, Pan and Pitch envelopes, Live's clip envelopes, stored on the clip in seconds of its content, modulating the clip's knobs rather than replacing them, played by two hidden devices at the head of the clip's own plugin list that exist only while the clip has an envelope for them, with Pitch drawn on a square-root scale and snapped to semitones only with Ctrl.
tags: [rhino, audio-clips, automation, devices, tracktion]
sources: []
updated: 2026-10-29
---

# Clip envelopes

Added on 2026-10-26 in commit `a1c9607`. The user asked for automation of an audio clip's controls, then pointed at Live's envelope per clip rather than track automation (Live 12 manual, ch. 26, pp. 504-512). They are the clip-local counterpart of [Track automation](automation.md). Audio clips only.

## Files

- `native/src/core/dsp/ClipEnvelope.h`: the rules, a pure header that the model, the device and the editor all read ([Displays draw from the DSP](displays-draw-from-the-dsp.md)). It holds the ranges, the stored ids, the clamp, the pitch snap (`snapClipEnvelopeValue`, which snaps only when asked), `clipEnvelopeValueAt` (the engine's line; two points at one moment are a step) and `clipEnvelopeValueBefore` (the same line from the left), the balance gains, and the display scale (`clipEnvelopeDisplayPosition` and its inverse `clipEnvelopeValueAtDisplayPosition`; see [Drawing Pitch](#drawing-pitch)). Unit-tested by `tests/ClipEnvelopeTest.cpp` (`checkClipEnvelopeRules`, run by the geometry runner).
- `native/src/model/SessionClipEnvelopes.cpp`: `Session::clipEnvelope`, `clipEnvelopesInUse`, `setClipEnvelope`, `clearClipEnvelope` and `mirrorClipEnvelopesToEngine`, plus the helpers in `SessionInternal.h` that the other clip paths call.
- `native/src/devices/infra/ClipEnvelopeDevice.*` and `ClipPitchDevice.*`: the devices that play the envelopes.
- `native/src/ui/audioclip/AudioClipEnvelope.cpp`: drawing and editing ([Audio clip editor](audio-clip-editor.md#envelopes)).

## What each one does

An envelope modulates its knob, as Live's do; it does not replace it.

| Envelope | Range | Effect |
|---|---|---|
| Gain | 0-100% | A share of the clip's Gain knob, so it can only take the clip down. |
| Pan | -1 to 1 | A balance applied after the clip's own pan. Centre leaves both sides as they are. A mono clip has nothing to balance, just as its Pan knob does nothing. |
| Pitch | ±24 st | Semitones added to the Pitch knob. A node can sit between semitones; holding Ctrl (Cmd on a Mac) lands it on a whole one. |

A single point holds its value across the whole clip. With no points, the knob plays alone. Fade In and Fade Out have no envelope, because they are lengths, not values over time.

### Drawing Pitch

Commit `5781ef0`, at the user's request. On a straight scale across ±24 st, +2 st sat only about 4% of the band above the middle, although +2 st is already a big move. Pitch is therefore drawn on a square-root scale either side of no shift: position = 0.5 ± 0.5·√(|st|/24), where 0 is the foot of the band and 1 the top. +1 st is drawn 0.204 of the way from the middle to the top, +2 st 0.289, +12 st 0.707. Gain and Pan stay linear. The engine still interpolates in semitones, so a straight stretch between two nodes is drawn as a curve ([Audio clip editor](audio-clip-editor.md#envelopes)). The same commit dropped Live's rule of snapping to whole semitones unless Shift is held, because the user rejected it.

## Storage

Each envelope that has points is a `rhinoClipEnvelope` child of the clip's state. Its `kind` is `gain`, `pan` or `pitch` (saved, so never renamed), and it holds `point` children with `time` and `value`, as a track lane does. An envelope cleared of its points removes its child.

**Times are seconds of the clip's content**: edit time - clip start + clip offset. A move changes nothing, a left trim or a split leaves every point on the sound it was drawn against, and the region copy (`ClipSnapshot::envelopes`) pastes the points as they are. Whatever stretches the content retimes the points through `retimeClipEnvelopes`:

- warp on or off, the clip tempo, Detect, and repitch on a tempo change, by the content ratio (`rescaleWarpedClip`);
- `setTempo`, by old over new BPM, but only on warped (auto-tempo) clips, since an unwarped clip plays at its own speed ([Transport](transport.md));
- a track pasted from a document at another tempo (`keepOnItsBeats`, [Track clipboard](track-clipboard.md));
- reverse, shifted by the change in offset, because the engine mirrors a reversed clip's window into its file ([Writing a clip's reverse switch moves its window](writing-reverse-moves-a-clips-window.md)).

## How it plays

The engine plays the envelopes, live and in renders, through hidden devices at the **head of the clip's own plugin list** (`AudioClipBase::getPluginList`). Tracktion runs that list after the wave node and before the clip's fade node (`createNodeForAudioClip`).

- **Clip Envelope** (`rhino.clip.envelope.v1`) has the parameters `gain`, `pan` and `fade`. It ramps across each engine sub-block, so a step does not click.
- **Clip Pitch** (`rhino.clip.pitch.v1`) has the parameter `semitones`. It shifts through Signalsmith, the stretcher an unwarped clip's Pitch knob uses.

Both are catalog entries flagged `infrastructure` and both pass `--device-test`. A clip carries a device only while it has an envelope for it (`syncClipEnvelopeDevices`). A Pitch envelope also brings Clip Envelope, to play the fades. The devices are added and removed inside the edit's transaction, so an undo restores the envelope and its device together. `rhinoClipEnvelopeDevice` on a device's state (`level` or `pitch`) separates them from any other effect in the clip's list (one dropped on a clip before commit `dd27e92`; a drop now goes on the track, [Device chain order](device-chain-order.md#clip-effects)). `Session::clipPluginCount` leaves them out, so neither the arrangement's FX badge nor the merge's list of lost clip effects counts them. Both answer `canBeAddedToMaster()` with true: after `AudioClipBase::setParent` a clip's plugin list has no owner track, and `insertPlugin` checks such a list as it checks the master's.

The devices' parameters follow engine curves that `mirrorClipEnvelopes` writes in **edit time** from the content-time points, outside the undo history, the way track lanes are mirrored. `Session::mirrorAutomationToEngine` ends by calling `mirrorClipEnvelopesToEngine`, which visits only clips that have a plugin and leaves unchanged curves alone. The track mirror's sweep for stray curves covers only track and main plugin lists. `setClipEnvelope` also mirrors straight away, so a drag is heard before it ends, and it restarts playback when the set of devices changed. At the clip's end the curve takes `clipEnvelopeValueBefore`, so a clip cut exactly on a step does not ramp across its whole length towards the far side of the step.

Clip Pitch works around three engine faults, measured at `4536d8a`:

- [Tracktion's pitch shifter under-reports its latency](tracktion-pitch-shifter-under-reports-latency.md): Clip Pitch reports its full delay, a fixed `ClipPitchDevice::latencySeconds` (0.32 s) at any rate.
- [Tracktion reads a delayed plugin's automation early](tracktion-reads-delayed-plugin-automation-early.md): the mirror writes the Pitch curve 0.32 s early.
- [Tracktion fades a clip after its plugins, ignoring their delay](tracktion-clip-fades-ignore-clip-plugin-delay.md): while a clip has a Pitch envelope, Rhino holds the clip's fades and Clip Envelope plays them ahead of the shifter.

## What it costs

- One clip with a Pitch envelope adds 0.32 s of delay compensation to the whole song.
- On a track with any latent clip plugin, every clip is processed on every block: `createNodeForClips` sums the clips (`SummingNode`) instead of combining them (`CombiningNode`).
- A clip with both a Pitch knob value and a Pitch envelope runs two stretchers in series: the knob's in the wave reader, the envelope's in Clip Pitch.
- A possible follow-up, not done: prime the stretcher at reset to shrink the 150 ms queue pad.

## Merge

`SessionMerge.cpp` renders with the plugins off, so the envelope devices are not heard in the merged file. Instead the merged clip takes the sources' envelopes as stretches in edit time: neutral under a source that had none, and a step where two sources meet. A source's held fades are lent to the engine for the render (`lendHeldFadesToEngine`, outside the undo history) and then taken back.

## Tests

`ClipEnvelopes.inc` sits in the arrangement runner right after `ClipPitch.inc`, in a `Session` of its own. It renders, so it comes before `GesturesAndPersistence.inc`. It renders at block size 480 ([Reproducing a live timing bug offline](reproducing-live-timing-offline.md)) and measures these cases:

- a Gain step halves the level, applied to whatever the knob is set to;
- undo and redo take the envelope and its device together;
- the envelope follows a move and a left trim;
- Pan hard right leaves only the right side;
- split, duplicate and merge carry the envelope;
- reverse, checked on the model only, because a reversed clip's proxy never arrives in a test render ([Offline renders that never return](renders-that-never-return.md));
- Pitch hits land within 3 ms of the plain clip's, with clean edges, at 150 Hz before the step and an octave up after it;
- a fade-out on a pitched clip takes down only the last hit;
- a saved and reopened document keeps all of it, and clearing Pitch hands the fades back to the engine.

The scenario then drives the editor with synthesised mouse events. Since `5781ef0` it also asserts that +2 st is drawn more than a quarter of the way from the middle to the top, that a pressed Pitch node lands between semitones and that Ctrl lands it on a whole one. It paints a ramp on every run as a smoke test of the curved line. `RHINO_CLIP_ENVELOPE_SNAPSHOT` writes the panel to a PNG ([Seeing the UI without taking the screen](headless-ui-snapshots.md)).

## Related

- [Audio clip editor](audio-clip-editor.md)
- [Track automation](automation.md)
- [Time warp](time-warp.md)
- [Built-in devices](built-in-devices.md)
- [Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)
