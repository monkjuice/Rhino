---
title: Audio clip editor
type: component
summary: Edits one audio clip's own gain, pan, pitch, fades, mute and reverse as clip properties, with one undo step per knob drag.
tags: [rhino, ui, audio-clips]
sources: []
updated: 2026-10-05
---

# Audio clip editor

The editor for one audio clip: `native/src/AudioClipPanel.*` in the UI (its warp half is a second translation unit, `AudioClipWarp.cpp`, see [Time warp](time-warp.md)) and `native/src/SessionAudioClips.cpp` in the model, which is the whole of that API.

## The clip's own mix

Gain (-60 to +24 dB), pan, pitch (±24 semitones), fade in, fade out, mute and reverse are properties of the **clip**, not its track: they live on the clip's `ValueTree` through Tracktion's `AudioClipBase`, so they travel with the clip, undo with it and leave its lane-mates alone. A copy or a duplicate carries them, and the clip's warp, since commit `b5d17e0` ([Region editing](region-editing.md)). The track fader on the card is still how a whole lane moves ([Mixer](mixer.md)). The engine keeps the fades from overlapping by shortening the other one, so the panel reads both back after setting either.

**One drag, one undo step.** A knob drag is bracketed by `Session::beginAudioClipGesture`/`endAudioClipGesture` (a depth counter, so nested brackets are safe). The bracket gives one undo entry and one change notification per drag; without it a fader pull left hundreds of undo entries and rebuilt the arrangement's clip cache on every mouse move. The clip-tempo field and warp-marker drags use the same bracket.

## Where it appears

Double-clicking an audio clip opens it in the shell's lower pane, which shows one face at a time: note editor, audio clip editor or Device View. Once open, it follows the clip selection as Live's clip view does: another audio clip replaces this one, a MIDI clip hands over to the note editor, anything else closes it ([App shell and control bar](app-shell.md)). The three faces became one pane in commit `a33503f` (2026-09-30).

## Commands that reach past it

- **Split** cuts at the timeline's insert line (`splitPosition` asks `Arrangement::insertPointTime`), so the button and Ctrl+E cut in the same place even while the transport moves.
- **R** (reverse) and **Ctrl+J** (merge) are shell shortcuts, because the keyboard is usually on one of this panel's knobs.
- **Merge** (`SessionMerge.cpp`) is the one clip command that writes a file: it renders the selected clips per track with the track's plugins off, so each clip's own mix lands in the file but clip-local effects are dropped (and reported). The file outlives an undo.

## Drawing and testing

The waveform is `paintWaveformLanes` in `WaveformLanes.h`, shared with the arrangement: one lane per channel with a hairline gap and a zero line, so a stereo clip never reads as one taller mono waveform. `ClipWarp.inc` renders the panel in a test and writes a PNG when `RHINO_CLIP_PANEL_SNAPSHOT` is set ([Seeing the UI without taking the screen](headless-ui-snapshots.md)).

## Related

- [Time warp](time-warp.md)
- [Arrangement view](arrangement-view.md)
- [App shell and control bar](app-shell.md)
- [Clips never overlap](clip-placement.md)
- [Track and clip colours](track-and-clip-colours.md)
