---
title: Track groups (bus tracks)
type: component
summary: Ctrl+G gathers tracks under a bus track that their audio feeds; the structure is positional and repaired after every reorder.
tags: [rhino, groups, mixer, arrangement]
sources: []
updated: 2026-10-03
---

# Track groups (bus tracks)

A group is a bus: a track of its own, with fader, pan, mute, solo and an audio-effect chain, that every member's output feeds. The model is `native/src/SessionGroups.cpp`; the fold arrow, indented member cards and multi-card selection are `native/src/ArrangementGroups.cpp`. Why the bus is an ordinary track is [A group is an ordinary bus track](group-is-a-bus-track.md).

## How it works

- **Grouping.** Ctrl+G (`Session::groupTracks`) appends a bus named `Group N`, gives it a `rhinoGroupBus` id one past the highest the document has used (so an id freed by an undo is never reissued), and gathers the members under it. Ctrl+Shift+G (`ungroupTracks`) deletes the bus and hands the members back to the main output; the tracks survive.
- **Positional structure.** A group is its bus followed by the run of tracks whose `rhinoGroup` equals the bus's id; no member list is stored. `reconcileTrackGroups` runs after `addTrack`, `moveTrack`, `removeAudioTrack`, every group command and every reopen: a plain track carried between two members joins, anything cut off from its bus leaves, then routing is rewritten to match (`TrackOutput::setOutputToTrack` for members, the default device for the rest).
- **Collapse** is a view setting on the bus (`rhinoGroupCollapsed`, no undo). Members are laid out at zero height rather than removed, so geometry code needs no special case.
- **No nesting.** Grouping refuses a selection that contains a bus, and a bus is never made a member.
- **A bus refuses** instruments and MIDI effects (`addDevice`), clips (`createClip`, `importAudioAt`), clip slots (`clipSlotAt`) and arming (`trackRecordInput` answers none).

## Pitfalls

- **Ask `usesDefaultAudioOut()`, not `getDestinationTrack() != nullptr`.** Deleting a bus leaves its members naming a track that no longer exists. That reads as "no destination" while the engine still resolves it on every render, and an offline render of the edit never returns ([Offline renders that never return](renders-that-never-return.md)).
- **A bus is not made by `appendTrack`.** `groupTracks` calls `insertNewAudioTrack` itself, so a bus has no `rhinoTrackType` (it reads as audio), no Utility device and no picked colour (it draws in a fixed grey until coloured).
- **Skip buses when looking for a playable track**, as `patternTrackOf`, `refreshUtilityPointers` and `restoreProject` do; a bus at index 0 once reopened with an instrument on it, silencing the project (`GroupBusReload.inc`).
- **Dropping a plain track directly under a bus empties the group** (traced in the code, not reproduced, 2026-10-03). The join rule compares the newcomer's two neighbours' `rhinoGroup`, a bus carries none, and the unjoined newcomer then breaks the run, cutting every member below it loose ([Hazards found while seeding the wiki](known-hazards.md)).
- `migrateLegacyTrackGroups` rebuilds pre-bus documents' groups on load ([No backward compatibility for .rhinoedit](no-rhinoedit-back-compat.md)).

## Related

- [A group is an ordinary bus track](group-is-a-bus-track.md)
- [Mixer](mixer.md)
- [Arrangement view](arrangement-view.md)
- [Track kinds: audio and MIDI](track-kinds.md)
