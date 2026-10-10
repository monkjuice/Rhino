---
title: App shell and control bar
type: component
summary: Main.cpp's window, control bar, docked browser and single lower pane, plus the 30 Hz timer that polls what the engine never broadcasts.
tags: [rhino, ui, shell]
sources: []
updated: 2026-10-09
---

# App shell and control bar

`native/src/Main.cpp` (about 2,200 lines as of 2026-10-03) holds `Application` (startup, log, test flags, a `.rhinoedit` given as first argument), `Window` and the content, `ControlWindow`: about 1,600 lines defined inline, the next split candidate in `AGENTS.md` ([Keeping files small](keeping-files-small.md)). No test can reach it, so `RHINO_SHELL_SNAPSHOT=<png>` under `--startup-test` writes the shell to a PNG ([Seeing the UI without taking the screen](headless-ui-snapshots.md)).

## The window

`Window` draws its own title bar but sets JUCE's drop-shadow flag, because on Windows that flag is what gives a borderless window native frame styles: without it there is no Aero Snap, no snap layouts, and maximise covers the taskbar. Full screen (F12) is kiosk mode, separate from maximise, with three overrides keeping the title bar. `StartupScreen.h` marks real completed startup stages, not a simulated percentage.

## The control bar

Under the 72 px bar sit the docked browser, whose 3 px divider is its resize handle, and the arrangement, with no margins or frames. `layoutControlBar` places from a running x: browser toggle, tempo and signature, then rewind, stop, play and record; from the right edge inwards, redo, undo, then the metronome and its menu. The readout is centred in what remains, capped at 480 px, and hidden rather than squeezed when under 260 px remain.

- `ControlBarFields.*`: `ValueDragBox`, the tempo and signature fields you drag; one drag is one undo step (`Session::beginTempoGesture`). Ctrl makes the step fine, and pressing or releasing it mid-drag carries on from the value on screen, counting the new step from the last pointer position; it used to re-read the whole drag at the new step (commit `bac4059`).
- `ControlBarIcons.*`: glyphs are paths worn by the borderless `IconButton`, because Unicode transport symbols fell through to a different system face on every machine.
- `TransportDisplay.*`: three columns in one recess; when narrow it drops whole readings in a fixed order and never moves the position or clock. `WallClock.h` spells every time, here and on the timeline ruler. The position and the loop read bar, count and sixteenth from `musicalPosition` (`ArrangementGrid.h`), counting in the signature's own note as the ruler does, so 6/8 has six counts of two sixteenths (commit `4416222`).

## The second view

The Arrange/DJ switch at the right of the bar, left of the metronome, and Tab, swap the arrangement for the [DJ view](dj-view.md) in the same rectangle (`setSessionViewOpen`, gated by `sessionViewEnabled`, true since 2026-10-09). The lower pane is the same under either view, which is how a deck's Edit key opens its track's clip in the note editor. Switching views hands the Device View the track the view being shown has selected: the arrangement's card, or the deck last clicked when it plays a track of the song.

Since 2026-10-10 the song's transport is out of reach while the DJ view shows: opening the view stops the song if it is rolling, dims rewind, stop, play and record (`RecordButton` paints grey when disabled whatever the tracks say), and makes Space the focused deck's play key (`DjView::togglePlayFocused`; F9 answers with a hint). The lower pane follows the focused console as it follows a track card (`followDeck`): a press on a console puts its track's chain in the Device View, opening the pane if it was closed, and moves an open clip editor to the track's clip - the note editor for a MIDI track, the audio editor for an audio one (`Session::djDeckEditClip` gives the first clip of the track's kind). The view switch follows the focused deck without opening a closed pane. A file or a group deck has no track of the song and changes nothing. `followDeck` reads whether a clip editor was open *before* it moves the arrangement's track selection, because that move re-reads the arrangement's own clip selection through `refreshEditorPanes` and may close the pane on the way.

## The lower pane

Note editor, audio clip editor and Device View are three faces of one pane (`LowerPane`). Double-clicking a clip opens its editor, which then follows the clip selection; a card click opens the Device View only when the pane is free. It floats over the arrangement's foot (`Arrangement::setBottomInset`): pushing the lanes up refitted them under a dragged split, so every clip grew and shrank under the pointer. A layout change asked for by a click waits for the timer and for no mouse button down, because that press may be starting a drag. The three faces became one pane in commit `a33503f` (2026-09-30). `ControlWindow::resized` floors the pane at 112 px (`minimumPaneHeight`) for the clip editors and at `DeviceRack::minimumHeight` (219 px) for the Device View; one 112 px floor for all three let a drag cut devices off (commit `67031ba`). The clamp writes back into `lowerPaneHeight`, so a clip editor opened after the Device View keeps the taller height until dragged, deliberately. No test reaches this clamp. The faces not showing, like the paused session view, mark themselves stale rather than rebuilding on every change, and catch up when shown ([What a change costs the interface](ui-cost-of-a-change.md)).

## The 30 Hz timer

`ControlWindow`'s timer polls what the engine does not broadcast (`Session::recordingStopped`, the session view's slot override when enabled, and `Session::djPoll` for the DJ booth's finished file reads and stale bounces), applies pending pane layout, updates the Info View hint (`InfoHints.h`), and rewrites the readout. It no longer plays track automation: since commit `fe163c8` the engine does ([Track automation](automation.md)).

## Related

- [Arrangement view](arrangement-view.md)
- [Colours and typography](colours-and-typography.md)
- [Computer MIDI keyboard](computer-keyboard.md)
- [Playhead rendering](playhead.md)
- [juce::File::createOutputStream appends](juce-output-stream-appends.md)
- [Transport, tempo and loop](transport.md)
- [What a change costs the interface](ui-cost-of-a-change.md)
