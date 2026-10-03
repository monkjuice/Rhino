---
title: Clips never overlap
type: concept
summary: The clip that just arrived wins the ground it lands on, and one function, Session::makeRoomForClip, enforces it for every path.
tags: [rhino, clips, arrangement, editing]
sources: []
updated: 2026-10-03
---

# Clips never overlap

Two clips may never overlap on one track, and the clip that just arrived wins: Live's rule. A clip dropped into the middle of a long one cuts a hole and fills it, and the parts either side survive as clips of their own. Trimming a clip over its neighbour eats the neighbour the same way.

## One place

`Session::makeRoomForClip` (`native/src/SessionRegion.cpp`) is the only implementation. It runs `clearClipRegionInEdit` over the newcomer's span on its own track: split every clip crossing either edge, then delete whatever lies wholly inside. Splitting first is what makes "trim", "cut a hole" and "replace" one case. Touching an edge is not overlapping (a 1e-7 s tolerance).

Every path that places a clip ends there: `editClip` (drags, trims, nudges), `importAudioAt`, `insertPatternPreset`, `copySlotClipToArrangement`, `mergeClips`, and recording through `tidyRecordedClips`. The engine drops a finished take on top of whatever was there, so `tidyRecordedClips` diffs the track's clip ids against those held before the take, by id because making room for one new clip can delete another. `pasteClipRegion` gets the same effect by clearing its whole rectangle first ([Region editing](region-editing.md)).

## Details that matter

- **`createClip` refuses occupied ground** ("There is already a clip here"): a double-click on a clip is a mistake, not an instruction. The exception is a new document's hidden `Pattern 1` placeholder, which `createClip` moves to the click and reveals, so the first track behaves like every other.
- **Multi-clip drags pass `movingWith`**, naming every clip in the gesture; otherwise the leading clip of a run moved right would delete the one behind it.
- **A hidden placeholder** inside a cleared span is removed whole rather than split into fragments nothing could address, and `repairPatternClip` then re-points the note editor.
- **MIDI clips are cut by position and offset, never by their notes.** A split's right half keeps the whole sequence and plays a window into it, and trimming the start moves the window, so no note moves.
- **A MIDI clip carries its instrument.** Moving or pasting one onto another lane switches that lane's instrument to the source track's, deliberately (see the comment in `pasteClipRegion`). The source is read by `activeTrackInstrument`, which answers 4OSC for a track with no instrument, so a clip from an empty MIDI track puts 4OSC on the target in place of whatever it ran.

## Not covered

`rescaleWarpedClip` resizes a warped clip in place when warp is switched on or its clip tempo changes (`*2` doubles its length, as `ClipWarp.inc` asserts) without calling `makeRoomForClip`, so a clip that grows can run over the next one (traced in the code, 2026-10-03; [Time warp](time-warp.md),
[Hazards found while seeding the wiki](known-hazards.md)).

## Related

- [Region editing](region-editing.md)
- [Arrangement view](arrangement-view.md)
- [Recording and the count-in](recording.md)
- [Time warp](time-warp.md)
- [One instrument per track](one-instrument-per-track.md)
