---
title: Playhead rendering
type: component
summary: Draws both playheads every display refresh from the audio graph's own position, repainting two narrow strips whose painters skip everything else, with a Direct2D fix.
tags: [rhino, ui, rendering, windows]
sources: []
updated: 2026-10-05
---

# Playhead rendering

`native/src/Playhead.*` serves both playheads, the arrangement's and the note editor's. Each panel moves its line from a `juce::VBlankAttachment`, so it follows the display's refresh rather than a timer, and nothing about it schedules audio.

## Where the position comes from

`playheadTime()` reads the playback context's latency-adjusted audible position (`getAudibleTimelineTime`) while playing. `TransportControl::getPosition` is only copied from the audio thread by a 50 Hz message-thread timer, so a line drawn from it would step behind the sound. A pending seek is shown at once, while the audio thread adopts it, but a scheduled future jump never moves the line ahead of the audio. Positions are kept fractional rather than rounded to whole pixels.

## Damage, not repaints

`movePlayhead` repaints two narrow strips, about six pixels each, at the old and new positions, never the panel. `playheadDamage` rounds them outward to physical pixels for fractional display scales.

Both painters skip what such a strip does not reach (commit `c9d7d5d`); before, they drew everything and let the clip throw it away, still paying for the text shaping, tempo-map conversions and model queries. The arrangement draws track cards, bar numbers, ruler times, grid lines, the empty-lane hint, notes, automation segments and clip names only where the repaint reaches, measures each clip name once per sync, and reads per-track facts once per sync. The note editor takes its cell size once per paint and skips the header numbers, keys, rows and sustain lines outside the repaint. Measured with `--profile-ui`, a strip fell from 1.3-1.5 ms to 0.4 ms in the arrangement and from about 0.6 ms to 12 µs in the note editor. A hidden note editor skips its vblank update altogether ([What a change costs the interface](ui-cost-of-a-change.md)).

Two cases the strips cannot cover are handled by their callers:

- A parked playhead is drawn only in the ruler and a rolling one sweeps the lanes, so starting or stopping changes the line's length without moving it; `Arrangement::updatePlayhead` repaints both strips itself.
- A MIDI take is drawn as it is played, between the take's start and the line. `Arrangement::repaintRecordingBand` invalidates what that band grew by each frame, and the whole band only when a note starts or ends ([Recording and the count-in](recording.md)).

## The Direct2D handoff

In the pinned JUCE (`37c894f`, `native/scripts/fetch-dependencies.py`), the Windows Direct2D peer runs vblank listeners *before* it paints the damage collected earlier, and `repaint()` only invalidates; `WM_PAINT` would collect that damage later. Left alone, the frame erased the old line but clipped out the new one. After repainting, `movePlayhead` calls `UpdateWindow`, which delivers `WM_PAINT` at once; for this backend that only collects damage, and drawing still happens at vblank. It applies only on Windows with Direct2D; macOS and the software renderer use plain invalidation. **Recheck it whenever JUCE is upgraded.**

Damage has to be exact on Direct2D: it presents from rotating buffers, so a region nobody repainted shows an *older* frame. The software renderer (`RHINO_RENDERER=software`) keeps one surface where such a region still holds the last correct frame, which makes it a useful control when a repaint artefact appears.

## Tests

`ClipGeometryTest.cpp` checks the damage rectangles. `Rendering.inc` moves each playhead through fractional positions, wraparound and hiding, and requires every incremental frame to match a full render pixel for pixel at scales 1.0, 1.25 and 2.0. `PartialRepaint.inc` paints strips, bands and seams of both panels twice under the same clip, culled and with the `cullRepaints` test seam off, and requires identical pixels; it cannot compare against the whole paint ([JUCE's rasteriser is not clip-invariant](juce-rasteriser-not-clip-invariant.md)). The Direct2D handoff itself is exercised only when `RHINO_NATIVE_RENDER_TEST=1` is set, because that check needs a real desktop peer: it puts the arrangement on the desktop off-screen, forces the Direct2D engine and queues damage the way a vblank would. None of this measures live frame pacing.

## Related

- [Arrangement view](arrangement-view.md)
- [Note editor (StepGrid)](note-editor.md)
- [App shell and control bar](app-shell.md)
- [The executable is RhinoDAW, not Rhino](executable-named-rhinodaw.md)
- [Tracktion Engine with a native JUCE UI](tracktion-and-juce.md)
- [Transport, tempo and loop](transport.md)
- [What a change costs the interface](ui-cost-of-a-change.md)
- [Profile paint on a software image](profile-paint-on-a-software-image.md)
