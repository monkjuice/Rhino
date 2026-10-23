---
title: The 120-track stress audit
type: analysis
summary: What a 120-track, seven-minute song costs Rhino, measured by --profile-stress on 2026-10-23: a change cost the message thread 27.4 ms and now costs 2.7, with the fixes (a cached track list, per-track clip views, the Session facade and its layer lint, deferred device resets) and what is still open.
tags: [rhino, performance, audit, real-time, architecture]
sources: []
updated: 2026-10-23
---

# The 120-track stress audit

On 2026-10-23 the whole app was audited at the size of a finished song, with a new timing tool, and what it found was fixed in four places: the model's track access, the arrangement's clip reads, the views' reach past `Session` and the audio thread's races. This page was written before the work was committed; all six CTest cases passed on it, the layer lint among them. Every figure is a median on the development machine, Release build, on the song below (135 tracks, 103,072 notes), on the message thread unless it says otherwise.

## The tool

`RhinoDAW.exe --profile-stress` (`native/src/tests/StressProfile.cpp`, dispatched in `tests/TestRunners.cpp`, listed in `native/CMakeLists.txt`; `Arrangement` befriends `runStressProfile`). It sits beside `--profile-ui` ([Writing Rhino tests](writing-rhino-tests.md#timing-not-checking)): it checks nothing and is not a CTest case. Run it from a shell, never through `Start-Process`, and compare two builds alternately.

The song is deterministic (fixed seeds): seven minutes at 120 BPM, 210 bars; 120 tracks in 15 groups of 8, so 135 in the edit with the buses; 60 MIDI tracks with 1,768 clips and 103,072 notes in six note-density roles; 60 audio tracks with 1,002 eight-bar clips over four generated loops; 30 4OSC, 10 Rhino FM, 40 Rhino EQ and 24 Compressor; 18 automation lanes. Every panel the shell keeps listening to is built beside it. It prints:

- the build phases, with the process's memory after each, and the first sync of every panel;
- what an announced change costs, the parts of the arrangement's sync, and each per-track `Session` getter times 120;
- paints at the default zoom and over the whole song, a playhead strip, the meters, the rack, and a knob, fader and tempo drag step;
- mute, undo, redo, add a track, import, move a clip and add a device, once stopped and again with the playback graph allocated, with the engine's pending update (its graph rebuild) timed after each;
- save (`projectSnapshot`, `createXml`, `writeTo`) and load (parse, `restoreProject`);
- an offline render of 20 s, and the live graph through the hosted audio device at 512 and 128 samples: median, p99 and max per block, with and without the first second ([the hosted device's recipe](hosted-device-silent-until-dispatched.md)).

Knobs, to bisect: `RHINO_STRESS_TRACKS` (even, half MIDI), `RHINO_STRESS_MINUTES`, `RHINO_STRESS_CLIPS=0`, `RHINO_STRESS_DEVICES=0` and `RHINO_STRESS_RENDER_SECONDS` (default 20, 0 skips the render). With no clips at all, 135 tracks still cost 17 ms a change at baseline, which is how it was seen that the tracks, not the notes, were the larger half of the bill. A finished run writes four 16 s loops, the saved document and the render to a temporary folder and deletes it; the process reaches about 440 MB.

## Measurements

The message thread. "Track cache" is the first fix below; "final" has the clip cache too.

| | Baseline | Track cache alone | Final |
| --- | --- | --- | --- |
| A change announced to every panel | 27.4 ms | 11.9 ms | 2.6-2.8 ms |
| Arrangement `sync()` (all of a change at baseline) | 27.4 ms | 11.9 ms | 2.7 ms |
| `buildRows` | 4.3 ms | 0.34 ms | 0.38 ms |
| `resized` | 3.0 ms | 0.03 ms | 0.03 ms |
| Mute toggle | 28.6 ms | | 3.4-4.1 ms |
| Undo of a mute | 29 ms | | 13.9-15.0 ms |
| Add a track | 29.7 ms | | 4.5 ms |
| Import one audio file | 30 ms | | 4.1 ms |
| Move a clip (the same with the graph allocated) | 29.4 ms | | 4.3-4.4 ms |
| Tempo drag step | 58 ms | 41 ms | 42-43 ms |
| Knob drag step | 0.15 ms | | 0.08 ms |
| Fader drag step | 0.01 ms | | 0.00 ms |
| Arrangement full paint, default zoom and whole song | 11 and 15 ms | | 11.4 and 14.7 ms |
| Playhead strip | 0.6 ms | | 0.65 ms |

At baseline the 27.4 ms of a sync was `buildRows` 4.3, `resized` 3.0, about 9-10 ms of `Session` getters and about 10 ms converting 103k notes through the tempo map and rebuilding 2,770 clip views. Each getter walked `te::getAudioTracks(*edit)`, about fifteen of them per track: `trackCount` 3.7 µs a call, `trackMixer` 5, `trackName` 7.8, `deviceSlots` 10, `trackRecordInput` 10, `trackMonitoring` 14, `trackGroups` 15 and `trackAutomations` 20 µs. After the track cache, 120 calls of a getter cost 0.00-0.07 ms, except `trackAutomations` 0.27, `deviceSlots` 0.32 and `trackGroups` 1.37 ms, which do real work per call (`trackGroups` scans the whole stack each time). The paints did not move. The meters' read was 0.08 ms at baseline and was not recorded again.

Undo and the tempo are the two that stayed dear. Neither names a track, so every track's views are read again (undoing a mute costs 13.9-15.0 ms with the getters now O(1)), and a tempo change also makes the engine remap every clip.

Costs the work does not touch, mostly the engine's, with the final run beside the baseline (percentages are of the block's time at 48 kHz):

| What | Baseline | Final |
| --- | --- | --- |
| Process memory with the document | 438 MB: the 30 4OSC 320 MB (10.7 MB each, Tracktion's plugin), the notes 60 MB, the audio clips 17 MB; the 10 Rhino FM 1.2 MB, 40 Rhino EQ 3.9 MB and 24 Compressor 0.3 MB between them | 440 MB |
| Save: `projectSnapshot` on the message thread, `createXml` and `writeTo` on the worker | 90-97, 470-550 and 80-1,050 ms (disk variance); a 6.7 MB file | 91, 475 and 82 ms |
| Load: parse, then `restoreProject` with every panel | 340-385 and 580-630 ms | 344 and 582 ms |
| Building the playback graph (`ensureContextAllocated`) | 300-315 ms with or without clips, devices or tracks; mostly opening the device in the test environment | |
| The engine's pending update after a structural edit, graph allocated | 95-110 ms after a MIDI or audio clip move, `restartPlayback` or adding a device (the first 516 ms); 0 ms after a mute | 94-104 ms after a MIDI clip move (the first 527 ms), 90-93 ms after adding a device |
| Offline render | 5.7-6.1 times real time; 135 empty tracks alone 9.6 times (1.1 ms a 512 block) | 6.2 times real time (1,715 µs a 512 block) |
| Live graph, 512-sample blocks (10.7 ms) | median 975-1,030 µs (9-10%), p99 1.6-2.0 ms (15-18%), max after warm-up 2 ms | median 1,115 µs (10%), p99 2.0 ms (19%), max after warm-up 2.8 ms |
| Live graph, 128-sample blocks (2.7 ms) | median 394-401 µs (15%), p99 850-950 µs (32-35%), max 1.3 ms | median 421 µs (16%), p99 858 µs (32%), max 2.0 ms |

`native_device_correctness` (`--self-test`) took 21 s alone and 29 s when run straight after a profile, against its limit of 30 s ([Build and test Rhino](build-and-test-rhino.md)).

## What it found and what was done

### The model caches its track list

Every `Session` getter walked the engine's track list, so a sync grew with the square of the track count. `Session::audioTracks()` now hands back a copy of a cached array (a copy, because callers add and remove tracks while holding it), `audioTrack(index)` one track, and a `ValueTree` listener on the edit's state marks the cache stale when any child is added, removed, reordered or redirected anywhere in the tree. The 121 `te::getAudioTracks(*edit)` calls in the `Session` units became `audioTracks()`; the free helpers in `SessionInternal.cpp`, which take a `te::Edit&`, and `restoreProject`'s check of the candidate edit still walk the tree. That alone took a sync from 27.4 to 11.9 ms. The first patch rewrote the cache's own rebuild into a call to itself, and every runner died with 0xC00000FD before it printed a line ([A CTest SegFault may be a stack overflow](stack-overflow-reports-as-segfault.md)). Details: [Session, the model](session-model.md#reading-the-document).

### Clip views are cached per track

What was left of a sync was reading every clip of every track and converting 103,072 notes through the tempo map, about 10 ms. `Session::trackRevision(track)` counts the changes marked against each track, and the arrangement keeps each track's clip views with the revision they were read at, reading a track again only when it moved or the track is new. MIDI notes are kept in the sequence's own seconds, shared with the cache and drawn from the clip window's origin, so a moved or trimmed clip converts nothing. A sync fell from 11.9 to 2.7 ms and a mute from 28.6 to 3.4-4.1 ms. Details: [Arrangement view](arrangement-view.md), [What a change costs the interface](ui-cost-of-a-change.md).

### The views ask `Session`, never the engine

An audit (by a sub-agent) found the UI and the shell reaching past `Session` in 65 lines of 18 files: tempo-map conversions, transport reads and writes, undo-manager reads, the playback-context reader in `ui/shared/Playhead.cpp`, and the shell listening to the engine's transport and undo manager and subscribing again on every document. They now call new `Session` methods (`isPlaying`, `playheadSeconds`, `locate`, `songLengthSeconds`, `secondsToBeats`, `beatsToSeconds`, `canUndo`, `canRedo`, `restartPlaybackIfRolling`, `audioDeviceManager`, `audioDeviceSummary`, `ownsDevice`) and listen to two relayed broadcasters, `transportState` and `undoHistory`. `Session::edit` and `engine` stay public because about 50 test files use them, so a lint holds the line instead. `Session::gestureHeld()` no longer asks JUCE whether a mouse button is down; the shell's 30 Hz timer tells the model (`setPointerHeld`). The tidy afterwards removed the `TransportControl` forward declaration from `ui/shared/Playhead.h`, which only the reader that moved to `Session` needed. Details: [Dependency direction](dependency-direction.md#the-views-ask-session-not-the-engine).

### The audio thread

A read of every audio callback Rhino writes (a sub-agent's audit, by reading, not by measuring) found no per-block allocation, lock, file access or logging in them. It found a race, some unguarded denormal paths and a MIDI thread that allocated:

- `reset()` on Rhino Bloom, Rhino EQ, the Vocoder and Rhino Tune cleared state on the message thread (`Session::panicReset` calls it) while the audio thread could be inside a block. Each now sets a `std::atomic<bool> clearPending` and the next `applyToBuffer` clears, as the SDK's `NativeDevice` already did.
- `juce::ScopedNoDenormals` joined `NativeDevice::applyToBuffer`, so every SDK device has it, and Bloom, the Vocoder and Rhino Tune. The Vocoder's float followers are flushed below 1e-15 in `VocoderEngine::flushDenormals`.
- The Push's MIDI thread grew a `std::vector` under a `SpinLock`; buttons, encoders and touches now cross a fixed ring of 256 messages that drops one when full.

Details: [Real-time audio rules](real-time-audio-rules.md), [Ableton Push 1](ableton-push.md).

## Open

As of 2026-10-23.

- **Message thread.** Undo, redo and a tempo change name no track, so they read every track again: 13.9-15.0 ms to undo a mute and 42-43 ms a tempo drag step, the engine also remapping every clip.
- **Audio thread.**
  - The browser preview's `AudioSourcePlayer` chain stays attached after the first audition, and JUCE's chain takes locks per block; detaching it while idle is the cheapest fix.
  - Rhino Tune's three FFTs per hop run inline in one block (0.07-0.35 ms bursts).
  - The DJ booth's send and beat effects wipe 0.9-3.2 MB delay lines inline on a type change.
  - `DjMixer`, Bloom, Space, `DrumSynth` and `FmEngine` call transcendentals per sample.
  - `CountInClick` posts an `AsyncUpdater` from the audio thread once per count.
- **Reaching past `Session`** (documented, not changed): views that write a device's view state straight to the device (Rhino EQ's selected band, the Drum Rack's selected pad and auto-select, the pad-playback drag bracket); file choosers and name or overwrite dialogs inside device faces and the DJ deck menu; device ids spelled in a few views; `findAudioClip != nullptr` as a clip-kind test in the shell.
- **The engine's, not Rhino's code.** 4OSC costs 10.7 MB an instance, 320 of the 438 MB. A structural edit while the graph is allocated stalls the message thread 90-110 ms, Tracktion rebuilding a graph of about 135 tracks times (five infrastructure plugins and their devices) and 2,770 clips. That per-track infrastructure (the hidden DJ input and output, Utility, Volume & Pan, the level meter) is also the floor of the live graph: 135 empty tracks render at 9.6 times real time. `createXml` takes about 0.5 s for 100,000 notes, already on the save's worker; `projectSnapshot`, 91-97 ms, is on the message thread.

## Re-run

Build the Release app and run `RhinoDAW.exe --profile-stress` from a PowerShell on an idle machine, with the knobs above to bisect. Alternate it with a build of the old revision, as with `--profile-ui`: the live graph's median was 975-1,030 µs in the baseline runs and 1,115 µs in the final one, so read a difference of that size with a second run before believing it. The paints go to a software image ([Profile paint on a software image](profile-paint-on-a-software-image.md)).

## Related

- [Session, the model](session-model.md)
- [Arrangement view](arrangement-view.md)
- [What a change costs the interface](ui-cost-of-a-change.md)
- [Dependency direction](dependency-direction.md)
- [Real-time audio rules](real-time-audio-rules.md)
- [Ableton Push 1](ableton-push.md)
- [Writing Rhino tests](writing-rhino-tests.md)
- [Build and test Rhino](build-and-test-rhino.md)
- [A CTest SegFault may be a stack overflow](stack-overflow-reports-as-segfault.md)
- [Tracktion's hosted device is silent until its start is dispatched](hosted-device-silent-until-dispatched.md)
- [Hazards found while seeding the wiki](known-hazards.md)
