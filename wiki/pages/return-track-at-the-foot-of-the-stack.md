---
title: A return is an ordinary track at the foot of the stack
type: decision
summary: A return track is a plain te::AudioTrack in getAudioTracks(), kept with the other returns under every other track and over the main row, and every track carries a send to every return from the moment either exists; taking returns out of the track list and making sends on first use were rejected, and since 2026-10-25 only the arrangement draws the returns pinned beside the main row.
tags: [rhino, mixer, returns, decision]
sources: []
updated: 2026-10-25
---

# A return is an ordinary track at the foot of the stack

## Context

Return tracks (commit `b7bd73c`, 2026-10-25; [Return tracks and sends](return-tracks.md)) needed a place in the model and a route from each track to each return. Tracktion supplies both halves: `te::AuxSendPlugin` copies a chain's audio onto a numbered bus, and `te::AuxReturnPlugin` adds a bus into a chain.

## Decision

- **A return is an ordinary `te::AudioTrack`** with an aux return at the head of its chain, just as a group's bus is a track ([A group is an ordinary bus track](group-is-a-bus-track.md)). The mixer, the device rack, automation, renaming, colour, Delete and the card all reach it with no second code path. What it refuses (`carriesOtherTracks`) is what makes it a return.
- **Returns stay together at the foot of the stack**, as Live keeps them. Every path that makes or moves a track holds that line, and `reconcileTrackGroups` repairs any break. Since `5632644` the arrangement draws them pinned over the main row (below).
- **Every other track always carries one send per return**, made at silence by `ensureReturnRouting`.

## Alternatives rejected

- **Returns taken out of the track list**, kept beside the main row the way the main row itself is kept. Every per-track feature and every `te::getAudioTracks` index would have needed a second code path. The main row avoids one only because it is addressed as one index past the end ([The main track](main-track.md)). Drawing the returns pinned beside the main row was not rejected, and is what the arrangement does now (below).
- **Sends made on first use**, when a knob is first turned. Adding a plugin rebuilds the playback graph, so the first drag of every knob would have started with a graph rebuild.

## Consequences

- `trackCount() - 1` is no longer the newest playing track: ask `firstReturnTrack()`. About 40 scenarios assumed it, or assumed four tracks in a new document, and now use `firstReturnTrack() - 1` and `firstReturnTrack() == 4` ([Writing Rhino tests](writing-rhino-tests.md)).
- A return's row and the space under it are past the last lane. A clip arriving there gets a lane made above the returns.
- A send at silence stays in the graph, one per return on every track. What that costs has not been measured.
- Mute and solo needed an engine override and graph rebuilds ([A send ignores mute while muted tracks are processed](send-ignores-mute-when-muted-tracks-process.md)).

## Change, 2026-10-25: drawn pinned

At the user's request, commit `5632644` made the arrangement draw the returns in a block pinned between the scrolling lanes and the main row. Main always comes last, the returns can be reordered among themselves, a new return is stacked at the bottom of the block, and the block opens with a heavier rule. Only the drawing changed. The returns are still ordinary tracks at the foot of `getAudioTracks()`, and every track-indexed path reaches them as before. The cost fell on the view: a second header container, two-pass painting and a second set of row rules ([Return tracks and sends](return-tracks.md#in-the-arrangement)).

## Related

- [Return tracks and sends](return-tracks.md)
- [A group is an ordinary bus track](group-is-a-bus-track.md)
- [Mixer](mixer.md)
- [Session, the model](session-model.md)
