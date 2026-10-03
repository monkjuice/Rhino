---
title: App shell and control bar
type: component
summary: Main.cpp's window, control bar, docked browser and single lower pane, plus the 30 Hz timer that polls what the engine never broadcasts.
tags: [rhino, ui, shell]
sources: []
updated: 2026-10-03
---

# App shell and control bar

`native/src/Main.cpp` (about 2,200 lines as of 2026-10-03) holds `Application` (startup, log, test flags, a `.rhinoedit` given as first argument), `Window` and the content, `ControlWindow`: about 1,600 lines defined inline, the next split candidate in `AGENTS.md` ([Keeping files small](keeping-files-small.md)). No test can reach it, so `RHINO_SHELL_SNAPSHOT=<png>` under `--startup-test` writes the shell to a PNG ([Seeing the UI without taking the screen](headless-ui-snapshots.md)).

## The window

`Window` draws its own title bar but sets JUCE's drop-shadow flag, because on Windows that flag is what gives a borderless window native frame styles: without it there is no Aero Snap, no snap layouts, and maximise covers the taskbar. Full screen (F12) is kiosk mode, separate from maximise, with three overrides keeping the title bar. `StartupScreen.h` marks real completed startup stages, not a simulated percentage.

## The control bar

Under the 72 px bar sit the docked browser, whose 3 px divider is its resize handle, and the arrangement, with no margins or frames. `layoutControlBar` places from a running x: browser toggle, tempo and signature, then rewind, stop, play and record; from the right edge inwards, redo, undo, then the metronome and its menu. The readout is centred in what remains, capped at 480 px, and hidden rather than squeezed when under 260 px remain. `native/README.md` (*The shell's chrome*) still puts the metronome beside the signature and calls the readout elastic (checked 2026-10-03).

- `ControlBarFields.*`: `ValueDragBox`, the tempo and signature fields you drag; one drag is one undo step (`Session::beginTempoGesture`).
- `ControlBarIcons.*`: glyphs are paths worn by the borderless `IconButton`, because Unicode transport symbols fell through to a different system face on every machine.
- `TransportDisplay.*`: three columns in one recess; when narrow it drops whole readings in a fixed order and never moves the position or clock. `WallClock.h` spells every time, here and on the timeline ruler.

## The lower pane

Note editor, audio clip editor and Device View are three faces of one pane (`LowerPane`). Double-clicking a clip opens its editor, which then follows the clip selection; a card click opens the Device View only when the pane is free. It floats over the arrangement's foot (`Arrangement::setBottomInset`): pushing the lanes up refitted them under a dragged split, so every clip grew and shrank under the pointer. A layout change asked for by a click waits for the timer and for no mouse button down, because that press may be starting a drag. `native/README.md` and `README.md` still describe two independent panels, merged in commit `a33503f` (2026-09-30).

## The 30 Hz timer

`ControlWindow`'s timer polls what the engine does not broadcast (`Session::recordingStopped`, the session view's slot override when enabled), applies pending pane layout, updates the Info View hint (`InfoHints.h`), drives track automation during playback via `Session::applyTrackAutomationAt`, and rewrites the readout. Both READMEs still call it a 10 Hz text timer.

## Related

- [Arrangement view](arrangement-view.md)
- [Colours and typography](colours-and-typography.md)
- [Computer MIDI keyboard](computer-keyboard.md)
- [Playhead rendering](playhead.md)
- [juce::File::createOutputStream appends](juce-output-stream-appends.md)
- [Transport, tempo and loop](transport.md)
