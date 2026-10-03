---
title: Session, the model
type: component
summary: The message-thread facade over one Tracktion engine and edit, one class split across 24 files, where every rule and refusal lives.
tags: [rhino, model, tracktion, undo]
sources: []
updated: 2026-10-03
---

# Session, the model

`rhino::Session` (`native/src/Session.h`) is the whole document model: a `juce::ChangeBroadcaster` that owns one `te::Engine` and the open `te::Edit`, and offers every command the UI may issue. It runs on the message thread only; the engine owns scheduling, streaming and playback. The UI depends on `Session`, never the reverse ([Dependency direction](dependency-direction.md)).

## One class, many files

`Session` is one class defined across 24 translation units (the `src/Session*.cpp` lines of `native/CMakeLists.txt`, as of 2026-10-03), split by caller as [Keeping files small](keeping-files-small.md) describes:

- `Session.cpp`: construction, the starter edit, `restoreProject`, `projectSnapshot`, undo and redo.
- Notes and patterns: `SessionNotes`, `SessionPresets`, `SessionPatches`.
- Tracks and mixing: `SessionTracks`, `SessionGroups`, `SessionMixer`, `SessionAutomation`, `SessionTransport`.
- Devices: `SessionDevices`, `SessionSidechain`, `SessionExternalPlugins`.
- Clips: `SessionClips`, `SessionRegion`, `SessionAudioClips`, `SessionWarp`, `SessionMerge`, `SessionSamples`, `SessionSlots`.
- Input and audition: `SessionRecording`, `SessionMidiInput`, `SessionAudioInput`, `SessionPreview`.
- `SessionInternal.h/.cpp`: property identifiers and helpers private to these files.

`HANDOVER.md`'s table covers the September split of `Session.cpp`; most of the other files arrived after it.

## What every command does

- **Refuses in the model.** A command that can refuse returns a `juce::Result` whose message the shell prints, so a rule (a track's kind, a bus's limits, chain order) is written once and every UI path inherits it.
- **Makes one undo step per intent.** A named `beginNewTransaction`, the edit, then an empty one to close it. Drags are bracketed (`beginNoteGesture`, `beginAudioClipGesture`, `beginTempoGesture`, the fader and device-parameter gestures) so a drag is one entry and one notification, not one per pixel. View settings (row height, group collapse) skip undo.
- **Notifies synchronously** with `sendSynchronousChangeMessage()`, so views re-sync in the same call stack.
- **Counts its own dirt.** `changeRevision`/`savedRevision` track user commands, because engine initialisation flips the edit's changed flag asynchronously.

Replacing the document (`newProject`, `restoreProject`) is bracketed by `Listener::editWillChange`/`editDidChange`, so views drop cached rows, clips and pointers before the old edit is freed. Declaration order destroys the preview and the edit before the engine.

## Pitfalls

- `isCommandLineTestMode()` suppresses machine preferences (browser preview, last track kind) and makes `pickTrackColour` deterministic, so a developer's settings cannot change what the suite sees.
- `RhinoEngineBehaviour` is the only thing Rhino tells the engine: where a recording goes, and that recording mutes what it covers.
- Some state is positional and dates from the original pattern-track-plus-audio-track layout: `patternClip` (the note editor's clip, kept valid by `repairPatternClip`), `rhinoPatternInstrument`, the `utility`/`audioUtility` pointers, and track-0 cases in `deviceSlots` and `deleteDevice`. `patternTrackOf` skips group buses because a bus can sit at index 0.
- Hold clip ids, not `te::Clip*` ([Hold ids, not pointers](ids-not-pointers.md)).

## Related

- [Project files (.rhinoedit)](project-files.md)
- [Track kinds: audio and MIDI](track-kinds.md)
- [The main track](main-track.md)
- [Track automation](automation.md)
- [Rhino's build targets](rhino-build-targets.md)
- [Dependency direction](dependency-direction.md)
- [The first track is still the pattern track](pattern-track.md)
- [Hazards found while seeding the wiki](known-hazards.md)
