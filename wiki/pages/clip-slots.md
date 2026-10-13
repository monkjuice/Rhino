---
title: Clip slots, the DJ consoles' cells
type: component
summary: Each track's clip slots, one per scene, are the cells a DJ console's clips grid shows; what the model guarantees about them, and the clip launcher that first showed them, removed on 2026-10-11.
tags: [rhino, clips, dj, slots]
sources: []
updated: 2026-10-12
---

# Clip slots, the DJ consoles' cells

Every track carries a clip slot per scene, and a slot holds at most one clip. Nothing launches them: they are the cells of a DJ console's clips grid (`DjClipGrid`), where a deck plays one cell's clip through the track's live chain ([DJ view and the booth](dj-view.md), the clips grid section). They are the only clips a deck plays: the arrangement's never reach one, since 2026-10-12, when the SONG cell went with the bounce ([DJ decks play live through the track's chain](dj-decks-play-live-through-the-track.md)). Model: `native/src/SessionSlots.cpp`, declared in `Session.h` with `SlotClip`. Tests: `tests/Arrangement/scenarios/ClipCells.inc` (a document of its own), `ClipRoundTrip.inc`, and the clips part of `DjBooth.inc`.

## What the model guarantees

- **Every track has a slot for every scene.** `ensureSceneSlots` gives a document at least `defaultScenes` (eight) and runs on every path that makes a track and on every load ([Mixer](mixer.md)). A group bus has no slots of its own: `clipSlotAt` answers null for one.
- **A cell holds what its lane may hold.** The slot paths ask `clipLaneRefusal`, the question the arrangement's drops and pastes ask, and a pattern goes through `preparePresetTrack`, which switches the whole track's instrument as it does on a lane ([A track's kind is fixed when it is made](track-kind-fixed-at-creation.md), [One instrument per track](one-instrument-per-track.md)).
- **A scene is made only when a clip lands in it.** `createSlotClip`, `copyClipToTrackSlot`, `insertPatternPresetInSlot` and `insertAudioFileInSlot` (with `insertBuiltInSampleInSlot` through it) grow the scenes after every refusal and inside their own undo step. So a drop on the console's + row that makes no clip, a device or a refused sample, adds no row, and one undo takes a new row and its clip together. Until 2026-10-11 the grid called `addScene` before it knew what the drop would do, and every refused drop left an empty row and an undo step of its own.
- **A cell edit is its track's alone.** Each slot path passes its track to `markModified` (`copySlotClipToArrangement` the lane it copied onto), with what it changed - the track's notes or audio clips, and its devices too when a pattern changes the chain (an instrument switched, a synth patch set, a blank rack given the default kit; `preparePresetTrack` reports it). `djDocumentChanged` then marks only the decks playing a cell of that track to make it again, and only for notes or audio clips; a changed chain is heard live and asks nothing. The mark is per track, not per cell, so an edit to another cell marks the track's deck too, and its `clipSignature` makes the remake hand nothing over when the cell it plays did not change (`ClipCells.inc`). Until 2026-10-11 these paths named no track, and one cell edit sent every bounced deck of the set to bounce again.
- **A cell's clip wears its track's colour.** `SlotClip::colour` is `Session::clipColour`: transparent unless the clip has a colour of its own, as a copy of a coloured timeline clip does. The grid paints a transparent one in the track's colour, darkened as the arrangement darkens a clip on its lane ([Track and clip colours](track-and-clip-colours.md)). Nothing on this path stamps or reads the engine's own clip colour.
- **The editors reach a cell's clip.** `findClip` searches the slots after the timeline, `trackHoldingClip` finds a slot clip's track, and every path that removes a slot clip calls `repairPatternClip` ([Hold ids, not pointers](ids-not-pointers.md)). Filling a filled cell is one of those paths: the engine's insert into a `ClipSlot` removes the clip already there whatever `DeleteExistingClips` says (`insertClipWithState` in `tracktion_ClipOwner.cpp`, read at the pinned commit `4536d8a`). `insertPatternPresetInSlot` missed the repair until 2026-10-11, so a pattern dropped on the cell the note editor had open left the editor holding a freed clip.
- **Only copies cross.** `copySlotClipToArrangement` puts a cell's clip on a lane, `copyClipToTrackSlot` a timeline clip or another cell's clip in a cell, and the arrangement's clip menu item *Copy to the track's DJ clips* (`copyClipToSlot`) the clip into the first free cell of its own track. All three rebuild the clip through `copyClipInto`, and a copy takes a name no other clip on the track wears (`uniqueClipNameOnTrack`).
- **Renders keep the slots out.** The merge holds `te::Renderer::ScopedClipSlotDisabler`, so a cell never plays into a render of the arrangement; the deck's bounce held one too until it went on 2026-10-12.

## The session view, removed

Rhino's first use of the slots was a clip launcher: tracks as columns, scenes as rows, Tracktion's `LaunchHandle` starting a slot on a quantised beat. It was built on 2026-09-14, paused behind a build switch, and left unreachable when the DJ view took the view switch on 2026-10-09. On 2026-10-11 the user said they would not use it ("I don't care anymore about the session view, just arrangement and DJ view"), and it was deleted:

- the view (`SessionView.h`, `SessionView.cpp`, `SessionViewPainter.cpp`, `SessionViewGestures.cpp`) and its scenario;
- the launching model: `launchSlot`, `launchScene`, `stopTrackSlots`, `stopAllSlots`, launch quantisation, `nextLaunchBeat`, `trackHasActiveSlot` and the `SlotClip` playing and queued flags;
- Back to Arrangement, laid out zero pixels tall, and the shell's 30 Hz poll of `anyTrackPlayingSlots` behind it, with `returnToArrangement`;
- scene names, scene deletion, the scene watcher, the instrument-clip inserts, `addScene`, and the `presetColour` and `instrumentColour` stamps.

The switch it had shared with the DJ view kept its job under the DJ view's name ([App shell and control bar](app-shell.md)). The scenes and slots stay, because the consoles' grids are them. The alternative, keeping the view paused, kept code no one could reach: the tests and the UI profiler were its only users, and its files carried 31 hex colours against the palette rule and a hand-written instrument menu against the catalog rule.

Nothing in Rhino raises or clears a track's `playSlotClips` flag now. It is a `CachedValue` on the track's state (read in the pinned engine's `tracktion_AudioTrack.h`), so a document saved while a launched clip played keeps it, and that track's timeline stays silent. Under [No backward compatibility for .rhinoedit](no-rhinoedit-back-compat.md) nothing migrates it.

## Related

- [DJ view and the booth](dj-view.md)
- [DJ decks play live through the track's chain](dj-decks-play-live-through-the-track.md)
- [Mixer](mixer.md)
- [Clips never overlap](clip-placement.md)
- [Arrangement view](arrangement-view.md)
- [One instrument per track](one-instrument-per-track.md)
- [Hold ids, not pointers](ids-not-pointers.md)
- [Track and clip colours](track-and-clip-colours.md)
