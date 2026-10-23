---
title: Region editing
type: concept
summary: In the lanes, copy, cut, paste, duplicate and delete act on a region, a span of time across a run of tracks, never on a set of clips; on a focused card they move whole tracks.
tags: [rhino, arrangement, editing, clipboard]
sources: []
updated: 2026-10-23
---

# Region editing

In the arrangement's lanes the clipboard commands act on a **region**: a span of time across a run of tracks, not a set of clips, as in Live. The UI half is `native/src/ui/arrangement/ArrangementSelection.cpp`; the edit half is `Session::copyClipRegion`, `clearClipRegion` and `pasteClipRegion` in `native/src/model/SessionRegion.cpp`. With a card focused, or from the card menu, the same keys copy, paste and delete whole tracks instead ([Track clipboard](track-clipboard.md)). The arrangement holds one clipboard of each kind and copying into either empties the other, so Ctrl+V always puts down what was copied last.

## Where the region comes from

`Arrangement::effectiveRegion` resolves it: a span dragged out over the lanes if there is one, otherwise the bounding rectangle of the selected clips. Selecting a clip therefore sets the region to that clip, which is why Ctrl+D on a clip and on its dragged span are the same command; selecting two clips far apart also reaches whatever lies between them on those tracks. A click that never drags leaves a zero-width region, the *insert point*, where a paste lands and playback starts.

A dragged region selects no clips, however much of a clip it covers: sweeping part of a clip is a span of the timeline, and only a press on a clip's header takes the clip (2026-10-23; until then the drag gathered every clip it touched and drew each one as selected). The commands about whole clips - reverse (R), mute, merge (Ctrl+J) - read `Arrangement::clipsToActOn`, which falls back to the clips the region runs across when none is picked, so they still reach what is highlighted. Bounce (Ctrl+B) does not since commit `cbf0cf0`: with no clip picked it bounces the dragged span itself, cut at its edges as Live does, and only picked clips bounce whole ([Bounce to New Track](bounce-to-new-track.md#a-time-selection)). A right-click on a clip inside the region keeps the region rather than selecting the clip, so the clip menu's *Bounce to New Track* there bounces the span. The colour key (C) and the arrow nudge act on a picked clip only.

## The operations

- **Copy (Ctrl+C)** takes a `ClipRegion`: the rectangle's own width and track count, plus a `ClipSnapshot` per clip inside, cut at the edges. The size is carried rather than measured off the clips, because an empty lane or a trailing rest is part of what was copied. A clip that only grazes the region, inside it by less than 10 ms (`minimumSplitSeconds`), is not copied. A drum sample one sample longer than a beat reaches one sample across the next grid line, and copying the region after it used to paste that sample as a sliver that played nothing (commit `972cc0e`). A clip wholly inside the region is copied however short it is.
- **Delete** empties the region (`clearClipRegion`): split at both edges, then remove what lies wholly inside, so trimming, cutting a hole and deleting whole clips are one case. A clip reaching less than 10 ms past an edge is not split but deleted whole, its tail outside included; paste's clear does the same ([Clips never overlap](clip-placement.md)). Deleting selected clips is one undo step however many there are (`Session::deleteClips`). **Cut (Ctrl+X)** is copy, then delete.
- **Paste (Ctrl+V)** clears the whole destination rectangle, then inserts, so it replaces what it lands on. A row whose kind does not match its lane, or that lands on a group bus, refuses the whole paste before anything is cleared. A failure after that point (a source file gone, an instrument that will not load) takes back the whole transaction with `undoCurrentTransactionOnly`, cleared ground and added lanes included (commit `b5d17e0`). Lanes a paste adds below the last one get their fader, slots and routing at once. The pasted rectangle becomes the selection, so a second paste replaces the first.
- **Duplicate (Ctrl+D)** pastes at the region's own end and moves the region onto the copy, so holding Ctrl+D keeps extending.
- **Split (Ctrl+E)** cuts at the insert line: the selected clips it crosses if any, otherwise every clip it crosses on the region's tracks, so nothing needs selecting first.

## Why snapshots, not ids

The clipboard holds values (`Session::ClipSnapshot`): position, offset and `rhinoClipColour`, plus, for a MIDI clip, the whole note list and its track's instrument as a catalog id. For an audio clip it holds the source file and speed, and since commit `b5d17e0` also the clip's gain, pan, pitch, fades, mute, reverse and warp (mode, clip tempo in beats, markers). Before that a copy played the raw file at speed one instead of what the original played. The paste writes reverse only for a clip that was reversed, and before anything else: writing it at all mirrors the new clip's window into its file, which made trimmed copies quieter until commit `50d87d5` ([Writing a clip's reverse switch moves its window](writing-reverse-moves-a-clips-window.md)). A cut deletes its sources, and an undo between copy and paste can too, so a clipboard of ids would paste nothing ([Hold ids, not pointers](ids-not-pointers.md)).

## In the note editor

`StepGridEditing.cpp` applies the same rule in steps: the marquee's span, the selected notes rounded out to whole steps, or the whole clip after Ctrl+A. One difference: a note paste clears only the pitches and steps it writes, not the whole rectangle.

## Related

- [Clips never overlap](clip-placement.md)
- [Track clipboard (cards)](track-clipboard.md)
- [Pointer and selection rules](pointer-and-selection.md)
- [Note editor (StepGrid)](note-editor.md)
- [Arrangement view](arrangement-view.md)
