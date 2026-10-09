---
title: What a change costs the interface
type: convention
summary: Session announces every change synchronously to every listening panel, so a drag announces once, a hidden panel goes stale, a frame reads only the controls it shows, painters skip what a repaint does not reach, and --profile-ui measures it.
tags: [rhino, ui, performance]
sources: []
updated: 2026-10-09
---

# What a change costs the interface

`Session` announces every change with `sendSynchronousChangeMessage()`, and every panel listening rebuilds in that call stack: the arrangement re-reads every clip and converts every note through the tempo map, and the rack, the note editor, the audio clip editor and the paused session view rebuild too ([Session, the model](session-model.md)). On a large document the arrangement alone takes about a millisecond. Frame pacing and pointer response are core requirements, so these rules keep a gesture from paying that per pixel. They come from commits `e7c9554`, `c9d7d5d`, `b6c1496` and `fe163c8` (2026-10-05).

## The rules

- **A drag announces once, at its end.** Inside a device-parameter gesture, `setDeviceParameter` announces only on `Session::deviceParameterValues`, which the rack hears to refresh the dragged device's panel alone. Inside a fader or pan gesture nothing is announced, and the dragged control draws its own value. `end*Gesture` tells everyone once. `Arrangement/scenarios/UiCost.inc` counts both. A new continuous control needs a gesture bracket, plus a broadcaster of its own if one view must follow it live. Settings that are not parameters can skip the session while dragged: the Drum Rack's sample knobs write the device directly without undo, then restore and write once through `editDeviceSettings` ([Drum Rack sample editor](drum-rack-sample-editor.md)).
- **A hidden panel waits.** A panel the shell has switched off checks `isHiddenInShell` (`native/src/UiVisibility.h`) in its change callback, marks itself stale and catches up in `visibilityChanged`. This covers the paused session view and whichever lower-pane faces are not showing. Its timers and vblank work skip too: a hidden note editor does not chase the playhead, and hidden device faces stop animating. A component with no parent, as tests and the profiler build them, never counts as hidden. `DeviceRack`, `StepGrid`, `AudioClipPanel`, `SessionView` and `DjView` follow this, and a new panel the shell can hide should too.
- **Nothing announces per frame.** The engine plays automation, and the rack reads moving knobs back itself ([Track automation](automation.md)).
- **A frame reads only the controls it shows.** `Session::deviceParameters(track, slot)` reads and formats every control: 1.75 ms median for a [Drum Rack](drum-rack.md)'s 768. So a frame path reads a range (`deviceParameters(track, slot, first, count)`), one (`deviceParameter`, optional) or the count (`deviceParameterCount`), all in `SessionDevices.cpp` (2026-10-07). The drum face reads the selected pad's six (16.7 µs), and `SessionAutomation.cpp`'s three callers read one each. The fallback editor window reads the six it shows, the selected pad's on a Drum Rack, on its 30 Hz timer.
- **A rebuild reads each fact once.** The arrangement reads what its painter asks about each track once per sync (`TrackFacts`), not per row per frame, because each question walks the engine's track list. It also hands `Session::clipPluginCount` the clip in hand rather than an id to look up again.
- **A painter draws only what the repaint reaches** (`repaintArea(g)`; [Playhead rendering](playhead.md)). Check it with the `cullRepaints` seam, under the same clip ([JUCE's rasteriser is not clip-invariant](juce-rasteriser-not-clip-invariant.md)).
- **Releasing a knob does not rebuild the playback graph** unless the gesture changed the device's latency (`endDeviceParameterGesture`), which delay compensation has to hear about.
- **A face repaints what changed and keeps what it has shaped.** The [Drum Rack](drum-rack.md) face shows the pattern:
  - `repaintDrums` (`DeviceEditorPanelDrums.cpp`) compares one string of everything the pads and map are drawn from (bank, selection, which of the 128 notes are filled, the sixteen shown pads), so a knob drag repaints only the selected pad's side.
  - Its 24 Hz tick repaints only the pads, map cells, key name and playhead strip that moved.
  - `drawLine` keeps each shaped line as a `juce::GlyphArrangement`, keyed by text, box, justification and font and cleared past 2,048 entries, because shaping text was most of the paint.
  - `padPicture` renders a synth's strike at 16 kHz instead of the device's rate; it and `samplePicture` keep the selected pad's picture while sound, settings and width are unchanged, at two widths, because the rack's face and the Drum Rack window draw one pad at different widths and evicted each other every frame.
  - A readout of text compares before it repaints. `DjMixerPanel`'s tempo readout formats its text on the 30 Hz tick and repaints its rectangle only when that differs from `drawnBpm`, the text the last paint drew, which replaced `DjView`'s blind repaint of the mixer's corner every tick ([DJ view and the booth](dj-view.md)); the deck display's readouts do the same.

## Measuring

`RhinoDAW.exe --profile-ui` prints median message-thread costs ([Writing Rhino tests](writing-rhino-tests.md)). It paints on a software image for a reason: [Profile paint on a software image](profile-paint-on-a-software-image.md). Medians from the commits, before and after: knob drag step 1.8 ms to 63 µs, fader step 1.8 ms to 4 µs, arrangement sync 1.8 ms to 0.9 ms, arrangement playhead strip 1.3-1.5 ms to 0.4 ms. Most of what is left of a sync is note positions converted through the tempo map. Caching them would need a content revision per clip, which the engine does not expose.

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
- [Playhead rendering](playhead.md)
- [Forge's panel repaints whole at 24 Hz](forge-panel-repaint-cost.md)
