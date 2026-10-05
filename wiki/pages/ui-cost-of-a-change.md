---
title: What a change costs the interface
type: convention
summary: Session announces every change synchronously to every listening panel, so a drag announces once, a hidden panel goes stale, painters skip what a repaint does not reach, and --profile-ui measures it.
tags: [rhino, ui, performance]
sources: []
updated: 2026-10-05
---

# What a change costs the interface

`Session` announces every change with `sendSynchronousChangeMessage()`, and every panel listening rebuilds in that call stack: the arrangement re-reads every clip and converts every note through the tempo map, and the rack, the note editor, the audio clip editor and the paused session view rebuild too ([Session, the model](session-model.md)). On a large document the arrangement alone takes about a millisecond. Frame pacing and pointer response are core requirements, so these rules keep a gesture from paying that per pixel. They come from commits `e7c9554`, `c9d7d5d`, `b6c1496` and `fe163c8` (2026-10-05).

## The rules

- **A drag announces once, at its end.** Inside a device-parameter gesture, `setDeviceParameter` announces only on `Session::deviceParameterValues`, which the rack hears to refresh the dragged device's panel alone. Inside a fader or pan gesture nothing is announced, and the dragged control draws its own value. `end*Gesture` tells everyone once. `Arrangement/scenarios/UiCost.inc` counts both. A new continuous control needs a gesture bracket, plus a broadcaster of its own if one view must follow it live.
- **A hidden panel waits.** A panel the shell has switched off checks `isHiddenInShell` (`native/src/UiVisibility.h`) in its change callback, marks itself stale and catches up in `visibilityChanged`. This covers the paused session view and whichever lower-pane faces are not showing. Its timers and vblank work skip too: a hidden note editor does not chase the playhead, and hidden device faces stop animating. A component with no parent, as tests and the profiler build them, never counts as hidden. `DeviceRack`, `StepGrid`, `AudioClipPanel` and `SessionView` follow this, and a new panel the shell can hide should too.
- **Nothing announces per frame.** The engine plays automation, and the rack reads moving knobs back itself ([Track automation](automation.md)).
- **A rebuild reads each fact once.** The arrangement reads what its painter asks about each track once per sync (`TrackFacts`), not per row per frame, because each question walks the engine's track list. It also hands `Session::clipPluginCount` the clip in hand rather than an id to look up again.
- **A painter draws only what the repaint reaches** (`repaintArea(g)`; [Playhead rendering](playhead.md)). Check it with the `cullRepaints` seam, under the same clip ([JUCE's rasteriser is not clip-invariant](juce-rasteriser-not-clip-invariant.md)).
- **Releasing a knob does not rebuild the playback graph** unless the gesture changed the device's latency (`endDeviceParameterGesture`), which delay compensation has to hear about.

## Measuring

`RhinoDAW.exe --profile-ui` prints median message-thread costs ([Writing Rhino tests](writing-rhino-tests.md)). It paints on a software image for a reason: [Profile paint on a software image](profile-paint-on-a-software-image.md). Medians from the commits, before and after: knob drag step 1.8 ms to 63 µs, fader step 1.8 ms to 4 µs, arrangement sync 1.8 ms to 0.9 ms, arrangement playhead strip 1.3-1.5 ms to 0.4 ms. Most of what is left of a sync is note positions converted through the tempo map. Caching them would need a content revision per clip, which the engine does not expose.

## Related

- [Session, the model](session-model.md)
- [App shell and control bar](app-shell.md)
- [Device rack and device editors](device-rack.md)
- [Arrangement view](arrangement-view.md)
- [Playhead rendering](playhead.md)
- [Forge's panel repaints whole at 24 Hz](forge-panel-repaint-cost.md)
