---
title: A group is an ordinary bus track
type: decision
summary: A group's bus is a plain te::AudioTrack kept in getAudioTracks(), so every track-indexed path reaches it with no second code path; since 2026-10-27 a bus can be a member of another group, up to ten levels, still with no stored tree.
tags: [rhino, groups, mixer, decision, nesting]
sources: []
updated: 2026-10-27
---

# A group is an ordinary bus track

## Context

Track groups first landed on 2026-09-19 (commit `e50c249`) as **organisational** groups: a `rhinoTrackGroup` node on the edit held each group's name, colour and collapsed flag, every member carried a membership property, and a band component of its own provided group mute, solo, rename and a group focus. Audio still went to the main output track by track, so a group was a way of drawing the stack, not a mixing point. It was replaced the same day (commit `207b5b3`).

## Decision

A group is a **bus**, and the bus is an ordinary `te::AudioTrack` that keeps its place in `getAudioTracks()`. Members' outputs are routed into it with `TrackOutput::setOutputToTrack`, which the engine allows in either direction in the track order, so the bus can sit above its members as it does in Live.

Because the bus is a track, the mixer, the device rack, automation, renaming, the colour palette, Delete and the arrangement's card drawing all reach it with no second code path, and it carries the group's name, colour, fader, mute and solo simply by being one. The band component and its group-only paths were deleted.

What a bus refuses is what makes it a bus: audio effects only (`addDevice`), no clips (`createClip`, `importAudioAt`), no clip slots (`clipSlotAt`), nothing to record (`trackRecordInput`). A sum has nothing for an instrument to play and nowhere to put a clip.

The structure is **positional**: a bus followed by the run of tracks whose `rhinoGroup` matches its `rhinoGroupBus`, a nested group's run included. With no stored member list, nothing can disagree with the screen, and `reconcileTrackGroups` makes the routing follow the order, so carrying a track into a group joins it and carrying it out leaves it, with the audio following both.

## Alternatives rejected

- **A group object beside the tracks** (the organisational model): a second set of mute, solo and rename paths, and still no signal path.
- **A stored member list**, which every reorder, deletion and undo would have to keep in step.

## Consequences

- Anything looking for "the first playable track" must skip buses, as the note editor's fallback `firstMidiTrackOf` does. Before the load path skipped them, a project whose stack opened with a group reopened with an instrument on its bus, and silent (`GroupBusReload.inc`); the first-track rules involved are gone since `a04b407` ([No track is special for being first](pattern-track.md)).
- Group structure lives in two places, a membership property per track and each track's output routing, so both are written inside the undoable transaction that decided them ([Track groups (bus tracks)](track-groups.md)).
- Deleting a bus leaves members naming a missing track, and asking `getDestinationTrack()` instead of `usesDefaultAudioOut()` made an offline render never return ([Offline renders that never return](renders-that-never-return.md)).
- The routing is positional in the engine too: a member's output names its bus by its place in the track list, as text resolved only when it changes, so after a reorder the text and the routing can disagree ([Tracktion resolves a track-to-track output only when its text changes](tracktion-track-output-resolves-on-change.md)), and an edit loaded from a copy of the state with tracks removed (the DJ bounce's, until it went on 2026-10-12) must set every member's output again by id ([A deck's bounce renders on a worker against a copy of the document](dj-bounce-on-a-copy.md)).
- **Groups nest, reversed on 2026-10-27** (commit `7965c72`). Until then groups did not nest and a routing tree was deliberately not built. At the user's request a bus may now be a member of another group, up to ten levels ([Track groups](track-groups.md#nesting)). It stayed cheap because nothing about the decision changed: a nested bus is still an ordinary track, carrying its parent's `rhinoGroup` beside its own `rhinoGroupBus` and feeding the parent bus like any member, so the tree is still read from positions and claims, never stored. The rules that grew (claims, levels, block moves) live in the pure header `native/src/model/GroupTree.h`, unit-tested without a `Session`.
- Return tracks (commit `b7bd73c`, 2026-10-25) follow the same pattern, and a return refuses what a bus refuses through one shared check, `carriesOtherTracks` ([A return is an ordinary track at the foot of the stack](return-track-at-the-foot-of-the-stack.md)).
- Old documents' `rhinoTrackGroup` nodes are rebuilt on load by `migrateLegacyTrackGroups`, an accommodation [No backward compatibility for .rhinoedit](no-rhinoedit-back-compat.md) would remove.

## Related

- [Track groups (bus tracks)](track-groups.md)
- [Mixer](mixer.md)
- [Session, the model](session-model.md)
- [Track kinds: audio and MIDI](track-kinds.md)
- [A return is an ordinary track at the foot of the stack](return-track-at-the-foot-of-the-stack.md)
