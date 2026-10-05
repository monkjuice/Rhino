---
title: Region editing
type: concept
summary: Copy, cut, paste, duplicate and delete all act on a region, a span of time across a run of tracks, never on a set of clips.
tags: [rhino, arrangement, editing, clipboard]
sources: []
updated: 2026-10-05
---

# Region editing

In the arrangement the clipboard commands act on a **region**: a span of time across a run of tracks, not a set of clips, as in Live. The UI half is `native/src/ArrangementSelection.cpp`; the edit half is `Session::copyClipRegion`, `clearClipRegion` and `pasteClipRegion` in `native/src/SessionRegion.cpp`.

## Where the region comes from

`Arrangement::effectiveRegion` resolves it: a span dragged out over the lanes if there is one, otherwise the bounding rectangle of the selected clips. Selecting a clip therefore sets the region to that clip, which is why Ctrl+D on a clip and on its dragged span are the same command; selecting two clips far apart also reaches whatever lies between them on those tracks. A click that never drags leaves a zero-width region, the *insert point*, where a paste lands and playback starts.

## The operations

- **Copy (Ctrl+C)** takes a `ClipRegion`: the rectangle's own width and track count, plus a `ClipSnapshot` per clip inside, cut at the edges. The size is carried rather than measured off the clips, because an empty lane or a trailing rest is part of what was copied.
- **Delete** empties the region (`clearClipRegion`): split at both edges, then remove what lies wholly inside, so trimming, cutting a hole and deleting whole clips are one case. Deleting selected clips is one undo step however many there are (`Session::deleteClips`). **Cut (Ctrl+X)** is copy, then delete.
- **Paste (Ctrl+V)** clears the whole destination rectangle, then inserts, so it replaces what it lands on. A row whose kind does not match its lane, or that lands on a group bus, refuses the whole paste before anything is cleared. A failure after that point (a source file gone, an instrument that will not load) takes back the whole transaction with `undoCurrentTransactionOnly`, cleared ground and added lanes included (commit `b5d17e0`). Lanes a paste adds below the last one get their fader, slots and routing at once. The pasted rectangle becomes the selection, so a second paste replaces the first.
- **Duplicate (Ctrl+D)** pastes at the region's own end and moves the region onto the copy, so holding Ctrl+D keeps extending.
- **Split (Ctrl+E)** cuts at the insert line: the selected clips it crosses if any, otherwise every clip it crosses on the region's tracks, so nothing needs selecting first.

## Why snapshots, not ids

The clipboard holds values (`Session::ClipSnapshot`): position, offset and `rhinoClipColour`, plus, for a MIDI clip, the whole note list and its track's instrument as a catalog id. For an audio clip it holds the source file and speed, and since commit `b5d17e0` also the clip's gain, pan, pitch, fades, mute, reverse and warp (mode, clip tempo in beats, markers). Before that a copy played the raw file at speed one instead of what the original played. A cut deletes its sources, and an undo between copy and paste can too, so a clipboard of ids would paste nothing ([Hold ids, not pointers](ids-not-pointers.md)).

## In the note editor

`StepGridEditing.cpp` applies the same rule in steps: the marquee's span, the selected notes rounded out to whole steps, or the whole clip after Ctrl+A. One difference: a note paste clears only the pitches and steps it writes, not the whole rectangle.

## Related

- [Clips never overlap](clip-placement.md)
- [Pointer and selection rules](pointer-and-selection.md)
- [Note editor (StepGrid)](note-editor.md)
- [Arrangement view](arrangement-view.md)
