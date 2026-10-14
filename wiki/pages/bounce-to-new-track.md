---
title: Bounce to New Track
type: component
summary: Ctrl+B renders the selected clips, a track or a group to an audio file on a new track directly under its source and mutes the source in the same undo step, by Live 12's rules - a track before its fader, a group after its bus.
tags: [rhino, arrangement, rendering, groups, undo]
sources: []
updated: 2026-10-14
---

# Bounce to New Track

Live 12's command (Live 12 manual, chapter 20, pp. 418-421; where the manual is: [Development environment and reference material](development-environment.md)), added on 2026-10-14. The key is Ctrl+B, Live's own binding, because Ctrl+T already adds a track. Not the DJ deck's bounce, which went on 2026-10-12 ([superseded](dj-bounce-on-a-copy.md)).

## Where it lives

- **Model**, `native/src/model/SessionBounce.cpp`: `Session::bounceClipsToNewTracks(ids, BounceResult*)` and `bounceTracksToNewTracks(tracks, BounceResult*)` each build `BounceJob`s for the private `bounceToNewTracks`, which refuses, renders every job, and only then edits. `BounceResult` gives the new tracks and clips so the gesture can select them.
- **UI**, `native/src/ui/arrangement/ArrangementBounce.cpp`: `bounceSelected` dispatches on `Focus` as Delete does: `Focus::track` bounces the selected cards, anything else the selected clips, including those a dragged region holds (whole clips, not the region's span). The selection then moves onto what was made. The clip menu offers *Bounce to New Track*; the card menu offers it too, or *Bounce Group to New Track* on a single bus card. Ctrl+B is handled in `Arrangement::keyPressed` and again in the shell (`app/MainComponentCommands.cpp`), like Ctrl+J, so it works with the keyboard in the lower pane; in the DJ view the shell only says to press Tab.

## Rules (Live's)

- **A track** is rendered after its devices, clip effects included, and before its mixer: its fader and pan stay out of the file and are copied to the new track. A track in a group is rendered alone, which the engine will not do by itself ([A track that feeds a bus renders nothing on its own](bus-member-renders-nothing-alone.md)), and its new track joins the group.
- **A group** (its bus card) is rendered after the bus, fader and pan included, with the bus and every member in `tracksToDo`. Its new track sits under the whole group, outside it, at unity. A member chosen with its bus goes in once ([Track groups](track-groups.md)).
- **The main row's devices** are never in a bounce (`useMasterPlugins` off).
- **The new track** is audio, directly under its source, named `<source> (Bounce)` in the source's colour; its clips are `<clip> (Bounce)`, or `(Bounce)` for an unnamed clip.
- **Clips:** one new track per source track, one clip per selected clip over its own span, every clip a window (offset) onto **one** render of the track across their union. A tail ringing into the next clip is in the bounce as in the song, and a gap is silent. A track or group bounce is one clip from the first clip's start to the last clip's end.
- **The source is muted in the same undo step** (*Bounce to new track*): the clips for a clip bounce, the track, or the bus. One Ctrl+Z takes it all back; the files outlive the undo, as a merge's do.

## Refusals and the render

Refused before anything is written: a muted source track or bus, a muted selected clip (both would render silence), a MIDI track with no instrument, any clip whose file is missing (`checkClipFile`, [Offline renders that never return](renders-that-never-return.md)). A track with no clips is passed over among several and refused alone.

The render runs on the message thread, as Ctrl+J merge's does: rendering the live edit from a worker while this thread edits it is a race. The edit comes off the device for it (`te::Edit::ScopedRenderStatus`), automation is mirrored to the engine first, a solo elsewhere cannot silence the rendered tracks (`ScopedTrackSoloIsolator`) and their clip slots are kept out (`ScopedClipSlotDisabler`). Output is a 32-bit float stereo WAV at the device's rate and block size, in `mergedAudioDirectory()` (`<project> Audio` beside the document), named `<track> Bounce N.wav` by `nextRenderFile` (`SessionInternal.cpp`, shared with the merge). A render with no progress for 15 s is given up (`bounceStallSeconds`, through `runRenderTask`); any failure deletes the files written and edits nothing.

Clips the bounce mutes stay visible: the arrangement draws a muted clip grey and its clip menu's ticked *Mute* (`Session::setClipsMuted`) is the way back ([Arrangement view](arrangement-view.md)).

`Arrangement/scenarios/ClipBounce.inc` measures the files (fader not baked in, gap silent, a MIDI clip through its instrument, a member alone, a group with both faders), undo, the refusals, Ctrl+B and the grey strip. It renders, so it sits before `GesturesAndPersistence.inc`.

## Not done (2026-10-14)

- Bouncing a region's exact span: Live cuts at the selection's edges; Rhino bounces the whole clips the region touches.
- *Bounce Track/Group in Place* and *Paste Bounced Audio*.
- A tail (a reverb, a delay) past the last clip's end is cut, because the span ends there.

## Related

- [Arrangement view](arrangement-view.md)
- [Track groups (bus tracks)](track-groups.md)
- [Audio clip editor](audio-clip-editor.md), where Ctrl+J merge is described
- [Session, the model](session-model.md)
