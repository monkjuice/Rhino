---
title: Audio clip editor
type: component
summary: Edits one audio clip's own gain, pan, pitch, fades, mute and reverse as clip properties, with one undo step per knob drag, and draws and edits the clip's Gain, Pan and Pitch envelopes over its waveform, Pitch on a square-root scale with Ctrl for whole semitones.
tags: [rhino, ui, audio-clips, automation]
sources: []
updated: 2026-10-26
---

# Audio clip editor

The editor for one audio clip: `native/src/ui/audioclip/AudioClipPanel.*` in the UI (its warp half is a second translation unit, `AudioClipWarp.cpp`, see [Time warp](time-warp.md), and its envelopes a third, `AudioClipEnvelope.cpp`) and `native/src/model/SessionAudioClips.cpp` in the model, which is the whole of that API.

## The clip's own mix

Gain (-60 to +24 dB), pan, pitch (±24 semitones), fade in, fade out, mute and reverse are properties of the **clip**, not its track: they live on the clip's `ValueTree` through Tracktion's `AudioClipBase`, so they travel with the clip, undo with it and leave its lane-mates alone. A copy or a duplicate carries them, and the clip's warp, since commit `b5d17e0` ([Region editing](region-editing.md)). The track fader on the card is still how a whole lane moves ([Mixer](mixer.md)). The engine keeps the fades from overlapping by shortening the other one, so the panel reads both back after setting either. While a clip has a Pitch envelope, its fades are held by Rhino (`rhinoFadeIn`, `rhinoFadeOut`) and the engine's own are zero. Read and write fades only through `clipFadeInSeconds`, `clipFadeOutSeconds` and `setClipFades` ([Tracktion fades a clip after its plugins, ignoring their delay](tracktion-clip-fades-ignore-clip-plugin-delay.md)). Writing reverse mirrors the clip's window into its file even when the value does not change, so `setAudioClipReversed` writes it only when it differs ([Writing a clip's reverse switch moves its window](writing-reverse-moves-a-clips-window.md)).

**Pitch streams through a live stretcher and moves no hit.** Since commit `5c9c1e1`, `setAudioClipPitch` ends in `Session::applyStretchPlayback`, which turns the engine's proxy off for a pitched clip and sends an unwarped clip's pitch through Signalsmith. Before that, any pitch made Tracktion play a rendered copy. That copy was made with no allowance for the stretcher's latency, so every hit came about 120 ms late, which sounded like a change of BPM. Its last 120 ms were cut off hard, which clicked at the clip's end and at every loop wrap ([Tracktion's time-stretched proxy plays late and cuts its end](tracktion-stretched-proxy-plays-late.md)). `ClipPitch.inc` measures both.

**One drag, one undo step.** A knob drag is bracketed by `Session::beginAudioClipGesture`/`endAudioClipGesture` (a depth counter, so nested brackets are safe). The bracket gives one undo entry and one change notification per drag; without it a fader pull left hundreds of undo entries and rebuilt the arrangement's clip cache on every mouse move. The clip-tempo field and warp-marker drags use the same bracket.

## Envelopes

Since commit `a1c9607`, the panel draws the clip's Gain, Pan and Pitch envelopes over its waveform, one at a time, and edits them there ([Clip envelopes](clip-envelopes.md)).

- **Choosing one.** The header has an *Envelope: None/Gain/Pan/Pitch* button between the clip's name and its file's name; on a narrow panel the file name gives way first. Its menu lists None, Gain, Pan and Pitch, with a bullet on each envelope that has points, and *Clear <shown>*. Right-clicking the Gain, Pan or Pitch knob offers *Show* and *Clear <name> envelope*. That right-click is heard by `KnobMenu`, a separate `juce::MouseListener` attached to those three sliders. It must not be the panel itself, which would hear its own presses twice ([A JUCE component listening to itself hears its own clicks twice](juce-self-listener-hears-clicks-twice.md)). A knob whose envelope has points shows its name in the envelope's red, with a bullet. Every menu choice goes through `chooseEnvelopeMenuItem` (1-3 show, 4 none, 11-13 clear), which is also the seam the tests use.
- **Drawing.** The line uses track automation's red (`0xffe2564f`). A faint guide marks where the envelope does nothing: the top for Gain, the middle for Pan and Pitch. An envelope with no points is drawn dashed. Since commit `5781ef0`, `yForEnvelopeValue` and `envelopeValueAtY` go through `clipEnvelopeDisplayPosition` and its inverse, so Pitch is drawn on a square-root scale ([Clip envelopes](clip-envelopes.md#drawing-pitch)). The engine interpolates in semitones, so `paintEnvelope` draws each Pitch stretch between nodes in 3 px steps, and the drawn curve is the line the engine plays ([Displays draw from the DSP](displays-draw-from-the-dsp.md)). Pitch also gets faint hairlines at ±1, ±2, ±5 and ±12 st, with ±2, ±5 and ±12 labelled at the band's left edge wherever a label is at least 11 px from the last one.
- **Gestures** follow the track lanes' rules ([Track automation](automation.md#dragging-a-level)):
  - A press grabs the node under it (within 6 px). Otherwise it puts a node down: on the line if the press is within 5 px of it, else at the pointer. Since `5781ef0` Pitch moves freely between semitones, and holding Ctrl (Cmd on a Mac, `isCommandDown`) lands it on a whole one, both when it is put down and while it is dragged.
  - A drag moves the node. Time follows the pointer absolutely, held between the neighbouring nodes and inside the span the clip plays. Height follows the pointer's travel: a drag carries the node's position in the band (0 to 1), not its value. One band of travel takes 200 px, or the band's height where that is taller, and Shift makes it a tenth. So a Pitch node moves slowly near no shift and faster towards the octaves, as drawn. Gain and Pan are linear, so they move as before.
  - A press that moves nothing records no undo step. A double-click removes a node. A right-click anywhere on the waveform opens the envelope menu.
  - One drag is one undo step (`beginAudioClipGesture`).
- **With warp markers showing**, the envelope is drawn under the marker bar in played time: content time × `speedRatio`, placed through `xForWarpTime`. A double-click places a warp marker only on the bar ([Time warp](time-warp.md)).

## Where it appears

Double-clicking an audio clip opens it in the shell's lower pane, which shows one face at a time: note editor, audio clip editor or Device View. Once open, it follows the clip selection as Live's clip view does: another audio clip replaces this one, a MIDI clip hands over to the note editor, anything else closes it ([App shell and control bar](app-shell.md)). The three faces became one pane in commit `a33503f` (2026-09-30).

## Commands that reach past it

- **Split** cuts at the timeline's insert line (`splitPosition` asks `Arrangement::insertPointTime`), so the button and Ctrl+E cut in the same place even while the transport moves.
- **R** (reverse) and **Ctrl+J** (merge) are shell shortcuts, because the keyboard is usually on one of this panel's knobs.
- **Merge** (`SessionMerge.cpp`) writes a file, as only the bounce also does: it renders the selected clips per track with the track's plugins off, so each clip's own mix lands in the file but clip-local effects are dropped (and reported). Clip envelopes are not rendered into the file; the merged clip takes them over instead ([Clip envelopes](clip-envelopes.md#merge)). The file outlives an undo. Ctrl+B [Bounce to New Track](bounce-to-new-track.md) is its counterpart through the track's devices, onto a new track. The clip menu's [Slice to New MIDI Track](slice-to-new-midi-track.md) renders one clip the same way before cutting it onto Drum Rack pads.
- **Mute** is the same clip property the arrangement's clip menu sets through `Session::setClipsMuted`, which takes MIDI clips too; a muted clip is drawn grey on the timeline ([Arrangement view](arrangement-view.md)).

## Drawing and testing

The waveform is `paintWaveformLanes` in `WaveformLanes.h`, shared with the arrangement: one lane per channel with a hairline gap and a zero line, so a stereo clip never reads as one taller mono waveform. `ClipWarp.inc` renders the panel in a test and writes a PNG when `RHINO_CLIP_PANEL_SNAPSHOT` is set ([Seeing the UI without taking the screen](headless-ui-snapshots.md)). `ClipEnvelopes.inc` drives the envelope gestures with synthesised mouse events and writes a PNG when `RHINO_CLIP_ENVELOPE_SNAPSHOT` is set.

## Related

- [Clip envelopes](clip-envelopes.md)
- [Time warp](time-warp.md)
- [Tracktion's time-stretched proxy plays late and cuts its end](tracktion-stretched-proxy-plays-late.md)
- [Arrangement view](arrangement-view.md)
- [App shell and control bar](app-shell.md)
- [Clips never overlap](clip-placement.md)
- [Track and clip colours](track-and-clip-colours.md)
