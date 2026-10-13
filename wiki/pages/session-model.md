---
title: Session, the model
type: component
summary: The message-thread facade over one Tracktion engine and edit, one class split across 29 files, where every rule and refusal lives.
tags: [rhino, model, tracktion, undo]
sources: []
updated: 2026-10-13
---

# Session, the model

`rhino::Session` (`native/src/model/Session.h`) is the whole document model: a `juce::ChangeBroadcaster` that owns one `te::Engine` and the open `te::Edit`, and offers every command the UI may issue. It runs on the message thread only; the engine owns scheduling, streaming and playback. The UI depends on `Session`, never the reverse ([Dependency direction](dependency-direction.md)).

## One class, many files

`Session` is one class defined across 29 translation units, all in `native/src/model/` (the `src/model/Session*.cpp` lines under `# Session` in `native/CMakeLists.txt`, as of 2026-10-13; [The source tree mirrors the layers](source-tree-mirrors-the-layers.md)), split by caller as [Keeping files small](keeping-files-small.md) describes:

- `Session.cpp`: construction, the starter edit, `restoreProject`, `projectSnapshot`, undo and redo.
- Notes and patterns: `SessionNotes`, `SessionPatternPresets`, `SessionPatches`.
- Tracks and mixing: `SessionTracks`, `SessionGroups`, `SessionMixer`, `SessionAutomation`, `SessionTransport`.
- Devices: `SessionDevices`, `SessionDevicePresets`, `SessionDrums` (the [Drum Rack](drum-rack.md)'s kits, pads, banks, slices and drops), `SessionSidechain`, `SessionExternalPlugins`, `SessionDeviceMacros` (the rack's curated knobs, 4OSC only; `DeviceMacros.cpp` until 2026-10-13).
- Clips: `SessionClips`, `SessionRegion`, `SessionAudioClips`, `SessionWarp`, `SessionMerge`, `SessionSamples`, `SessionSlots`.
- Input and audition: `SessionRecording`, `SessionMidiInput`, `SessionAudioInput`, `SessionPreview`.
- The DJ booth: `SessionDj` (decks, transport, mixer, what the document saves) and `SessionDjSources` (file and audio-cell reads, MIDI cells made into notes, a deck bound to its track, Live, what an edit leaves a deck to do), sharing `SessionDjInternal.h` ([DJ view and the booth](dj-view.md)). The booth comes off the audio device in `releaseAudioDevice` and the destructor; the destructor and `djReset` unbind every deck from its track first, and `djReset` drains the worker. `ensureTrackMixers` (`SessionMixer`) gives every track the booth's two hidden ends beside its fader ([DJ decks play live through the track's chain](dj-decks-play-live-through-the-track.md)).
- `SessionInternal.h/.cpp`: property identifiers and helpers private to these files.

The September 2026 split established the responsibility-based `Session*.cpp` layout; most of the files arrived later.

## What every command does

- **Refuses in the model.** A command that can refuse returns a `juce::Result` whose message the shell prints, so a rule (a track's kind, a bus's limits, chain order) is written once and every UI path inherits it.
- **Makes one undo step per intent.** A named `beginNewTransaction`, the edit, then an empty one to close it. Drags are bracketed (`beginNoteGesture`, `beginAudioClipGesture`, `beginTempoGesture`, the fader and device-parameter gestures) so a drag is one entry and one notification, not one per pixel. Inside a device-parameter drag a step announces only on `deviceParameterValues`, and inside a fader or pan drag nothing at all, until the gesture ends. Several clips moved or deleted together are one step (`moveClips`, `deleteClips`), as are several files dropped together (`importAudioFilesAt`). View settings (row height, group collapse) skip undo.
- **Fails whole.** A command that fails after it has begun editing takes back its own transaction with `undoCurrentTransactionOnly`, so a refusal half way, after a lane was made or an instrument switched, leaves the document as it was (`editClip`, `moveClips`, `pasteClipRegion`, `importAudioFilesAt`; commits `b5d17e0`, `d20faa4`).
- **Notifies synchronously** with `sendSynchronousChangeMessage()`, so views re-sync in the same call stack. The view a command was called from may have rebuilt its own cache by the time the call returns: never hold a reference into a view's list across a `Session` call (commit `2df76f8` fixed two in the arrangement). What each announcement costs is [What a change costs the interface](ui-cost-of-a-change.md).
- **Says what it changed.** Every command ends in `markModified(track, ModelChange)`: the track it changed, or -1, and what it changed, a combinable `Session::ModelChange` of `parameters`, `curves`, `notes`, `audioClips`, `devices` and `tempo`, or `none`, with `unknown` (every kind) the default and what undo, redo and the commands that reshape the stack (moving or removing a track, grouping) pass. `none` - names, colours, lane heights, arming, monitoring, inputs, the click, the count-in, the loop range, a group's collapse, mute and solo, the main fader - bumps the revision and the edit's changed flag and nothing else. Anything else marks the lanes stale, so a listener on the session's own announcement writes them onto the engine's curves ([Track automation](automation.md)); and `notes`, `audioClips` or `tempo` leave the DJ decks playing a cell of that track, or every such deck for -1 or a tempo, to make their cell again (`djDocumentChanged`). A device, a parameter or a curve asks nothing of a deck, which hears its track's chain live ([DJ view and the booth](dj-view.md)). A clip edit goes through `markClipsModified`, which marks each track it reached with `notes` or `audioClips` by the track's kind, and with `devices` too where a MIDI clip changed them by bringing its instrument or its kit ([One instrument per track](one-instrument-per-track.md)); a DJ change the document saves goes through `markDjModified`, which is `none`. Adding a track is `none` too: it moves the main row's index, but the mirror finds a curve by its device and parameter, and no deck plays the new track.
- **Counts its own dirt.** `changeRevision`/`savedRevision` track user commands, because engine initialisation flips the edit's changed flag asynchronously.

Replacing the document (`newProject`, `restoreProject`) is bracketed by `Listener::editWillChange`/`editDidChange`, so views drop cached rows, clips and pointers before the old edit is freed. Anything holding a `te::Plugin::Ptr` must let go before then: the automation mirror's curves and the rack's floating device window both did not, and a plugin outliving its edit corrupted the heap ([Hold ids, not pointers](ids-not-pointers.md)). Declaration order destroys the preview and the edit before the engine.

## Pitfalls

- `isCommandLineTestMode()` suppresses machine preferences (browser preview, last track kind) and makes `pickTrackColour` deterministic, so a developer's settings cannot change what the suite sees.
- `RhinoEngineBehaviour` is the only thing Rhino tells the engine: where a recording goes, that recording mutes what it covers, and, since 2026-10-12, that a muted track still runs its devices (`shouldProcessMutedTracks`), so the song's mute and solo leave a DJ deck its track ([Mixer](mixer.md)).
- The note editor's clip, `patternClip` kept beside `patternClipID`, is the one long-lived pointer. `repairPatternClip` re-finds it after anything that can take it, and a document with no MIDI track has none, so ask `hasPatternClip()` before `pattern()`. The rest of the positional state from the original pattern-track layout was retired in commit `a04b407` ([No track is special for being first](pattern-track.md)).
- Hold clip ids, not `te::Clip*` ([Hold ids, not pointers](ids-not-pointers.md)).
- A device's parameter list can be long: a Drum Rack has 768 controls. On a path that runs per frame or per change, read a range (`deviceParameters(track, slot, first, count)`), one control (`deviceParameter`) or the count (`deviceParameterCount`), not the whole list ([What a change costs the interface](ui-cost-of-a-change.md)).
- View state a command writes without undo may still announce, so other views follow: `showDrumBank` does, for the note editor's drum rows.
- `SessionPatternPresets.cpp` is the [pattern presets](pattern-presets.md); [device presets](device-presets.md) are
  `SessionDevicePresets.cpp`. On 2026-10-05, when it was still `SessionPresets.cpp`, a file write meant to create the
  new one overwrote the old one, restored from git at once; the rename of 2026-10-13 is for exactly that confusion.
  Check a name is free before writing a new file here.

## Related

- [Project files (.rhinoedit)](project-files.md)
- [Track kinds: audio and MIDI](track-kinds.md)
- [The main track](main-track.md)
- [Track automation](automation.md)
- [Rhino's build targets](rhino-build-targets.md)
- [Dependency direction](dependency-direction.md)
- [No track is special for being first](pattern-track.md)
- [What a change costs the interface](ui-cost-of-a-change.md)
- [Hazards found while seeding the wiki](known-hazards.md)
