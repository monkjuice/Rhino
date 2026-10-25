---
title: "Track kinds: audio and MIDI"
type: concept
summary: Every track is audio or MIDI from creation; one property records it, one call reads it, and the model refuses mismatched content.
tags: [rhino, tracks, model]
sources: []
updated: 2026-10-24
---

# Track kinds: audio and MIDI

A Rhino track is either audio or MIDI (`Session::TrackType`), declared when it is made, for the reasons in [A track's kind is fixed when it is made](track-kind-fixed-at-creation.md).

## One property, one reader

A MIDI track carries `rhinoTrackType="midi"` on its state; an audio track carries nothing. Absent means audio, which is what every older document says, so nothing was migrated. `Session::trackType(track)` reads that property and nothing else, never the device chain.

## Where tracks come from

- `Session::appendTrack(TrackType, int after = -1)` names the track `MIDI` or `Audio`, colours it with `pickTrackColour`, writes the kind and inserts a Utility device, placing it directly after track `after` or, out of range, at the foot. `addTrack(TrackType, int below = -1, int* added = nullptr)` adds the undo step, group placement and reconciling, scene slots and fader, and reports the new index through `added`; `below` out of range (-1 or the main row's index) means the foot. Names carry no number: the card prints the one-based position beside the name (`01  MIDI`).
- The `+` button (`Arrangement::showAddTrackMenu`) asks which kind; Ctrl+T (`app/MainComponentCommands.cpp`) repeats the last menu choice. That choice is `lastAddedTrackType` in the machine's settings file, `%APPDATA%\Rhino\Rhino.settings`: MIDI by default, always MIDI under the test flags, and recorded only by the menu. Both go through `Arrangement::insertTrack`, which passes `selectedTrack` as `below` (since 2026-10-14): the track lands directly under the selected card and becomes the selection, so repeated adds stack in the order made, as in Live. The arrangement always has a selected row (0 at start), so only the main row selected sends a track to the foot. Placement in a group is in [Track groups (bus tracks)](track-groups.md).
- A track added at the foot that joins no group marks `ModelChange::none`; one landing mid-stack or joining a group marks `ModelChange::unknown`, as `moveTrack` does, since every lane under it moves.
- Lanes made by a clip dragged below the last lane, a paste running off the bottom, or a browser item or file dropped past the last lane take the kind of what arrives, and are appended at the foot, as is `addAudioTrack()`.
- `Session::buildStarterEdit`, used at startup and by *File > New project*, makes `MIDI`, `Audio`, `MIDI`, `Audio` plus the pinned main row, repeating `appendTrack`'s steps inline. The first MIDI track also holds `Pattern 1`, a hidden placeholder clip for the note editor.
- A group bus is made directly by `groupTracks`, with no kind (so it reads as audio) and no Utility ([Track groups (bus tracks)](track-groups.md)).

## What reads the kind

- Notes: `createClip` and `preparePresetTrack` want a MIDI track; `editClip`, `moveClips` and `pasteClipRegion` refuse a clip whose kind does not match the lane, and so do the clip-slot paths behind a DJ console's cells (`clipLaneRefusal`).
- Devices: `addInstrumentDevice`, `addMidiEffectDevice`, `addDrumKit` and `addDrumSound` refuse an audio track. Audio effects, and a channel-strip facility (an `infrastructure` catalog entry such as Utility), go on either kind.
- Audio: `importAudioAt` refuses a MIDI track. `importAudio` with no target fills the first empty audio lane and adds a track only when none is free. A sample dropped on a MIDI track running a [Drum Rack](drum-rack.md) is not refused: the arrangement checks `trackHasDrumRack` first and hands the file to `addDrumSound`, which puts it on the next empty pad rather than making a clip.
- `trackRecordInput` records MIDI or audio by kind, and nothing for a bus or the main row.

## Related

- [A track's kind is fixed when it is made](track-kind-fixed-at-creation.md)
- [Track and clip colours](track-and-clip-colours.md)
- [Clips never overlap](clip-placement.md)
- [Recording and the count-in](recording.md)
- [One instrument per track](one-instrument-per-track.md)
- [No track is special for being first](pattern-track.md)
