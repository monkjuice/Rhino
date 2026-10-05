---
title: No track is special for being first
type: decision
summary: Rhino began as one pattern on one synth track; commit a04b407 retired every rule that singled out the first track, and each track's own chain now records what it plays.
tags: [rhino, model, tracks, history]
sources: []
updated: 2026-10-05
---

# No track is special for being first

## Context

Rhino began as a 16-step pattern on one synth track beside one audio track. Long after tracks became general, parts of the model still treated the first track that was not a group bus (`patternTrackOf`), or index 0, as that pattern track. Nothing on screen said so, and reordering or emptying the stack broke it:

- Deleting or reordering the first track planted the note editor's hidden placeholder MIDI clip on whatever track was now first, an audio track included.
- `restoreProject` refused a document whose first track lacked a MIDI clip and a Utility, so a reordered stack could be saved and then not reopen.
- The edit's `rhinoPatternInstrument` was written for index 0 but re-applied on reopen to whichever track was first, and it knew only four instruments.
- The rack hid other instruments on track 0 and refused to delete its Utility, 4OSC or Drums.
- The positional `utility` and `audioUtility` pointers were never refreshed on undo and were read only by tests.

## Decision

All of it was removed in commit `a04b407` (2026-10-05). Each track's own plugin list records its instrument, and reopening restores the document as saved, after collapsing documents that stacked instruments.

The one survivor is the note editor's clip: `patternClip`, kept beside `patternClipID`. `Session::repairPatternClip` (`SessionClips.cpp`) finds it again by id. Failing that, it takes the first MIDI clip on the first MIDI track (`firstMidiTrackOf`, which skips buses and never answers an audio track), giving that track a hidden starter clip if it has none. A document with no MIDI track has no editor clip: `hasPatternClip()` says so, every note edit refuses with a message, and the step grid draws an empty editor. A pattern preset loads onto the open clip's own track through `preparePresetTrack`, exactly as a drop on that lane does ([Pattern presets](pattern-presets.md)).

## Consequences

- Ask `hasPatternClip()` before `pattern()`, which asserts.
- Do not add state keyed on track position; hold ids ([Hold ids, not pointers](ids-not-pointers.md)).
- `native/src/tests/Pattern/scenarios/NoFirstTrack.inc` removes the first track, moves a track with no clip to the top, saves and reopens, removes the first track's instrument and empties the stack of MIDI tracks. It checks that no MIDI clip lands on an audio track, every track keeps its instrument, and note edits refuse cleanly with no clip open.
- Older pages still tell the history: [Debugging a crash only one project triggers](debugging-a-crashing-project.md) and [No backward compatibility for .rhinoedit](no-rhinoedit-back-compat.md).

## Related

- [Session, the model](session-model.md)
- [Note editor (StepGrid)](note-editor.md)
- [Project files (.rhinoedit)](project-files.md)
- [Track kinds: audio and MIDI](track-kinds.md)
- [Hazards found while seeding the wiki](known-hazards.md)
