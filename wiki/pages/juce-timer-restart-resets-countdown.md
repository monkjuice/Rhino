---
title: Starting a running juce::Timer restarts its count
type: gotcha
summary: startTimer and startTimerHz on a running timer reset its countdown, so a setup function re-run faster than the timer it starts keeps that timer from ever firing; the rack's 30 Hz automation follow froze Rhino EQ's spectrum and every other face's 24 Hz frames this way until setTarget stopped restarting a running clock (2026-10-26).
tags: [juce, ui, timers, gotcha, rhino]
sources: []
updated: 2026-10-26
---

# Starting a running juce::Timer restarts its count

`juce::Timer::startTimer` and `startTimerHz` on a timer that is already running do not leave it alone: they set the
interval and start the countdown again from zero. Any path that re-runs a setup function more often than the timer it
starts fires the timer never. Nothing logs, asserts or looks broken except that the thing the timer drives stops
moving.

## The case in Rhino (2026-10-26)

User report: Rhino EQ's live spectrum did not animate on audio tracks.

- `DeviceRack::timerCallback` (30 Hz) calls `followAutomation()` while the transport rolls, which calls
  `DeviceEditorPanel::setTarget` on every face whose `followsAutomation()` finds a knob on a lane not taken over by
  hand. `DrumRackView::followAutomation` in `ui/rack/DrumRackWindow.cpp` does the same through `face.setTarget`.
- `setTarget` called `startTimerHz(24)` every time. A 41 ms frame restarted every 33 ms never came due, so
  `tickEqSpectrum` never ran.
- It showed on audio tracks because they sound only while the song plays, which is exactly when the rack follows
  lanes. An EQ on a MIDI track played from the keyboard with the transport stopped still animated. The clue was
  rhino.log: "4 Freq: automation shown in the arrangement" on that EQ just before the report.
- Every face that asks for frames starved the same way while it had a lane: Rhino Tune's meter, the Vocoder's bands,
  the Drum Rack's pad and key lights, in the rack and in `DrumRackWindow`.

The fix (`setTarget` in `ui/rack/DeviceEditorPanel.cpp`) starts the clock only when `!isTimerRunning()`, and still
stops it for a face that wants no frames. The re-target path was left as it is, because `setTarget` is the rack's
established refresh path; only the restart was removed.

## The rule

Guard a start in any function that may run repeatedly: `if (!isTimerRunning()) startTimerHz(n);`. Call a bare start
only where restarting the count is the point, such as a debounce.

## Testing it

`DeviceRackTest.cpp` (after the 4OSC "A face stops following once its lane is cleared" check) puts an EQ on audio
track 1 with a lane on band 4's frequency (`RhinoEqDevice::frequencyParameter(3)`), sets the face's timer to 1000 ms
through friend access as a stand-in for a clock part-way through its count, calls `chainView.followAutomation()` three
times and requires `getTimerInterval() == 1000`; before the fix it read 41.

A real-time test, sleeping and then pumping `juce::Timer::callPendingTimersSynchronously()`, was rejected: that fires
every timer in the process, so the rack's own timer re-targets first and hides the bug, and JUCE's timer thread waits
up to 300 ms for a callback the runner has not dispatched, which makes such a test flaky.

## Related

- [Device rack and device editors](device-rack.md)
- [Rhino EQ](rhino-eq.md)
- [Track automation](automation.md)
- [Writing Rhino tests](writing-rhino-tests.md)
