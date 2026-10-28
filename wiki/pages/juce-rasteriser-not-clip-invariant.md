---
title: JUCE's rasteriser is not clip-invariant
type: gotcha
summary: A path's anti-aliased edge can come out differently under a different clip, so check a culled repaint against an unculled paint under the same clip, never against the whole paint, and paint both on a software image, since a Direct2D bitmap draws a path a shade differently after drawing others like it.
tags: [rhino, juce, ui, testing, direct2d]
sources: []
updated: 2026-10-28
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

## Direct2D differs even under the same clip, 2026-10-28

When the new document's stack changed to MIDI, MIDI, Audio, Audio ([Track kinds: audio and MIDI](track-kinds.md)), `PartialRepaint.inc` failed on one pixel of a card's fold arrow (195,408: culled `E26262`, full `E05959`). The arrow's geometry was identical to the integer in both paints, and each mode repeated its own result. The full paint had drawn three identical arrows on the rows above before that one; the culled paint drew it alone; and the canvases were plain `juce::Image`s, Direct2D bitmaps on Windows. Folding or unfolding the returns made no difference: the track colours did, which is why the old stack happened to pass. Since then both canvases are `juce::SoftwareImageType()`, and the two paints agree.

This is the Direct2D behaviour Forge met ([A buffered child broke Forge's byte-identical paint](buffered-child-breaks-pixel-stability.md)): the same path drawn into a Direct2D bitmap comes out a shade differently depending on what was drawn before it. Whether the clip difference above is the same effect was not checked. Paint any picture that must match another to the pixel on a software image ([Profile paint on a software image](profile-paint-on-a-software-image.md)).

## Related

- [Playhead rendering](playhead.md)
- [What a change costs the interface](ui-cost-of-a-change.md)
- [Writing Rhino tests](writing-rhino-tests.md)
- [Seeing the UI without taking the screen](headless-ui-snapshots.md)
- [A buffered child broke Forge's byte-identical paint](buffered-child-breaks-pixel-stability.md)
- [Profile paint on a software image](profile-paint-on-a-software-image.md)
