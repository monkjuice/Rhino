---
title: Slice to New MIDI Track
type: component
summary: Live 12's Slice to New MIDI Track on the audio-clip menu - the clip rendered as it plays, cut at transients or every bar to 1/16, each slice a WAV of its own on a Drum Rack pad from C1 up, on a new MIDI track under the clip whose one clip strikes them in order, the source muted, all one undo step.
tags: [rhino, arrangement, drums, rendering, undo]
sources: []
updated: 2026-10-27
---

# Slice to New MIDI Track

Live 12's command (Live 12 manual §13.1, pp. 335-337; where the manual is: [Development environment and reference material](development-environment.md)), added on 2026-10-27 in commit `3627686`. The user asked for "audio -> splice to midi" on an audio clip's right-click menu, read as this command.

## Where it lives

- **Model**: `Session::sliceClipToNewMidiTrack(id, everyBeats = sliceAtTransients, SliceResult*)` in `native/src/model/SessionSliceToMidi.cpp`, declared in `Session.h` after `bounceOutcome` with `sliceAtTransients` (0.0), `firstSliceNote` (36) and `SliceResult {track, clip, slices}`.
- **UI**: `showClipMenu` (`ui/arrangement/Arrangement.cpp`) has a *Slice to New MIDI Track* submenu, enabled only on an audio clip: *At Transients* (id 40) and *Every 1 Bar* to *Every 1/16* (ids 41-45, beats from `gridDivisionBeats`). It acts on the clip the menu was opened on, not the selection, as Split and Warp do, because Live slices one clip. `Arrangement::sliceToNewMidiTrack` (`ArrangementBounce.cpp`) calls the model, selects the new MIDI clip and writes the status line.

## What it does

1. **Renders the clip as it plays**, alone, to its end, blocking on the message thread, as Ctrl+J merge renders ([Audio clip editor](audio-clip-editor.md#commands-that-reach-past-it)): track and master plugins off, stereo, solo isolator and clip-slot disabler, held fades lent to the engine ([Tracktion fades a clip after its plugins](tracktion-clip-fades-ignore-clip-plugin-delay.md)), 32-bit float at the device's rate and block size, given up after 15 s without progress (`runRenderTask`). The clip's gain, pan, pitch, warp, fades and reverse are in the slices; the track's devices and fader are not, nor are clip effects or clip envelopes, which the engine plays on the clip's plugin list gated with the track's. **Rejected:** slicing the source file, right only for a clip that plays its file untouched; otherwise the notes sit against audio the clip never plays.
2. **Cuts** at transients by the Drum Rack's own `DrumSlicer::slices` at default `Playback` (sensitivity 0.5, at most 64; [Drum Rack sample editor](drum-rack-sample-editor.md#slicing)), or every N song beats from the clip's start. The first slice starts with the clip; a cut leaving under `minimumSplitSeconds` (10 ms) before the end is dropped.
3. **Writes each slice to a WAV of its own**, `<clip> Slice N.wav` (`nextRenderFile`) in `mergedAudioDirectory()`: `<project> Audio`, or `%APPDATA%\Rhino\Merged audio` while unsaved. A pad reads the whole of its file up to `DrumRackEngine::longestSampleSeconds` (10 s), so pads sharing one render would each hold all of it and a clip over 10 s would lose its end. A slice over 10 s is cut into equal parts that fit.
4. **Fills a Drum Rack from C1** (36), Live's first slice note, not C2 where Rhino's kits sit ([Drum Rack](drum-rack.md)). That leaves 92 pads; more slices is refused before any file is written. Each pad is a plain one-shot of its whole file named `<clip> N`, with **no fades**: struck in order at velocity 127 the pads give back the render sample for sample, where *Slice to pads*' 4 ms fade-out would dip the sound at every cut. Fading resequenced slices is left to the sample editor. The bank opens at C1 with C1 selected.
5. **Edits in one undo step**, "Slice <clip> to new MIDI track": the source clip muted; a MIDI track appended directly under the clip's track, in its group if it has one; a Drum Rack by `switchTrackInstrument` and `setKit`; the track named `<clip> Slices` (after the switch, which renames the track for its instrument) in the clip's colour (`clipColour`); a MIDI clip `<clip> Slices` over the clip's exact span, one note per slice, pitch 36 + i, from the slice's song beat to the next slice's. A failure partway calls `undoCurrentTransactionOnly` and deletes the files. The files outlive an undo, as a merge's and a bounce's do.

Muting the source is Rhino's choice, the [Bounce to New Track](bounce-to-new-track.md) convention, so the song does not play the material twice; Live's manual does not say it mutes.

## Refusals

Not an audio clip; a clip in a DJ cell rather than on the timeline; a muted clip or track (it would slice silence); a missing file (`checkClipFile`, [Offline renders that never return](renders-that-never-return.md)); no Drum Rack in the build; more than 92 slices; a bounce or export running.

## Test

`tests/Arrangement/scenarios/ClipSliceToMidi.inc`, after `ClipBounce.inc` and, since it renders, before `GesturesAndPersistence.inc` ([Writing Rhino tests](writing-rhino-tests.md)). A 12 s fixture at 120 BPM, four 220 Hz hits 0.5 s apart then a quiet 330 Hz tone from 1.9 s. It checks the refusals (a MIDI clip; every 1/16, 96 slices, with nothing made or written; a muted clip), every 1/4 (24 slices, a note on each beat from 36) and its undo, and At Transients: six slices (the hits, the tone's start, the 10.1 s tone cut in two), name, colour, mute, pads 36-41, files beside the project none over 10 s, lengths summing to the clip. It also listens: the slices' track bounced against the clip bounced alone, relative RMS error under 0.1 over the hits and across the tone's cut.

## Not done (2026-10-27)

- Live's *Warp Marker* division, *Preserve warped timing*, slicing presets and macro mapping.
- A stepped render behind the loading screen; the window freezes for the render, as for a merge ([A long command runs in slices behind a modal screen](long-commands-run-in-slices.md)).
- Clip effects and clip envelopes in the slices.

## Related

- [Drum Rack](drum-rack.md), [Drum Rack sample editor](drum-rack-sample-editor.md)
- [Bounce to New Track](bounce-to-new-track.md)
- [Audio clip editor](audio-clip-editor.md), where Ctrl+J merge is described
- [Arrangement view](arrangement-view.md)
- [Session, the model](session-model.md)
