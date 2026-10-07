---
title: A juce::Path holding only a start point is empty
type: gotcha
summary: juce::Path::isEmpty() ignores startNewSubPath points, so a loop that asks it whether to start or continue a line starts a new subpath every time and strokes nothing; keep a flag of your own.
tags: [rhino, juce, ui, painting]
sources: []
updated: 2026-10-07
---

# A juce::Path holding only a start point is empty

`juce::Path::isEmpty()` counts only segments. A path that holds nothing but a `startNewSubPath` point is still empty.

So the natural way to build a polyline fails silently:

- `if (path.isEmpty()) path.startNewSubPath(x, y); else path.lineTo(x, y);` starts a new subpath on every point, because the path never stops being empty.
- The result is a run of lone points, and stroking it draws nothing. No assertion fires.

Found on 2026-10-07 in the [Drum Rack sample editor](drum-rack-sample-editor.md)'s envelope line (`DeviceEditorPanelDrumSample.cpp`), which was invisible until the face was snapshotted ([Seeing the UI without taking the screen](headless-ui-snapshots.md)). The fix is a separate `begun` flag that decides between `startNewSubPath` and `lineTo`.

## Related

- [Seeing the UI without taking the screen](headless-ui-snapshots.md)
- [JUCE's isBold() is false for SemiBold](juce-isbold-misses-semibold.md)
- [JUCE's rasteriser is not clip-invariant](juce-rasteriser-not-clip-invariant.md)
