---
title: Track groups (bus tracks)
type: component
summary: Ctrl+G gathers tracks under a bus track that their audio feeds; the structure is positional and repaired after every reorder.
tags: [rhino, groups, mixer, arrangement]
sources: []
updated: 2026-10-16
---

# Track groups (bus tracks)

A group is a bus: a track of its own, with fader, pan, mute, solo and an audio-effect chain, that every member's output feeds. The model is `native/src/model/SessionGroups.cpp`; the fold arrow, indented member cards and multi-card selection are `native/src/ui/arrangement/ArrangementGroups.cpp`. Why the bus is an ordinary track is [A group is an ordinary bus track](group-is-a-bus-track.md).

## How it works

- **Grouping.** Ctrl+G (`Session::groupTracks`) appends a bus named `Group N`, gives it a `rhinoGroupBus` id one past the highest the document has used (so an id freed by an undo is never reissued), and gathers the members under it. Ctrl+Shift+G (`ungroupTracks`) deletes the bus and hands the members back to the main output; the tracks survive.
- **Positional structure.** A group is its bus followed by the run of tracks whose `rhinoGroup` equals the bus's id; no member list is stored. `reconcileTrackGroups` runs after `addTrack`, `moveTrack`, `removeAudioTrack`, every track paste, duplicate and delete, every group command, every lane a drag or paste makes, and every reopen: a plain track carried between two members, or directly under the bus, joins; anything cut off from its bus leaves; then routing is rewritten to match (`TrackOutput::setOutputToTrack` for members, the default device for the rest).
- **A track added under a group** (`addTrack` with `below`, from the `+` menu or Ctrl+T under the selected card, 2026-10-14). Under an open group's bus or any of its members it joins that group, and the join is written explicitly (`trackGroupMemberID`) before reconciling: under the *last* member is not between two members, so reconciling alone would leave it outside. Under any part of a collapsed group (the bus, or a hidden member still selected) it lands after the whole group, outside it, since inside a folded group it would be made at zero height and never seen. `TrackManagement.inc` covers both.
- **Bouncing** ([Bounce to New Track](bounce-to-new-track.md), Ctrl+B, 2026-10-14). A bus card bounces the group after its bus, fader included; the new track lands under the whole group, outside it, at unity, and the bus is muted. A member bounced alone is rendered before its own fader and without the bus ([A track that feeds a bus renders nothing on its own](bus-member-renders-nothing-alone.md)); its new track joins the group by an explicit `trackGroupMemberID`, as a track added under the last member does, so it plays into the bus as its source did.
- **Cards on the clipboard** ([Track clipboard](track-clipboard.md), 2026-10-16). A bus stands for its group in cut, copy, paste, duplicate and delete. A pasted or duplicated bus is a new group with its own `rhinoGroupBus` id, its copied members following it; plain copies landing in an open group join it; a clipboard carrying a group lands below the whole target group. **Delete on a bus card deletes the whole group** (`Session::removeTracks`), as in Live; until 2026-10-16 it called `removeAudioTrack`, which dropped the bus and kept the members. Ungroup is now the only way to drop just the bus.
- **Undo restores the group whole** (commit `8dd3627`). Membership is written through the undo manager, and every caller reconciles inside the transaction of the move, group or delete that decided it, so one Ctrl+Z puts back position, membership and routing together. Ungrouping and deleting a bus let the members go and route them to the main output while the bus still exists (`dissolveGroupInEdit`). Undo replays a transaction backwards, so it brings the bus back before routing anything into it; routed after the deletion, the members lost their routing for good. `Arrangement/scenarios/GroupUndo.inc` covers each case.
- **Collapse** is a view setting on the bus (`rhinoGroupCollapsed`, no undo). Members are laid out at zero height rather than removed, so geometry code needs no special case. The bus's fold arrow stands before its name, the arrow every card now carries (commit `14c9c77`, 2026-10-15); it used to sit in the left gutter of the bus card alone, and `paintGroupGutter` now paints only the members' bus-colour band. On a bus the arrow folds the members and never the bus's own row; on a member it folds that row alone ([Arrangement view](arrangement-view.md)).
- **No nesting.** Grouping refuses a selection that contains a bus, and a bus is never made a member.
- **A bus refuses** instruments and MIDI effects (`addDevice`), clips (`createClip`, `importAudioAt`, and since `b5d17e0` moved and pasted ones), clip slots (`clipSlotAt`, `clipLaneRefusal`), arming (`trackRecordInput` answers none) and, since group decks went on 2026-10-12, a DJ deck (`loadDjDeckTrack`).

## Pitfalls

- **Ask `usesDefaultAudioOut()`, not `getDestinationTrack() != nullptr`.** Deleting a bus leaves its members naming a track that no longer exists. That reads as "no destination" while the engine still resolves it on every render, and an offline render of the edit never returns ([Offline renders that never return](renders-that-never-return.md)).
- **A bus is not made by `appendTrack`.** `groupTracks` calls `insertNewAudioTrack` itself, so a bus has no `rhinoTrackType` (it reads as audio), no Utility device and no picked colour (it draws in a fixed grey until coloured).
- **Skip buses when looking for a playable track**, as the note editor's fallback `firstMidiTrackOf` does; a bus at index 0 once reopened with an instrument on it, silencing the project (`GroupBusReload.inc`).
- **A bus carries no `rhinoGroup` of its own**, so a rule that compares neighbours' membership must treat a bus above as naming its group. The join rule did not, and until `8dd3627` a track dropped directly under a bus broke the run and cut every member below it loose.
- **A member's output names its bus by position, not id.** `TrackOutput` records the destination by the bus's place in the audio-track list, so an edit built from a copy of the state with tracks removed resolves it to the wrong track or none: the DJ bounce's stripped copy routed a member straight to the output, past its bus's fader (2026-10-10). Anything that loads a reduced copy must call `setOutputToTrack` again for every member, as `bounceDjDeck` did by the bus's id until the bounce went on 2026-10-12 ([A deck's bounce renders on a worker against a copy of the document](dj-bounce-on-a-copy.md)); nothing builds such a copy now.
- **A member is rendered only as part of its bus.** An offline render naming a member without its bus renders nothing ([gotcha](bus-member-renders-nothing-alone.md)).
- `migrateLegacyTrackGroups` rebuilds pre-bus documents' groups on load ([No backward compatibility for .rhinoedit](no-rhinoedit-back-compat.md)).

## Related

- [A group is an ordinary bus track](group-is-a-bus-track.md)
- [Mixer](mixer.md)
- [Arrangement view](arrangement-view.md)
- [Track kinds: audio and MIDI](track-kinds.md)
- [Bounce to New Track](bounce-to-new-track.md)
- [Track clipboard (cards)](track-clipboard.md)
