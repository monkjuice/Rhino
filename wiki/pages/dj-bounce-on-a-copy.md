---
title: A deck's bounce renders on a worker against a copy of the document
type: decision
summary: A track or group bounced to a DJ deck renders on the booth's worker from a snapshot copy of the edit, never the live edit nor the message thread; the copy's plugins and the test runners' inline render are the price.
tags: [rhino, dj, rendering, threads]
sources: []
updated: 2026-10-10
---

# A deck's bounce renders on a worker against a copy of the document

## Context

A DJ deck plays audio, not the edit ([DJ view and the booth](dj-view.md)), so a track or group of the song reaches a deck by being rendered to a file, a bounce, and is bounced again after every edit. The first version (commit `e4040dd`) rendered on the message thread, as Ctrl+J merge does (`SessionMerge.cpp`): a render reads the edit, and an edit changed under it from the message thread is a race, so blocking that thread was the simple way to be safe. Measured on 2026-10-09, one bar took 0.6-1 s with nothing repainting; a long track would have frozen the window for many seconds, on every re-bounce.

## The choice

Commit `363fd05`: the render runs on the booth's one worker thread, against a copy of the document loaded from a snapshot of its state (`te::loadEditFromState(engine, edit->state.createCopy())` in `Session::bounceDjDeck`, `native/src/SessionDjSources.cpp`), given the live edit's `editFileRetriever` so its media resolves. This is the rule the file workers already follow ([Project files (.rhinoedit)](project-files.md)): a worker is handed a detached snapshot, never the live edit. The live edit stays on the device, so the decks and a track played Live are not interrupted, and the person goes on editing while it renders.

Rejected: rendering the live edit from the worker (the race above), and keeping the render synchronous (the freeze).

## What it costs and what it requires

- **The copy holds only the tracks the bounce plays, and every bus** (since 2026-10-10). `bounceDjDeck` copies the state after `mirrorAutomationToEngine` and `flushState`, so the copy carries the lanes as playback would, then removes every `TRACK` child whose id (`te::EditItemID::fromProperty(child, te::IDs::id)`) is neither a member's nor a group bus's before `loadEditFromState`. The copy's plugins are still made again for every bounce, but only the bounced tracks', so a synth or a kit on another track no longer costs the message thread anything; the `bouncing` line in `rhino.log` carries the copy's time, 0.3-6 ms in the test documents. Making the copy and the `RenderTask` still happens on the message thread; the stall felt over repeated knob gestures was Live's monitoring switches rebuilding the song's graph, not the copy ([DJ view and the booth](dj-view.md)).
- **The stripped copy must have its routing set again.** A track's output names its destination bus by its place in the audio-track list, not by id (observed: in the stripped copy a group member rendered straight out, at the same level before and after a −20 dB bus fader, where the full copy had routed it into the bus). So once the copy's tracks are found, `bounceDjDeck` points each member whose live track feeds a bus (`getDestinationTrack()`) at the copy's track carrying that bus's id with `setOutputToTrack`, before the `RenderTask` is made. `DjBooth.inc`'s "passes through the bus's fader" check is what caught it ([Track groups (bus tracks)](track-groups.md)).
- **`DjBounceWork` belongs to the message thread alone** (`SessionDjInternal.h`): the copy, a `ScopedTrackSoloIsolator`, a `ScopedClipSlotDisabler`, the `WavAudioFormat` and the `RenderTask`, declared so the render and the scopes die before the copy. It is made in `bounceDjDeck` and destroyed in `djPoll` once the job has raised `done`; the worker holds only a plain pointer to the render plus the shared `DjLoadJob`, and touches neither after `done`. `djReset` and `~DjBooth` drain the pool (`removeAllJobs(true, 10000)`) before dropping jobs and bounces, because a render still running reads a copy being destroyed.
- **A sixty-second budget.** A render that keeps returning unfinished would hold the only worker for good, so the loop gives up with "The bounce did not finish in time"; the job's `cancel` flag ends it sooner. Only a new source for the deck sets that flag (since 2026-10-10): a bounce asked for while the deck's own is still rendering marks the deck stale and returns, and `finishDjLoad` bounces again at landing, because cancelling under every edit meant a long render never landed while its devices were worked. The worker runs at `juce::Thread::Priority::low`, so a render no longer competes with the audio thread. The WAV goes to `%TEMP%\Rhino DJ bounces`, is read back with `readDjTrack`, analysed with the song's tempo as `knownBpm`, and deleted.
- **The test runners render inline.** Tracktion's render initialisation takes a `MessageManagerLock`, which a worker is granted only while the message thread dispatches. The app's always does, as it does for the WAV export; the runners have no dispatch loop (`JUCE_MODAL_LOOPS_PERMITTED=0`), so under `isCommandLineTestMode` the render runs on the message thread and `djPoll` installs it as before. The worker path is exercised only by the app, as the export's is.
- **The renderer fails at once, with "Didn't find any audio to render", when nothing in the job makes audio.** Two triggers are known. A group names its bus and the members that feed it in `tracksToDo`, because the engine builds a member into its bus's node and naming only part of that rendered nothing; `DjBooth.inc` checks the bus's own fader is in the bounce by turning it down and bouncing again. And a MIDI track running no instrument has nothing to turn its clips into audio, so `bounceDjDeck` catches it before any copy is made and gives the deck silence instead (below).

The render's parameters are a merge's: from the top of the song to the end of the last clip among the tracks, rounded up to whole bars and at least one, at the device's rate and block size, `usePlugins` on, `useMasterPlugins` off, 24-bit stereo. Solo elsewhere and session-view slot clips are kept out by the two scopes. A deck on a clip slot (since 2026-10-10, [the console's clips grid](dj-view.md)) is the one exception: its span is that clip's own length, and before the render the copy track's timeline clips are removed and the slot's clip laid from the top in their place by `copyClipInto` with its loop dropped, the slot itself staying disabled like every other. A MIDI track with no instrument is not rendered at all (2026-10-10): once the span is known, `bounceDjDeck` cancels the deck's pending job, makes a `DjTrack` of silence of that span at the device's rate (48 kHz with no device), analyses it with the song's tempo as the known grid and no key detection, and hands it to `finishDjLoad` with the deck's generation and the `keepBeat` asked for, as if the worker had returned it; `rhino.log` says the deck holds N bars of silence. An instrument added later marks the deck stale like any edit, and the next bounce is real ([DJ view and the booth](dj-view.md)).

**A bounce carries the material's notes** (2026-10-10). Before the silent-track path and the copy, `bounceDjDeck` gathers the notes the render will play as `DjMidiEvent`s (`core/DjTrack.h`): a slot deck's clip from beat zero over the clip's length, else every MIDI clip the arrangement shows (`shouldShowClipInArrangement`) at its start beat; each note's clip-relative beat less `Session::clipOffsetBeats`, clipped to the clip's window; sorted by beat, a note-off before a note-on at the same beat. A group's bounce gathers none. They ride in `DjLoadJob::midi`, the worker moves them into `DjTrack::midi` beside the audio, and the silent bounce carries them too. The live preview plays them to the track's own instrument while a knob is held ([DJ view and the booth](dj-view.md)).

## Related

- [DJ view and the booth](dj-view.md)
- [Project files (.rhinoedit)](project-files.md)
- [Arrangement view](arrangement-view.md)
- [Offline renders that never return](renders-that-never-return.md)
- [Real-time audio rules](real-time-audio-rules.md)
