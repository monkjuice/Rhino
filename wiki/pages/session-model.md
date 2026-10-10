---
title: Session, the model
type: component
summary: The message-thread facade over one Tracktion engine and edit, one class split across 28 files, where every rule and refusal lives.
tags: [rhino, model, tracktion, undo]
sources: []
updated: 2026-10-10
---

# Session, the model

`rhino::Session` (`native/src/Session.h`) is the whole document model: a `juce::ChangeBroadcaster` that owns one `te::Engine` and the open `te::Edit`, and offers every command the UI may issue. It runs on the message thread only; the engine owns scheduling, streaming and playback. The UI depends on `Session`, never the reverse ([Dependency direction](dependency-direction.md)).

## One class, many files

`Session` is one class defined across 28 translation units (the `src/Session*.cpp` lines of `native/CMakeLists.txt` before `SessionView`, as of 2026-10-09), split by caller as [Keeping files small](keeping-files-small.md) describes:

- `Session.cpp`: construction, the starter edit, `restoreProject`, `projectSnapshot`, undo and redo.
- Notes and patterns: `SessionNotes`, `SessionPresets`, `SessionPatches`.
- Tracks and mixing: `SessionTracks`, `SessionGroups`, `SessionMixer`, `SessionAutomation`, `SessionTransport`.
- Devices: `SessionDevices`, `SessionDevicePresets`, `SessionDrums` (the [Drum Rack](drum-rack.md)'s kits, pads, banks, slices and drops), `SessionSidechain`, `SessionExternalPlugins`.
- Clips: `SessionClips`, `SessionRegion`, `SessionAudioClips`, `SessionWarp`, `SessionMerge`, `SessionSamples`, `SessionSlots`.
- Input and audition: `SessionRecording`, `SessionMidiInput`, `SessionAudioInput`, `SessionPreview`.
- The DJ booth: `SessionDj` (decks, transport, mixer, what the document saves) and `SessionDjSources` (file reads, bounces, Live, the stale re-bounce), sharing `SessionDjInternal.h` ([DJ view and the booth](dj-view.md)). The booth comes off the audio device in `releaseAudioDevice` and the destructor, and `djReset` drains its worker before the bounces' copies of the document go.
- `SessionInternal.h/.cpp`: property identifiers and helpers private to these files.

The September 2026 split established the responsibility-based `Session*.cpp` layout; most of the files arrived later.

## What every command does

- **Refuses in the model.** A command that can refuse returns a `juce::Result` whose message the shell prints, so a rule (a track's kind, a bus's limits, chain order) is written once and every UI path inherits it.
- **Makes one undo step per intent.** A named `beginNewTransaction`, the edit, then an empty one to close it. Drags are bracketed (`beginNoteGesture`, `beginAudioClipGesture`, `beginTempoGesture`, the fader and device-parameter gestures) so a drag is one entry and one notification, not one per pixel. Inside a device-parameter drag a step announces only on `deviceParameterValues`, and inside a fader or pan drag nothing at all, until the gesture ends. Several clips moved or deleted together are one step (`moveClips`, `deleteClips`), as are several files dropped together (`importAudioFilesAt`). View settings (row height, group collapse) skip undo.
- **Fails whole.** A command that fails after it has begun editing takes back its own transaction with `undoCurrentTransactionOnly`, so a refusal half way, after a lane was made or an instrument switched, leaves the document as it was (`editClip`, `moveClips`, `pasteClipRegion`, `importAudioFilesAt`; commits `b5d17e0`, `d20faa4`).
- **Notifies synchronously** with `sendSynchronousChangeMessage()`, so views re-sync in the same call stack. The view a command was called from may have rebuilt its own cache by the time the call returns: never hold a reference into a view's list across a `Session` call (commit `2df76f8` fixed two in the arrangement). What each announcement costs is [What a change costs the interface](ui-cost-of-a-change.md).
- **Mirrors automation after it.** `markModified` marks the lanes stale, and a listener on the session's own announcement writes them onto the engine's curves ([Track automation](automation.md)). It also marks the bounced DJ decks stale (`djDocumentChanged`; `markModified(track)` names the track a knob move changed, and then only the decks playing that track, or a group it is in, go stale); a DJ change the document saves uses `markDjModified` instead, so it dirties the document without re-bouncing anything.
- **Counts its own dirt.** `changeRevision`/`savedRevision` track user commands, because engine initialisation flips the edit's changed flag asynchronously.

Replacing the document (`newProject`, `restoreProject`) is bracketed by `Listener::editWillChange`/`editDidChange`, so views drop cached rows, clips and pointers before the old edit is freed. Anything holding a `te::Plugin::Ptr` must let go before then: the automation mirror's curves and the rack's floating device window both did not, and a plugin outliving its edit corrupted the heap ([Hold ids, not pointers](ids-not-pointers.md)). Declaration order destroys the preview and the edit before the engine.

## Pitfalls

- `isCommandLineTestMode()` suppresses machine preferences (browser preview, last track kind) and makes `pickTrackColour` deterministic, so a developer's settings cannot change what the suite sees.
- `RhinoEngineBehaviour` is the only thing Rhino tells the engine: where a recording goes, and that recording mutes what it covers.
- The note editor's clip, `patternClip` kept beside `patternClipID`, is the one long-lived pointer. `repairPatternClip` re-finds it after anything that can take it, and a document with no MIDI track has none, so ask `hasPatternClip()` before `pattern()`. The rest of the positional state from the original pattern-track layout was retired in commit `a04b407` ([No track is special for being first](pattern-track.md)).
- Hold clip ids, not `te::Clip*` ([Hold ids, not pointers](ids-not-pointers.md)).
- A device's parameter list can be long: a Drum Rack has 768 controls. On a path that runs per frame or per change, read a range (`deviceParameters(track, slot, first, count)`), one control (`deviceParameter`) or the count (`deviceParameterCount`), not the whole list ([What a change costs the interface](ui-cost-of-a-change.md)).
- View state a command writes without undo may still announce, so other views follow: `showDrumBank` does, for the note editor's drum rows.
- `SessionPresets.cpp` is the [pattern presets](pattern-presets.md); [device presets](device-presets.md) are
  `SessionDevicePresets.cpp`. On 2026-10-05 a file write meant to create the new one overwrote the old one, restored
  from git at once. Check a name is free before writing a new file here.

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
