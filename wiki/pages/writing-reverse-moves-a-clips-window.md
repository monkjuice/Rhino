---
title: Writing a clip's reverse switch moves its window
type: gotcha
summary: Tracktion mirrors an audio clip's offset into its file on every write of isReversed, even writing off onto a new clip, so a trimmed copy jumped to the far end of its file; write such a property only when the value differs.
tags: [rhino, tracktion, audio-clips, clipboard]
sources: []
updated: 2026-10-15
---

# Writing a clip's reverse switch moves its window

`te::AudioClipBase::valueTreePropertyChanged` calls `updateReversedState()` for any notification on `IDs::isReversed`, and that calls `reverseLoopPoints()` (skipped only during undo and redo). For a non-looping clip that sets offset = source end - length - offset: the clip's window is mirrored to the other end of its file. Nothing checks whether the value really changed, and a property going from absent to present notifies, so writing `false` onto a freshly inserted clip mirrors it too. Checked in `modules/tracktion_engine/model/clips/tracktion_AudioClipBase.cpp` at the pinned commit `4536d8a` ([Directories not to read](off-limits-directories.md) says how).

## What it broke

`Session::pasteClipRegion` (`native/src/model/SessionRegion.cpp`) called `setIsReversed(snapshot.reversed)` on every new audio clip, so copy, paste and duplicate of a **trimmed** clip played the wrong stretch. A 0.655 s snare trimmed to 0.5 s pasted at offset 0.1548 s, past its attack, so the copy looked and sounded quieter; a copy of that copy mirrored back to 0. Fixed in commit `50d87d5`.

## Why no test caught it

A clip that is its whole file is its own mirror, and the old `RegionClipboard.inc` crop (0.25-0.75 s of a 1 s file) is symmetric. A test of anything that touches a clip's offset needs an asymmetric window: the regression block at the end of `native/src/tests/Arrangement/scenarios/RegionClipboard.inc` copies the first 0.3 s of a 1 s clip, copies the copy, sets a never-reversed clip forwards and copies a reversed one.

## The rule

- A paste sets reverse only for a snapshot that was reversed, and first, before warp, speed and the re-asserted position, so a reversed copy keeps the original's window.
- `Session::setAudioClipReversed` (`SessionAudioClips.cpp`) writes the property only when `getIsReversed()` differs.
- In general, a Tracktion clip property with a `valueTreePropertyChanged` side effect is written only when its value differs; writing the default is not harmless.

## Related

- [Region editing](region-editing.md)
- [Audio clip editor](audio-clip-editor.md)
- [Writing Rhino tests](writing-rhino-tests.md)
