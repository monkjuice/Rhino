---
title: JUCE's rasteriser is not clip-invariant
type: gotcha
summary: A path's anti-aliased edge can come out differently under a different clip, so check a culled repaint against an unculled paint under the same clip, never against the whole paint.
tags: [rhino, juce, ui, testing]
sources: []
updated: 2026-10-05
---

# JUCE's rasteriser is not clip-invariant

## Symptom

A test repaints a strip of a panel and compares it with the same pixels of a full paint. It fails on a pixel or two at the edge of a path, even with every culling optimisation switched off. In commit `c9d7d5d` (2026-10-05), an automation node's edge pixel in a 2 px band of the arrangement differed from the whole paint.

## Cause

JUCE anti-aliases a path's edge differently under a different clip region. The same `paint()` therefore gives slightly different edge pixels under a strip clip than under the full bounds. Nothing in Rhino's painters is wrong; the comparison is. This was observed painting into a default `juce::Image`, which on Windows is a Direct2D bitmap ([Profile paint on a software image](profile-paint-on-a-software-image.md)).

## What to do

Paint each area twice **under the same clip**, once culled and once drawing everything, and require identical pixels. `Arrangement` and `StepGrid` carry a private `cullRepaints` flag for this test seam. While it is on, the painters skip whatever lies outside `repaintArea(g)`. `native/src/tests/Arrangement/scenarios/PartialRepaint.inc` paints over magenta, a colour neither panel uses, so a pixel the culled paint forgets cannot pass for one it drew. It covers 3 px columns every 23 px, 2 px rows every 17 px, and the seams the culling reasons about: the card edge, the playhead, the key labels, and a zoomed, scrolled note grid.

Do not loosen the comparison with a tolerance instead. One wide enough to pass an edge pixel can also pass a real fault, such as a faint line that was never drawn.

Prove such a test bites by breaking a cull on purpose. Breaking the note editor's keyboard cull fails this scenario, and no other check notices it.

## Related

- [Playhead rendering](playhead.md)
- [What a change costs the interface](ui-cost-of-a-change.md)
- [Writing Rhino tests](writing-rhino-tests.md)
- [Seeing the UI without taking the screen](headless-ui-snapshots.md)
