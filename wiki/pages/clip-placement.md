---
title: Clips never overlap
type: concept
summary: The clip that just arrived wins the ground it lands on, and one function, Session::makeRoomForClip, enforces it for every path.
tags: [rhino, clips, arrangement, editing, tracktion]
sources: []
updated: 2026-10-23
---

# Clips never overlap

Two clips may never overlap on one track, and the clip that just arrived wins: Live's rule. A clip dropped into the middle of a long one cuts a hole and fills it, and the parts either side survive as clips of their own. Trimming a clip over its neighbour eats the neighbour the same way.

## One place

`Session::makeRoomForClip` (`native/src/model/SessionRegion.cpp`) is the only implementation. It runs `clearClipRegionInEdit` over the newcomer's span on its own track: split every clip crossing either edge, then delete whatever lies wholly inside. Splitting first is what makes "trim", "cut a hole" and "replace" one case. Touching an edge is not overlapping (a 1e-7 s tolerance).

**No sliver is left.** A clip is cut at an edge only when the part left outside is at least `minimumRegionPieceSeconds(span)` (`native/src/model/ClipGeometry.h`): `minimumSplitSeconds`, the 10 ms a split by hand keeps (commit `972cc0e`), or a tenth of the span when the span is under 0.1 s (commit `cf4e959`). One that crosses by less is left whole, still overlaps, and goes with everything else inside; before the floor, a clip overlapping its neighbour by a few milliseconds left a stub drawn as a sliver nobody could grab. A copy keeps the same floor ([Region editing](region-editing.md)). Only regions scale it: `canSplitClipAt` (a split by hand, Ctrl+E) and [Bounce](bounce-to-new-track.md#a-time-selection) keep a fixed 10 ms.

**Tracktion will not cut within 1 ms of a clip's end** (`split` in `model/clips/tracktion_ClipOwner.cpp` at `4536d8a` wants `time.reduced(0.001 s).contains(t)`), and a refused cut leaves the clip whole and overlapping, so the pass after it deletes all of it, the part outside included. Until `cf4e959`, deleting one deepest-zoom cell (about 0.12 ms) inside a clip therefore lost everything from the cell to the clip's end, the second cut falling within 1 ms of the first; paste and `makeRoomForClip` did the same around tiny clips. `clearClipRegionInEdit` now cuts through `splitClipAt` (anonymous namespace, `SessionRegion.cpp`): the engine's `splitClip` where it will cut, and nearer an end the same cut made by hand. The clip's state copied under new ids (`te::EditItemID::remapIDs`, `insertClipWithState`) takes the right part by `setStart(at, true, false)`, the original keeps the left by `setEnd(at, true)`, and the fades at the cut are cleared. It skips the engine's re-wrap of a looped clip's offset, as arrangement clips never loop; Tracktion's own `deleteRegionOfClip` cuts a range out the same way, one split and a `setStart`. Call the engine's `splitClip` directly only well clear of both ends, as Ctrl+E and Bounce do. `native/src/tests/Arrangement/scenarios/ClipSlivers.inc` pins all of it with a generated WAV one sample longer than a beat: no stubs, no one-sample copy, and one deepest-zoom cell copied, cut and pasted to within 1e-7 s, half a millisecond into a clip included.

Every path that places or grows a clip ends there: `editClip` and `moveClips` (drags, trims, nudges), `importAudioAt` and `importAudioFilesAt`, `insertPatternPreset`, `copySlotClipToArrangement`, `mergeClips`, a note paste that lengthens the open clip, every warp change that lengthens one (`SessionWarp.cpp`), and recording through `tidyRecordedClips`. The engine drops a finished take on top of whatever was there, so `tidyRecordedClips` diffs the track's clip ids against those held before the take, by id because making room for one new clip can delete another. `pasteClipRegion` gets the same effect by clearing its whole rectangle first ([Region editing](region-editing.md)).

## Details that matter

- **`createClip` refuses taken ground and stops short of a clip ahead.** A clip at or over the start refuses ("There is already a clip here."): a double-click on a clip is a mistake, not an instruction. A visible clip starting inside the new one's span ends the new one there instead (commit `3a0933c`), so a double-click just before a clip makes the clip that fits. The length asked for (`lengthBeats`, the arrangement's grid cell; [Arrangement view](arrangement-view.md)) is never less than a bar. The exception is a new document's hidden `Pattern 1` placeholder, which `createClip` moves to the click and reveals, so the first track behaves like every other.
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
