---
title: Clips never overlap
type: concept
summary: The clip that just arrived wins the ground it lands on, and one function, Session::makeRoomForClip, enforces it for every path.
tags: [rhino, clips, arrangement, editing]
sources: []
updated: 2026-10-05
---

# Clips never overlap

Two clips may never overlap on one track, and the clip that just arrived wins: Live's rule. A clip dropped into the middle of a long one cuts a hole and fills it, and the parts either side survive as clips of their own. Trimming a clip over its neighbour eats the neighbour the same way.

## One place

`Session::makeRoomForClip` (`native/src/SessionRegion.cpp`) is the only implementation. It runs `clearClipRegionInEdit` over the newcomer's span on its own track: split every clip crossing either edge, then delete whatever lies wholly inside. Splitting first is what makes "trim", "cut a hole" and "replace" one case. Touching an edge is not overlapping (a 1e-7 s tolerance).

Every path that places or grows a clip ends there: `editClip` and `moveClips` (drags, trims, nudges), `importAudioAt` and `importAudioFilesAt`, `insertPatternPreset`, `copySlotClipToArrangement`, `mergeClips`, a note paste that lengthens the open clip, every warp change that lengthens one (`SessionWarp.cpp`), and recording through `tidyRecordedClips`. The engine drops a finished take on top of whatever was there, so `tidyRecordedClips` diffs the track's clip ids against those held before the take, by id because making room for one new clip can delete another. `pasteClipRegion` gets the same effect by clearing its whole rectangle first ([Region editing](region-editing.md)).

## Details that matter

- **`createClip` refuses occupied ground** ("There is already a clip here"): a double-click on a clip is a mistake, not an instruction. The exception is a new document's hidden `Pattern 1` placeholder, which `createClip` moves to the click and reveals, so the first track behaves like every other.
- **Multi-clip drags pass `movingWith`**, naming every clip in the gesture; otherwise the leading clip of a run moved right would delete the one behind it. `Session::moveClips` carries the whole selection, drags and arrow nudges alike, in one transaction with one playback restart, and rolls everything back if any clip is refused (commit `b5d17e0`).
- **Files dropped together land end to end** from the drop point, in the order given, as one undo step and all or nothing (`importAudioFilesAt`, commit `d20faa4`). Imported one at a time at the same point, each made room by trimming away the one before, and only the last survived.
- **A hidden placeholder** inside a cleared span is removed whole rather than split into fragments nothing could address, and `repairPatternClip` then re-points the note editor.
- **MIDI clips are cut by position and offset, never by their notes.** A split's right half keeps the whole sequence and plays a window into it, and trimming the start moves the window, so no note moves.
- **A MIDI clip carries its instrument.** Moving or pasting one onto another lane switches that lane's instrument to the source track's, deliberately (see the comment in `pasteClipRegion`). Since commit `b5d17e0` the source is named by catalog id (`carriedInstrument`), bypassed or not, and a clip from a track that plays nothing leaves the target's instrument alone ([One instrument per track](one-instrument-per-track.md)).
- **Warp growth is covered** since `b5d17e0`: `rescaleWarpedClip` lengthens a clip in place (`*2` doubles it), and its callers now make room afterwards. A tempo change does not need to: a Repitch clip keeps its length in beats and the engine keeps the next clip on its beats, which `ClipWarp.inc` pins (commit `6eaa5ba`; [Time warp](time-warp.md)).

## Related

- [Region editing](region-editing.md)
- [Arrangement view](arrangement-view.md)
- [Recording and the count-in](recording.md)
- [Time warp](time-warp.md)
- [One instrument per track](one-instrument-per-track.md)
