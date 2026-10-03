---
title: Track and clip colours
type: concept
summary: Every new track is born with a palette colour; a clip wears its track's colour until C gives it its own rhinoClipColour.
tags: [rhino, colours, tracks, clips]
sources: []
updated: 2026-10-03
---

# Track and clip colours

## Tracks are born coloured

Every path that makes a track calls `Session::pickTrackColour` (`native/src/SessionTracks.cpp`): `appendTrack`, and through it `addTrack`, a clip dragged below the last lane and a paste that runs off the bottom, plus the starter stack in `buildStarterEdit`. A stack is therefore legible the moment it exists rather than after someone has coloured it by hand. The pick is random among the `trackColourPalette` entries no track is already wearing, starting over from the whole palette once all are on screen (43 entries as of 2026-10-03). Under the command-line test modes it takes the first unused entry instead, because scenarios that compare pixels cannot agree with a dice roll; tracks still come out distinct, in the same order every run.

A group bus is the exception: `groupTracks` makes it without a pick, and until someone colours it the arrangement draws it in neutral greys (its card from `palette::control`, its group band from `defaultGroupColour` in `ArrangementGroups.cpp`). The main row takes no colour at all.

Recolouring a track from the card's right-click palette is a decision about the track, so `setTrackColour` makes it an undo step. A row's height is a view setting and is written without one.

## Clips wear their track's colour

A clip has no colour of its own until someone presses **C** on it. `cycleClipColour` then writes `rhinoClipColour`, walks a separate eight-colour clip palette (`nextClipColour` in `SessionInternal.cpp`), and after the last entry goes back to *nothing*, so a clip can always rejoin its lane. `Session::clipColour` is the one place to ask; a transparent answer means "use the track's".

The property is Rhino's own because the engine cannot say "no colour": `te::Clip::getColour` substitutes a default per clip type for anything unset or transparent, so a clip could never be asked whether it had been coloured. That is why every path that made a clip used to stamp a colour on it, and why no timeline path does now. Copy, paste, duplicate, merge and the copies between views carry `rhinoClipColour`, not the engine's colour.

## Drawn darker

`ArrangementPainter.cpp` fills a clip with its own colour, or else its track's colour darkened (`darker(0.5f)`), as a translucent band behind light text. A card is a solid block behind dark text; one value cannot serve both.

## Leftover

Slot clips in the paused session view are still given an engine colour on creation (`presetColour`, `instrumentColour`), and the view draws `getColour()` rather than `Session::clipColour` ([Session view (paused)](session-view.md)).

## Related

- [Track kinds: audio and MIDI](track-kinds.md)
- [Colours and typography](colours-and-typography.md)
- [Arrangement view](arrangement-view.md)
- [Track groups (bus tracks)](track-groups.md)
