---
title: DJ view and the booth
type: component
summary: Up to six CDJ-style decks either side of a DJM-style mixer, run by Rhino's own engine as a second audio callback, playing files or the song's tracks and groups bounced to audio and bounced again as they are edited.
tags: [rhino, dj, decks, mixer, ui, dsp]
sources: []
updated: 2026-10-09
---

# DJ view and the booth

The DJ view is the second view of the shell, opened by the Arrange/DJ switch in the control bar or by Tab (`sessionViewEnabled` in `native/src/Main.cpp`, now true). It took the place the paused clip launcher had ([Session view (paused)](session-view.md)); that view is still built and tested but nothing in the shell reaches it. The user asked for it on 2026-10-09 as an experimental feature "oriented to DJs", with a CDJ-3000 and a DJM-V10 as the references.

| Layer | Files |
| --- | --- |
| DSP (`RhinoCore`, JUCE core only) | `core/DjEngine.*` (the booth: decks into a mixer, the command queue, quantised starts, sync), `core/DjDeck.*` (one player), `core/DjMixer.*` (channel strips, isolator, colour filter, crossfader, master, beat effect), `core/DjAnalysis.*` (waveform columns, tempo and grid, bars and drops, key), `core/DjTrack.h` (a deck's material) |
| Model | `SessionDj.cpp` (decks, transport, mixer, what the document saves), `SessionDjSources.cpp` (files read on a worker, tracks and groups bounced, Live, the stale re-bounce), `SessionDjInternal.h` (the booth and its audio callback) |
| UI | `DjView.*` (the view), `DjDeckPanel.*` (a deck's console), `DjDeckDisplay.*` (its screen), `DjMixerPanel.*` (the mixer), `DjControls.h` (pads, faders, meters) |
| Tests | `tests/DjTest.cpp` (`--self-test`), `tests/Arrangement/scenarios/DjBooth.inc` |

## The engine is a second callback

The booth is `DjEngine`, pure C++ over JUCE core like the Drum Rack's engine, and `Session::DjBooth` wraps it in an `AudioIODeviceCallback` on the engine's own device manager, the arrangement the library preview and the count-in already use ([Browser and library preview](browser.md), [Recording and the count-in](recording.md)). JUCE sums its callbacks, so the booth is mixed with whatever the edit plays. That is what gives every deck a transport of its own: none of them is the song's, and the arrangement can stand still while six decks play.

- **Commands cross in a single-producer queue** (`DjEngine::Command`, 256 deep) and are applied at the start of a block; the deck's position is changed on the audio thread and nowhere else. What a deck is doing is read back from atomics (`Session::djDeckState`). Knob and fader moves are atomics the audio thread reads once a block and smooths ([Real-time audio rules](real-time-audio-rules.md)).
- **Material is handed over by pointer** (`DjEngine::setTrack`) and freed by `collect()` once as many blocks have ended as had begun when it was let go of, the way the Drum Rack counts its samples out. A deck adopts a swapped track at its next block or its next command, converting its position, cues and loop by beat when asked (`keepBeatOnSwap`), which is what a re-bounce wants.
- **Positions are frames of the track at its own rate**, so a hot cue survives a device at another rate. The player reads with four-point Hermite interpolation; the tempo fader, the jog's push and sync all change the read rate (vinyl-style: pitch moves with tempo), smoothed over 30 ms. Key lock (pitch-held stretching) is not built.
- **The audio callback's scratch buffer holds this callback's previous block** (see `CountInClick`), so `DjEngine::process` clears before it writes. A block bigger than `prepare` was told is worked in pieces.
- The tests drive `DjEngine::process` and `Session::djProcessOffline` directly; no audio device is needed.

## What a deck does

`DjDeck` is a CDJ's transport: play/pause with 2 ms fades; Cue as a CDJ reads it (playing: stop and return; stopped at the cue point: play while held; stopped elsewhere: set it here); eight hot cues (press empty to set, press set to jump and play, right-click to clear); loop in/out, beat loops of 1/8 to 32 beats, reloop/exit, halve and double; beat jump, which carries an active loop with it; seek; reverse; and the end of the track stops it. With quantise on, cue points and loop points snap to the track's nearest beat.

**Quantised starts** are the Live mechanism the user asked for, and optional. `DjEngine::Quantise` is off, beat, bar or four bars. A start or a hot cue jump on a deck other than the master is held (`DjDeck::Pending`, shown as a blinking Play key) until the master's next boundary falls inside a block, then lands at that frame, snapped to the deck's own boundary of the same size (a bar at most), so bars line up with bars. With nothing else playing, or quantise off, it lands at once.

**Master and sync.** The master is the deck chosen with its Master key while it plays, else the first playing deck; when it stops the next playing deck takes over (`chooseMaster`). A synced deck plays at the master's effective tempo and, with phase lock on (default), is nudged back onto the master's beat whenever it drifts by more than a millisecond, by at most two per cent of rate. Pressing Sync also jumps the deck onto the master's phase within the bar.

## The mixer

`DjChannelStrip` is trim (±12 dB), a three-band **isolator** (two Linkwitz-Riley fourth-order crossovers at 250 Hz and 3 kHz; each band from a full kill, `DjIsolator::killDb`, to +6 dB), the one-knob **colour filter** (a state-variable filter: low-pass closing from 20 kHz to 60 Hz left of centre, high-pass opening from 20 Hz to 10 kHz right of it, dead in the middle), a meter after the EQ and before the fader, the fader (gain is position squared), cue, the effect send and a crossfader side (A, through, B). The crossfader blends equal-power or, with its curve up, cuts sharply. The master section repeats the isolator, adds the level with a soft clip over the last decibel, and mixes a cue bus onto outputs 3 and 4 when the device has them.

`DjBeatFx` is one unit on one channel or the master, timed in beats of the master tempo (128 BPM when nothing plays): echo and delay (which ring out after they are switched off; delay bounces between the sides), flanger, phaser, a swept filter, a Schroeder reverb, roll (captures the beats after it is switched on and repeats them) and trans (a gate). The swept effects take their phase from the master's beat count, so they lock to the beat.

## What a deck plays

- **A file** is read whole and analysed on the booth's one worker thread, up to fifteen minutes, and `Session::djPoll` installs it. Any format JUCE's basic formats read.
- **A track or a group of the song** is bounced to audio at once, on the message thread, as a merge renders ([Merging audio clips](arrangement-view.md)): from the top of the song to the end of the last clip among the tracks, in whole bars, through the tracks' own devices and faders and not the main chain, solo elsewhere and slot clips kept out. The bounce lands with a loop over its whole length, so a stem plays round. It knows its tempo exactly, so the grid is laid from the song's.
- **A bounced deck goes stale** on every document change (`markModified` calls `djDocumentChanged`), and the poll bounces it again once the document has been quiet for half a second, one deck per poll, swapping the new audio in by beat. That is how the note editor edits a deck while it plays: a deck's Edit key opens its track's clip in the lower pane. Reload bounces at once; a right-click on it switches the automatic re-bounce off for that deck.
- **Live** switches a track deck's monitoring On ([Track inputs and monitoring](inputs-and-monitoring.md)), so its instrument plays from the keys over the bounce; a Drum Rack on it plays its pads. The monitoring is put back when Live goes off. A group has no instrument and a file no track, so both refuse. A refusal from arming (no audio device, an input the machine lacks) leaves the deck live all the same: the track took the setting and will be heard once there is an input.
- Rendering on a worker was considered and rejected: a render reads the edit, and an edit changed under it from the message thread is a race. A bounce is a few bars and the decks play on through it on their own callback, so the stall is not heard.

## The analysis

`analyseDjTrack` makes the picture and the numbers from a float buffer. One pass fills a waveform column per 256 frames in three bands (lows under 180 Hz, highs over 2.5 kHz) and, per column, the energy of four bands. The tempo comes from an onset envelope (log-compressed rises of the band energies, local mean removed) by autocorrelation over 60-200 BPM, scored with its octaves and a mild preference around 128, then a comb search that settles the period to five thousandths of a BPM and finds the phase. The band energies for that envelope are averaged over 46 ms: a column is shorter than one cycle of a kick, so a column's own energy rose and fell with the sine's phase and the ripple correlated with itself every 0.4 beats and buried the beat (the probe that found it is `scratchpad/tempo_probe.py`'s method, written up here because the number was 158 for a 126.5 beat). The phase is then read from the unsmoothed flux, which spikes in the column each attack lands in. The downbeat is the beat of the four the low band hits hardest. Bars are measured as RMS from the first downbeat, and a drop is a bar that is loud, half again as loud as the four before it and plainly louder than the one before, eight bars apart at least. The key is a chroma profile (a 4096-point transform every 8192 frames, 55 Hz to 2.2 kHz, lower partials weighted) against the Krumhansl-Schmuckler profiles, named as a DJ reads it with its Camelot number.

## The view

`DjView` stands the decks either side of the mixer, odd to the left and even to the right, up to three a column. `DjDeckPanel` is a CDJ laid flat: the name bar chooses the source (the song's tracks and groups, a file chooser, the library's samples by pack, the machine's drives) or takes a dropped file; the screen; the hot cues; Cue and Play; the loop and jump keys; Sync, Master and Reverse; the tempo fader with its range. `DjDeckDisplay` scrolls the waveform under a fixed playhead from tiles painted once at the current zoom (the wheel zooms, 3-48 s across), draws the grid, the loop and the cue flags on top each frame, shows the whole track in a strip below with its drops marked and the played part dimmed (a click seeks), and a line of readouts. Dragging the scrolling strip pushes the tempo while the button is down, as a hand on the platter does. The view's displays move on the display's refresh, its keys and meters at 30 Hz, and the shell's timer polls the loads. Hidden, the view marks itself stale as the other faces do ([What a change costs the interface](ui-cost-of-a-change.md)). Meaning colours are `palette::dj*` in `Theme.h`.

## What the document saves

`rhinoDj` on the edit's state, written when a snapshot is taken (`projectSnapshot`) and read back on restore: each deck's source (a file by stored path, a track by its `EditItemID`, a group by id), tempo settings, cue, hot cues and loop in seconds, Live and the automatic re-bounce; every channel strip; the crossfader, master and effect; quantise and phase lock. Positions are not saved. A reopened deck is empty and stale and the poll loads it again; its cues are applied once the material is there (`applyDjDeckSettings`). Loading a source, setting a hot cue and adding or removing a deck mark the document modified; knob and fader moves do not, and nothing here is an undo step.

## Decisions

- **Decks play audio, not the edit.** A deck with a transport of its own cannot be a Tracktion track driven by the song's one transport, so the song's material reaches a deck by being bounced. The price is the re-bounce after an edit, which the half-second quiet period and the swap by beat make tolerable.
- **One engine, one callback,** rather than a Tracktion edit per deck: a second edit per deck would have cost a graph each and still needed the mixer written.
- **Vinyl-style tempo** (pitch moves with speed) for now; a pitch-held mode needs the engine's stretcher on the booth's thread and is noted as not built.
- **Rhino's own mixer DSP** rather than the edit's plugins, so the mixer is measured offline and never rebuilds a playback graph.

## Not built

Key lock; slip mode; a sampler; recording the master; a second deck layout for very narrow windows; MIDI control of the booth; rendering a bounce without stalling the message thread while an edit is in flight.

## Related

- [Session view (paused)](session-view.md)
- [App shell and control bar](app-shell.md)
- [Real-time audio rules](real-time-audio-rules.md)
- [Drum Rack](drum-rack.md)
- [Track inputs and monitoring](inputs-and-monitoring.md)
- [Offline renders that never return](renders-that-never-return.md)
- [Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)
