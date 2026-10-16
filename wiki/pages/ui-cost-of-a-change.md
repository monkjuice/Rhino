---
title: What a change costs the interface
type: convention
summary: Session announces every change synchronously to every listening panel, so a drag announces once, a hidden panel goes stale, a frame reads only the controls it shows, painters skip what a repaint does not reach, and --profile-ui measures it.
tags: [rhino, ui, performance]
sources: []
updated: 2026-10-16
---

# What a change costs the interface

`Session` announces every change with `sendSynchronousChangeMessage()`, and every panel listening rebuilds in that call stack: the arrangement re-reads every clip and converts every note through the tempo map, and the rack, the note editor, the audio clip editor and the DJ view rebuild too ([Session, the model](session-model.md)). On a large document the arrangement alone takes about a millisecond. Frame pacing and pointer response are core requirements, so these rules keep a gesture from paying that per pixel. They come from commits `e7c9554`, `c9d7d5d`, `b6c1496` and `fe163c8` (2026-10-05).

## The rules

- **A drag announces once, at its end.** Inside a device-parameter gesture, `setDeviceParameter` announces only on `Session::deviceParameterValues`, which the rack hears to refresh the dragged device's panel alone. Inside a fader or pan gesture nothing is announced, and the dragged control draws its own value. The note-velocity gesture works the same way (`beginNoteVelocityGesture`, 2026-10-16), covering a velocity-lane drag or stroke and held V. Inside it `setNoteVelocities` marks the track modified for the DJ booth but announces nothing, and the note editor re-reads the model after each write itself (`StepGrid::refreshNoteVelocities`), since its change listener does not run ([Note editor velocity lane](note-velocity-lane.md)). `end*Gesture` tells everyone once; the velocity gesture's end does so only if something changed. `Arrangement/scenarios/UiCost.inc` counts both. An end always decrements its depth and closes the transaction, even when the device or track has gone in the meantime. Until 2026-10-13, `endDeviceParameterGesture`, `endTrackVolumeGesture` and `endTrackPanGesture` returned early in that case. The depth then stayed counted, every later change reached the rack alone, and `gestureHeld` stayed true. An [Ableton Push](ableton-push.md) encoder held while its device was deleted made this reachable, and `PushController.inc` tests it. A second press during a held gesture can strand a bracket the same way. In the note editor a second finger, or a pen used beside the mouse, used to overwrite the velocity gesture, so `gestureHeld` stayed true and later velocity edits went unannounced. `StepGrid::mouseDown` now ends any held gesture before starting another (2026-10-16). A new continuous control needs a gesture bracket, plus a broadcaster of its own if one view must follow it live. Settings that are not parameters can skip the session while dragged: the Drum Rack's sample knobs write the device directly without undo, then restore and write once through `editDeviceSettings` ([Drum Rack sample editor](drum-rack-sample-editor.md)).
- **A hidden panel waits.** A panel the shell has switched off checks `isHiddenInShell` (`native/src/ui/shared/UiVisibility.h`) in its change callback, marks itself stale and catches up in `visibilityChanged`. This covers the DJ view while the arrangement shows and whichever lower-pane faces are not showing; `UiCost.inc` checks it on a hidden `DjView`, whose decks wait for it to be shown. The arrangement does not follow it yet: hidden while the DJ view shows, it still syncs on every change (`Arrangement::changeListenerCallback`, checked 2026-10-11). Its timers and vblank work skip too: a hidden note editor does not chase the playhead, and hidden device faces stop animating. A component with no parent, as tests and the profiler build them, never counts as hidden. `DeviceRack`, `StepGrid`, `AudioClipPanel` and `DjView` follow this, and a new panel the shell can hide should too.
- **Nothing announces per frame.** The engine plays automation, and the rack reads moving knobs back itself ([Track automation](automation.md)).
- **A frame reads only the controls it shows.** `Session::deviceParameters(track, slot)` reads and formats every control: 1.75 ms median for a [Drum Rack](drum-rack.md)'s 768. So a frame path reads a range (`deviceParameters(track, slot, first, count)`), one (`deviceParameter`, optional) or the count (`deviceParameterCount`), all in `SessionDevices.cpp` (2026-10-07). The drum face reads the selected pad's six (16.7 µs), and `SessionAutomation.cpp`'s three callers read one each. The fallback editor window reads the six it shows, the selected pad's on a Drum Rack, on its 30 Hz timer.
- **A rebuild reads each fact once.** The arrangement reads what its painter asks about each track once per sync (`TrackFacts`), not per row per frame, because each question walks the engine's track list. It also hands `Session::clipPluginCount` the clip in hand rather than an id to look up again.
- **A painter draws only what the repaint reaches** (`repaintArea(g)`; [Playhead rendering](playhead.md)). Check it with the `cullRepaints` seam, under the same clip ([JUCE's rasteriser is not clip-invariant](juce-rasteriser-not-clip-invariant.md)).
- **Releasing a knob does not rebuild the playback graph** unless the gesture changed the device's latency (`endDeviceParameterGesture`), which delay compensation has to hear about.
- **Nothing made again for a deck mid-gesture.** `djPoll` makes a DJ deck's cell again only while `Session::gestureHeld()` is false - a knob, fader, tempo, audio clip or note-velocity gesture (the last counted by its bracket, because held V presses no mouse button), or any mouse button down - so a note dragged in a cell is not cut on every step; and a device, a parameter or a curve asks nothing of a deck at all, being heard live through the track ([DJ view and the booth](dj-view.md)). The same hold used to keep a bounce's document copy (0.3-6 ms with built-in devices, 106 ms with Forge) off the message thread mid-drag, after one started under a held knob froze the display (2026-10-11); the bounce went on 2026-10-12 ([superseded](dj-bounce-on-a-copy.md)).
- **A face repaints what changed and keeps what it has shaped.** The [Drum Rack](drum-rack.md) face shows the pattern:
  - `repaintDrums` (`DeviceEditorPanelDrums.cpp`) compares one string of everything the pads and map are drawn from (bank, selection, which of the 128 notes are filled, the sixteen shown pads), so a knob drag repaints only the selected pad's side.
  - Its 24 Hz tick repaints only the pads, map cells, key name and playhead strip that moved.
  - `drawLine` keeps each shaped line as a `juce::GlyphArrangement`, keyed by text, box, justification and font and cleared past 2,048 entries, because shaping text was most of the paint.
  - `padPicture` renders a synth's strike at 16 kHz instead of the device's rate; it and `samplePicture` keep the selected pad's picture while sound, settings and width are unchanged, at two widths, because the rack's face and the Drum Rack window draw one pad at different widths and evicted each other every frame.
  - A readout of text compares before it repaints. `DjMixerPanel`'s tempo readout formats its text on the 30 Hz tick and repaints its rectangle only when that differs from `drawnBpm`, the text the last paint drew, which replaced `DjView`'s blind repaint of the mixer's corner every tick ([DJ view and the booth](dj-view.md)); the deck display's readouts do the same. The [track level meters](level-meters.md) compare bar and hold heights in whole pixels, so a silent song repaints nothing on its frame clock.

## Measuring

`RhinoDAW.exe --profile-ui` prints median message-thread costs ([Writing Rhino tests](writing-rhino-tests.md)). It paints on a software image for a reason: [Profile paint on a software image](profile-paint-on-a-software-image.md). Medians from the commits, before and after: knob drag step 1.8 ms to 63 µs, fader step 1.8 ms to 4 µs, arrangement sync 1.8 ms to 0.9 ms, arrangement playhead strip 1.3-1.5 ms to 0.4 ms. Most of what is left of a sync is note positions converted through the tempo map. Caching them would need a content revision per clip, which the engine does not expose. Since 2026-10-16 it also times "note editor velocity drag step" and "note editor velocity lane repaint"; no medians for them were recorded here.

The Drum Rack face is timed by `runDrumRackFaceTest`, not `--profile-ui`. Its medians:
- 2026-10-06, sixteen pads: the whole face painted in 1.27 ms, down from 7 ms, against about 0.69 ms for Rhino FM's face in the same runner. Rendering the strike picture at 16 kHz took 0.36 ms against 1.4 ms at 48 kHz.
- 2026-10-07, 128 pads and the sample editor: a knob frame refreshes in 122 µs (270 µs before the range reads) and paints the selected side in 0.9 ms; the whole face paints in 2.24 ms, the map and the sample picture being the additions.

## Related

- [Session, the model](session-model.md)
- [App shell and control bar](app-shell.md)
- [Device rack and device editors](device-rack.md)
- [Drum Rack](drum-rack.md)
- [Drum Rack sample editor](drum-rack-sample-editor.md)
- [Arrangement view](arrangement-view.md)
- [Note editor velocity lane](note-velocity-lane.md)
- [Playhead rendering](playhead.md)
- [Ableton Push 1](ableton-push.md)
- [Forge's panel repaints whole at 24 Hz](forge-panel-repaint-cost.md)
