---
title: The first track is still the pattern track
type: gotcha
summary: Rhino began as one pattern on one synth track, and several rules still single out the first track, which surprises any change that reorders or empties it.
tags: [rhino, model, tracks, legacy]
sources: []
updated: 2026-10-03
---

# The first track is still the pattern track

Rhino began as a 16-step pattern on one synth track beside one audio track. The tracks are general now, but part of
the model still treats the **first track that is not a group bus** (`patternTrackOf` in `native/src/SessionInternal.cpp`)
or index 0 as that pattern track. Nothing on screen says so, which is what makes it a trap.

## What still depends on it (as of 2026-10-03)

- **The note editor's clip.** `Session::pattern()` dereferences `patternClip`. A new document's `Pattern 1` is a hidden
  placeholder MIDI clip on track 0 (shown only once it has notes), so the note editor works from the first click.
  `repairPatternClip` re-points the editor when its clip is deleted, and `ensureEditablePatternClip` falls back to the
  first MIDI clip on the pattern track after undo, redo and load ([Note editor (StepGrid)](note-editor.md)).
- **`rhinoPatternInstrument`**, an edit property (`synth`, `drums`, `wave` or `forge`). Dropping an instrument, a
  pattern or a MIDI clip on track 0 writes it, and `restoreProject` switches the first track to it on every reopen
  ([Pattern presets](pattern-presets.md)).
- **Opening a project.** `restoreProject` refuses a document whose pattern track lacks a MIDI clip or a Utility
  ("The project is missing its pattern track devices.").
- **The rack.** On track 0, `deviceSlots` hides built-in instruments other than the one `rhinoPatternInstrument`
  names, and `deleteDevice` refuses to remove its Utility, 4OSC or Drums ("Core devices stay in the starter track
  chain.").
- **Positional pointers.** `utility` and `audioUtility` point at the Utility devices of the first two non-bus tracks
  and are re-read by `refreshUtilityPointers` whenever tracks appear, move or go.

## Why it bites

- `patternTrackOf` skips group buses because an instrument switched onto a bus replaces the sum of everything feeding
  it, so a group at the top of the stack would have silenced the project.
- A guarded read of `tracks[1]` beside an unguarded write crashed every one-track project on open (fixed 2026-09-16;
  [Debugging a crash only one project triggers](debugging-a-crashing-project.md)).
- Reordering the stack moves which track is "first" without telling the bookkeeping: a saved file may then fail the
  open check above, or reopen with a different instrument on its first track. Both are traced in the code, not yet
  reproduced ([Hazards found while seeding the wiki](known-hazards.md)).
- A new catalog instrument is not enough on its own: `restoreProject` knows only `wave`, `forge` and `drums` (4OSC for
  anything else), so a new instrument on the first track reopens as a 4OSC ([Adding a device to Rhino](adding-a-device.md)).

## How to work with it

Test any change that touches tracks with a stack whose first track has been reordered, with a group bus at the top,
and with a single track. Do not add more state keyed on position. Rhino owes old documents nothing
([No backward compatibility for .rhinoedit](no-rhinoedit-back-compat.md)), so retiring this bookkeeping is a matter of
making the current save and load paths stop needing it.

## Related

- [Session, the model](session-model.md)
- [Project files (.rhinoedit)](project-files.md)
- [Pattern presets](pattern-presets.md)
- [Track kinds: audio and MIDI](track-kinds.md)
- [Hazards found while seeding the wiki](known-hazards.md)
