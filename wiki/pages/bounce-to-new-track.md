---
title: Bounce to New Track
type: component
summary: Ctrl+B renders the selection as a span (a clicked clip is its own span), a track or a group to an audio file on a new track directly under its source and mutes the source in the same undo step, by Live 12's rules - a track before its fader, a group after its bus, a time selection cut at its edges - a slice per frame behind a cancellable loading screen.
tags: [rhino, arrangement, rendering, groups, undo, threads, selection]
sources: []
updated: 2026-10-23
---

# Bounce to New Track

Live 12's command (Live 12 manual, chapter 20, pp. 418-421; where the manual is: [Development environment and reference material](development-environment.md)), added on 2026-10-14. The key is Ctrl+B, Live's own binding, because Ctrl+T already adds a track. Not the DJ deck's bounce, which went on 2026-10-12 ([superseded](dj-bounce-on-a-copy.md)).

## Where it lives

- **Model**, `native/src/model/SessionBounce.cpp`, stepped since commit `d6f53ba` (2026-10-14):
  - `startBounceClips(ids)` / `startBounceRegion(start, end, firstTrack, lastTrack)` (commit `cbf0cf0`, 2026-10-23) / `startBounceTracks(tracks)` build one `BounceJob` per source, make every refusal at once, take the edit off the device (`te::Edit::ScopedRenderStatus`, held for the whole run), stop transports and mirror automation. A start while `isBouncing()` or `isExporting()` is refused (the WAV export runs the same way; [Project files](project-files.md#wav-export)).
  - `continueBounce(ms)` renders for about that long and returns true while work remains; one `RenderTask::runJob` call is one block (Tracktion at `4536d8a`). After the last job it calls the private `finishBounce`, which makes the whole edit (tracks, clips, muting) as one undo step.
  - `cancelBounce()` (and the private `abandonBounce(why)` on a failure) destroys the render in flight, deletes every file written and leaves the document untouched.
  - `bounceOutcome(BounceResult*)` says how it ended (`BounceResult` gives the new tracks and clips to select); `bounceProgress()` gives the fraction by length rendered across jobs, the source's name and step/steps.
  - The run is `Session::BounceRun`, defined only in `SessionBounce.cpp` and held by `std::unique_ptr<BounceRun, BounceRunDeleter>` so `Session.cpp` need not see it. One job's render and everything it holds in place (solo isolator, slot disabler, the [`ScopedProperty` overrides](bus-member-renders-nothing-alone.md), the `RenderTask` last so it dies first) is `RenderInFlight`, in an anonymous namespace.
  - `bounceClipsToNewTracks` / `bounceRegionToNewTracks` / `bounceTracksToNewTracks` survive as run-to-the-end wrappers (start, loop `continueBounce`, outcome) for the tests and for an `Arrangement` with no shell.
- **UI**, `native/src/ui/arrangement/ArrangementBounce.cpp`: `bounceClips`/`bounceTracks` start the bounce, then `runBounce()` hands it to the shell through `Arrangement::bounceStarted` (the [loading screen](app-shell.md#the-loading-screen)) or, with no shell, loops it to the end; `bounceFinished()` reads the outcome, selects what was made and writes the status line ("The bounce was cancelled." included). `bounceSelected` dispatches on what the last click selected, as Delete does: `Focus::track` bounces the selected cards; any range selection (`timeSelection.isRange()`) goes to `bounceRegion`, which bounces the span itself ([below](#a-time-selection)), and since 2026-10-23 a clicked clip is its own span, so it too is bounced as a region cut at its edges, which is the clip whole, muted whole ([Region editing](region-editing.md#one-selection)); `bounceClips(clipsToActOn())` remains only for a primary clip with no span around it, after a ruler click. `bouncingRegion` makes `bounceFinished` say the originals are muted inside the selection. The selection then moves onto what was made. The clip menu's *Bounce to New Track* calls `bounceSelected` too (since `cbf0cf0`; before, `bounceClips(clipsToActOn())`), so a right-click inside a dragged span bounces the span, and one on a clip outside it selects that clip and bounces it whole. The card menu offers it too, or *Bounce Group to New Track* on a single bus card. Ctrl+B is handled in `Arrangement::keyPressed` and again in the shell (`app/MainComponentCommands.cpp`), like Ctrl+J, so it works with the keyboard in the lower pane; in the DJ view the shell only says to press Tab.

## Rules (Live's)

- **A track** is rendered after its devices, clip effects included, and before its mixer: its fader and pan stay out of the file and are copied to the new track. A track in a group is rendered alone, which the engine will not do by itself ([A track that feeds a bus renders nothing on its own](bus-member-renders-nothing-alone.md)), and its new track joins the group.
- **A group** (its bus card) is rendered after the bus, fader and pan included, with the bus and every member in `tracksToDo`. Its new track sits under the whole group, outside it, at unity. A member chosen with its bus goes in once ([Track groups](track-groups.md)).
- **The main row's devices** are never in a bounce (`useMasterPlugins` off).
- **The new track** is audio, directly under its source, named `<source> (Bounce)` in the source's colour; its clips are `<clip> (Bounce)`, or `(Bounce)` for an unnamed clip.
- **Clips** (`startBounceClips`; reached from the UI only for a primary clip with no span around it since 2026-10-23): one new track per source track, one clip per selected clip over its own span, every clip a window (offset) onto **one** render of the track across their union. A tail ringing into the next clip is in the bounce as in the song, and a gap is silent. A track or group bounce is one clip from the first clip's start to the last clip's end.
- **The source is muted in the same undo step** (*Bounce to new track*): the clips for a clip bounce, the part inside the selection for a time selection, the track, or the bus. One Ctrl+Z takes it all back; the files outlive the undo, as a merge's do.

## A time selection

Live 12 (pp. 418-420, the screenshot on p. 420): a time selection becomes a new track for every track with a clip in it, the bounce covers exactly the selection, and the sources are cut at its edges and muted inside. Rhino since commit `cbf0cf0` (2026-10-23); before, Ctrl+B bounced the whole clips the span touched.

- **Which tracks.** Each track in the span's run holding an unmuted, arrangement-visible clip that overlaps it. A clip inside by less than `minimumSplitSeconds` (10 ms) only grazes an edge and is left out; one wholly inside counts however short. Bounce keeps a fixed 10 ms and refuses a span under it ("Select a span of the timeline to bounce."), while `copyClipRegion`'s floor shrinks to a tenth of a region under 0.1 s since commit `cf4e959` ([Region editing](region-editing.md)), so a deepest-zoom cell copies but does not bounce. Lanes with nothing playing are passed over; with none at all: "There is no clip playing inside the selection to bounce."
- **Muted clips are left out, not refused**, unlike a clip bounce: they play nothing in the song. So a second Ctrl+B over a span already bounced is refused.
- **The new clip** spans exactly the selection, offset 0, named `<clip> (Bounce)` (or `(Bounce)`) after the earliest clip in it and in that clip's colour, Live's rule.
- **The cut.** `finishBounce`, in the bounce's undo step, splits each rendered source clip at the selection's start and end and mutes only the inside (`BounceJob::region`). A clip reaching past an edge by less than 10 ms is not split there but muted whole, as Delete treats it for a span of 0.1 s or more. The cuts call the engine's `splitClip` directly, which refuses within 1 ms of a clip's end; the 10 ms floors and the 10 ms minimum span keep every cut clear of that, so scaling them as regions do would need Rhino's `splitClipAt` ([Clips never overlap](clip-placement.md#one-place)).
- **Tracktion** (upstream source at `4536d8a`): `ClipTrack::splitClip` is `split(clip, time)` in `model/clips/tracktion_ClipOwner.cpp`. It copies the clip's state into a new clip starting at `time`, trims the original with `setEnd(time)` and returns the new right-hand clip; the original keeps its id and the left part. Split at the start, then split the returned clip at the end.
- **A note held into the selection is in the bounce.** A render starting mid-clip chases notes: `MidiNode` calls `MidiNodeHelpers::createMessagesForTime` when the play head is not contiguous with the last block or sits at the clip's local start, and (non-MPE) it re-sends the note-on of every note that spans that time.

## Refusals and the render

Refused before anything is written: a muted source track or bus, a muted selected clip (both would render silence; a time selection leaves muted clips out instead), a MIDI track with no instrument, any clip whose file is missing (`checkClipFile`, [Offline renders that never return](renders-that-never-return.md)). A track with no clips is passed over among several and refused alone.

The render runs on the message thread, a slice per frame behind the shell's modal loading screen: rendering the live edit from a worker while this thread edits it is a race ([A long command runs in slices behind a modal screen](long-commands-run-in-slices.md)). Until commit `d6f53ba` it was one blocking loop, as Ctrl+J merge's still is, and the window froze. Because the run holds clips and track indices across slices, nothing may edit meanwhile: the screen takes all input, `Session::undo`/`redo` do nothing while `isBouncing()`, `newProject`, `restoreProject` and `~Session` call `cancelBounce()` first, the Push's `commandsAllowed` is false, and closing the window cancels before asking about unsaved changes.

A solo elsewhere cannot silence the rendered tracks (`ScopedTrackSoloIsolator`) and their clip slots are kept out (`ScopedClipSlotDisabler`). Output is a 32-bit float stereo WAV at the device's rate and block size, in `mergedAudioDirectory()` (`<project> Audio` beside the document), named `<track> Bounce N.wav` by `nextRenderFile` (`SessionInternal.cpp`, shared with the merge) when its job starts, so two sources of one name never get the same file. A render with no progress for 15 s is given up (`bounceStallSeconds`, through `stopRenderWhenStalled`/`renderTaskOutcome`); a failure or a cancel deletes the files written and edits nothing.

Clips the bounce mutes stay visible: the arrangement draws a muted clip grey and its clip menu's ticked *Mute* (`Session::setClipsMuted`) is the way back ([Arrangement view](arrangement-view.md)).

`Arrangement/scenarios/ClipBounce.inc` measures the files (fader not baked in, gap silent, a MIDI clip through its instrument, a member alone, a group with both faders), undo, the refusals, Ctrl+B and the grey strip. A time-selection block bounces 0.25-0.75 s of a sine fixture (quiet first half, loud second) across two lanes, checks by RMS that the file starts at the selection, that the source is cut in three with only the middle muted, that one undo restores the single clip, and that a second Ctrl+B over the muted span is refused. A stepped run renders one slice, refuses a second start, ignores an undo, then cancels with the track count, mute, fader and the folder's files unchanged. The real `LoadingScreen` drives one bounce to the end and hands it back once, and Esc cancels another; the runner has no message loop, so it is the screen's friend and calls `timerCallback` itself. It renders, so it sits before `GesturesAndPersistence.inc`.

## Not done (2026-10-23)

- *Bounce Track/Group in Place* and *Paste Bounced Audio*.
- A tail (a reverb, a delay) past the end of what is bounced, the last clip's end or the selection's end, is cut, because the render ends there.
- `SessionBounce.cpp` is 661 lines, past the split line; splitting it means moving `BounceJob` into a private `SessionBounceInternal.h` ([Keeping files small](keeping-files-small.md)).

## Related

- [Arrangement view](arrangement-view.md)
- [Region editing](region-editing.md), the dragged span and the edge rule
- [Clips never overlap](clip-placement.md), the engine's 1 ms split refusal
- [Track groups (bus tracks)](track-groups.md)
- [Audio clip editor](audio-clip-editor.md), where Ctrl+J merge is described
- [Session, the model](session-model.md)
- [App shell and control bar](app-shell.md), the loading screen
- [A long command runs in slices behind a modal screen](long-commands-run-in-slices.md)
