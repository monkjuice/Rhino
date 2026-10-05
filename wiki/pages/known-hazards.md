---
title: Hazards found while seeding the wiki
type: analysis
summary: Probable bugs traced by reading the code on 2026-10-03; every Rhino item was fixed by 2026-10-05, and the four Forge items are still open.
tags: [both, bugs, maintenance]
sources: []
updated: 2026-10-05
---

# Hazards found while seeding the wiki

Each item below was **traced in the code on 2026-10-03 while seeding this wiki**, as a lead to confirm rather than a known bug. A fixed item stays listed with the commit that fixed it, and the pages that mentioned it have been updated. Status checked against the code on 2026-10-05.

## Rhino

1. **Fixed in `8dd3627`.** Lanes made by a drag or a paste had no fader until the next `addTrack`. Both paths now run `ensureTrackMixers`, `ensureSceneSlots` and `reconcileTrackGroups` at once ([Mixer](mixer.md)).
2. **Fixed in `a04b407`.** A reordered stack could save a project that would not reopen, because `restoreProject` demanded a MIDI clip and a Utility on the first non-bus track ([No track is special for being first](pattern-track.md)).
3. **Fixed in `a04b407`.** Reopening could change the first track's instrument through `rhinoPatternInstrument`, which is gone.
4. **Fixed in `8dd3627`.** A plain track dropped directly under a group bus emptied the group. The join rule now treats the bus above as naming its group ([Track groups (bus tracks)](track-groups.md)).
5. **Fixed in `b5d17e0`.** Warping could lengthen a clip over its neighbour. Every warp change that can grow a clip now runs `makeRoomForClip` ([Clips never overlap](clip-placement.md)). A related review claim, that lowering the tempo makes a Repitch clip overlap the next, did not reproduce and is pinned by a test in `6eaa5ba`.
6. **Fixed in `b5d17e0`.** Copying a MIDI clip from a track with no instrument installed a 4OSC. A clip now carries its track's instrument by catalog id, and nothing from a track that plays nothing ([One instrument per track](one-instrument-per-track.md)).
7. **Fixed in `23de20c`.** A MIDI effect added before any instrument ended up behind the instrument added later ([Device chain order](device-chain-order.md)).
8. **Fixed in `23de20c`.** A dropped Utility was invisible: Utility is no longer offered in the browser ([Built-in devices](built-in-devices.md)).
9. **Fixed, but not by `492cc8b`.** Rhino Arp built a `te::MidiMessageArray` on every `applyToBuffer` call. The redesigned arp (wiki log, 2026-10-03 and 10-04) reuses two arrays reserved in `initialise()` ([Real-time audio rules](real-time-audio-rules.md)).
10. **Fixed in `a04b407`.** The dragged loop span outlived its document: `restoreProject` now drops it, as `newProject` did ([Transport, tempo and loop](transport.md)).
11. **Fixed in `23de20c`.** The clip-slot paths skipped the track-kind refusals. `insertDeviceClipInSlot`, `insertAudioFileInSlot` and `copySlotClipToArrangement` now ask `clipLaneRefusal` ([Session view (paused)](session-view.md)).
12. **Fixed in `23de20c`.** The built-in sample cache could append to a stub. `createSample` now writes a temporary file and moves it into place ([juce::File::createOutputStream appends](juce-output-stream-appends.md)).

## Forge

All four are still open as of 2026-10-05.

13. **Slot 1's default destination is wrong.** It is the literal `13` in `src/ForgeParameters.cpp`, commented as the cutoff; since format 3, 13 is C PAN and the cutoff is `cutoffDestination`. Silent until its depth is raised.
14. **Forge state in older Rhino projects is misrouted.** Host state carries no version, so a project saved before 2026-09-28 reopens with modulation destinations from index 11 up naming other controls ([Stored indices are append-only](append-only-stored-indices.md)).
15. **The knob-size layout check cannot fail.** `uniformKnobDiameter` never returns less than 40, and the test's threshold is 40 ([One knob diameter for the whole Forge panel](forge-knob-diameter-is-panel-wide.md)).
16. **Warp does nothing on a spectral oscillator**, and a warp stage reading a spectral oscillator as its source probably reads a constant ([Forge spectral oscillator](forge-spectral.md)).

## Related

- [Writing Rhino tests](writing-rhino-tests.md)
- [Session, the model](session-model.md)
