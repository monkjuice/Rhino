---
title: Hazards found while seeding the wiki
type: analysis
summary: Probable bugs traced by reading the code on 2026-10-03, none of them reproduced yet, with where each one lives and what it breaks.
tags: [both, bugs, maintenance]
sources: []
updated: 2026-10-03
---

# Hazards found while seeding the wiki

Each item below was **traced in the code on 2026-10-03 while seeding this wiki, and not reproduced**: a lead to confirm
with a test, not a known bug. When one is fixed or disproved, delete it here and on the page that mentions it.

## Rhino

1. **Lanes made by a drag or a paste have no fader.** `appendTrack` skips the engine's default plugins, and neither
   `editClip` nor `pasteClipRegion` then calls `ensureTrackMixers`, so `setTrackVolumeDb` answers "That track has no
   fader." until the next `addTrack`, grouping or reopen ([Mixer](mixer.md)).
2. **A reordered stack may save a project that will not reopen.** `restoreProject` refuses a document whose first
   non-bus track lacks a MIDI clip and a Utility, and nothing keeps them there when an audio track is dragged to the
   top ([The first track is still the pattern track](pattern-track.md)).
3. **Reopening can change the first track's instrument.** `restoreProject` re-applies `rhinoPatternInstrument` to
   whichever track is first, and knows only `wave`, `forge` and `drums`, with 4OSC for anything else. A reordered stack,
   or a future catalog instrument, comes back with a different instrument ([The first track is still the pattern track](pattern-track.md)).
4. **Dropping a plain track directly under a group bus empties the group.** The join rule in `reconcileTrackGroups`
   compares the newcomer's two neighbours, a bus carries no `rhinoGroup`, and the second pass then cuts every member
   below loose ([Track groups (bus tracks)](track-groups.md)).
5. **Warping can overlap clips.** `rescaleWarpedClip` lengthens a clip in place (`*2` doubles it) without
   `makeRoomForClip` ([Clips never overlap](clip-placement.md)).
6. **Copying a MIDI clip from an empty track installs a 4OSC.** A MIDI clip carries its track's instrument on purpose
   ([Clips never overlap](clip-placement.md)), but a track with none reads as 4OSC (`activeTrackInstrument`), so
   pasting or duplicating its clip replaces whatever the destination ran with a 4OSC.
7. **A MIDI effect added before any instrument ends up behind it.** `addMidiEffectDevice` appends when no instrument is
   present; an instrument added later goes in at the front, so an arpeggiator sits after the synth
   ([Device chain order](device-chain-order.md)).
8. **A dropped Utility is invisible.** The browser offers Utility, but `deviceSlots` skips that type as infrastructure.
9. **Rhino Arp allocates on the audio thread.** `RhinoArpDevice::applyToBuffer` reserves a `te::MidiMessageArray` on
   every call ([Real-time audio rules](real-time-audio-rules.md)).
10. **The dragged loop span outlives its document.** It is not saved, and `restoreProject` does not clear it
    ([Transport, tempo and loop](transport.md)).
11. **The clip-slot paths skip the track-kind refusals** (`insertDeviceClipInSlot`, `insertAudioFileInSlot`,
    `copySlotClipToArrangement`). Latent while the session view is off ([Session view (paused)](session-view.md)).
12. **The built-in sample cache can append.** `createSample` writes through `createOutputStream` without deleting a
    stub of 44 bytes or fewer ([juce::File::createOutputStream appends](juce-output-stream-appends.md)).

## Forge

13. **Slot 1's default destination is wrong.** It is the literal `13` in `src/ForgeParameters.cpp`, commented as the
    cutoff; since format 3, 13 is C PAN and the cutoff is `cutoffDestination`. Silent until its depth is raised.
14. **Forge state in older Rhino projects is misrouted.** Host state carries no version, so a project saved before
    2026-09-28 reopens with modulation destinations from index 11 up naming other controls
    ([Stored indices are append-only](append-only-stored-indices.md)).
15. **The knob-size layout check cannot fail.** `uniformKnobDiameter` never returns less than 40, and the test's
    threshold is 40 ([One knob diameter for the whole Forge panel](forge-knob-diameter-is-panel-wide.md)).
16. **Warp does nothing on a spectral oscillator**, and a warp stage reading a spectral oscillator as its source
    probably reads a constant ([Forge spectral oscillator](forge-spectral.md)).

## Related

- [Where the written docs disagree with the code](doc-drift.md)
- [Where Forge's docs disagree with its code](forge-doc-drift.md)
- [Writing Rhino tests](writing-rhino-tests.md)
- [Session, the model](session-model.md)
