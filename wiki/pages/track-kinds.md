---
title: "Track kinds: audio and MIDI"
type: concept
summary: Every track is audio or MIDI from creation; one property records it, one call reads it, and the model refuses mismatched content.
tags: [rhino, tracks, model]
sources: []
updated: 2026-10-03
---

# Track kinds: audio and MIDI

A Rhino track is either audio or MIDI (`Session::TrackType`), declared when it is made, for the reasons in [A track's kind is fixed when it is made](track-kind-fixed-at-creation.md).

## One property, one reader

A MIDI track carries `rhinoTrackType="midi"` on its state; an audio track carries nothing. Absent means audio, which is what every older document says, so nothing was migrated. `Session::trackType(track)` reads that property and nothing else, never the device chain.

## Where tracks come from

- `Session::appendTrack(TrackType)` names the track `MIDI` or `Audio`, colours it with `pickTrackColour`, writes the kind and inserts a Utility device; `addTrack` adds the undo step, group reconciling, scene slots and fader. Names carry no number: the card prints the one-based position beside the name (`01  MIDI`).
- The `+` button (`Arrangement::showAddTrackMenu`) asks which kind; Ctrl+T (`Main.cpp`) repeats the last menu choice. That choice is `lastAddedTrackType` in the machine's settings file, `%APPDATA%\Rhino\Rhino.settings`: MIDI by default, always MIDI under the test flags, and recorded only by the menu.
- Lanes made by a clip dragged below the last lane, a paste running off the bottom, or a browser item or file dropped past the last lane take the kind of what arrives.
- `Session::buildStarterEdit`, used at startup and by *File > New project*, makes `MIDI`, `Audio`, `MIDI`, `Audio` plus the pinned main row, repeating `appendTrack`'s steps inline. Track 0 also holds `Pattern 1`, a hidden placeholder clip.
- A group bus is made directly by `groupTracks`, with no kind (so it reads as audio) and no Utility ([Track groups (bus tracks)](track-groups.md)).

## What reads the kind

- Notes: `createClip` and `preparePresetTrack` want a MIDI track; `editClip` and `pasteClipRegion` refuse a clip whose kind does not match the lane.
- Devices: `addInstrumentDevice`, `addMidiEffectDevice` and `addDrumKit` refuse an audio track. Audio effects, and a channel-strip facility (an `infrastructure` catalog entry such as Utility), go on either kind.
- Audio: `importAudioAt` refuses a MIDI track. `importAudio` with no target fills the first empty audio lane and adds a track only when none is free.
- `trackRecordInput` records MIDI or audio by kind, and nothing for a bus or the main row.
- `ArrangementPainter.cpp`'s empty-lane hint names what the lane accepts.

## Stale docs

`README.md` ("Dropping one also makes the track a MIDI track for good") and `native/README.md` (`trackType` answers `midi` for any track with an instrument; the fifth track is `MIDI 5`; a drop below the last lane calls `addAudioTrack`, now true only for files) describe earlier stages, and `HANDOVER.md`'s one-track starter is gone (checked 2026-10-03).

## Related

- [A track's kind is fixed when it is made](track-kind-fixed-at-creation.md)
- [Track and clip colours](track-and-clip-colours.md)
- [Clips never overlap](clip-placement.md)
- [Recording and the count-in](recording.md)
- [One instrument per track](one-instrument-per-track.md)
- [The first track is still the pattern track](pattern-track.md)
