---
title: Pattern presets
type: component
summary: The fourteen built-in one-bar patterns, each a set of notes plus the instrument and patch that play them, and what dropping one does to a track.
tags: [rhino, presets, browser]
sources: []
updated: 2026-10-03
---

# Pattern presets

The browser's **Patterns** section offers fourteen one-bar patterns, grouped Synth, Bass, Lead, Pad and Drums
(`native/src/BrowserPanel.cpp`). Each is a `Session::PatternPreset`. The model side is two files:
`native/src/SessionPatches.cpp` holds the note tables (`presetPattern`), the 4OSC patches (`applySynthPatch`) and the
Rhino Wave patches (`applyRhinoWavePatch`); `native/src/SessionPresets.cpp` puts them on a track.

## A pattern is notes plus the sound that plays them

A `PresetPattern` (declared in `SessionInternal.h`) carries the notes, a name, and which instrument plays them: Rhino
Drums, Rhino Wave, or 4OSC with one of its patches. So dropping a pattern on a lane does two things at once.

1. `preparePresetTrack` readies the track. It refuses an audio track ("Drop patterns on a MIDI track instead"), then
   switches the track's instrument through `switchTrackInstrument` — replacing whatever was there, since a track has
   [one instrument](one-instrument-per-track.md) — and applies the pattern's patch to it.
2. `insertPatternPreset` inserts a one-bar MIDI clip at the drop position, fills it with the notes, and runs
   `makeRoomForClip`, so the new clip wins the ground it lands on ([Clips never overlap](clip-placement.md)).

The clip-slot path in the session view (`insertPatternPresetInSlot`) calls the same `preparePresetTrack`, so the two
cannot drift apart. The cost is that dropping a pattern into one slot changes the instrument for every clip on that
track.

A drum pattern switches the track to Rhino Drums but does not pick a kit. A fresh drum device starts on the TR-808 kit,
and an existing one keeps its kit. The five kits are separate drops under Instruments / Drum Rack, each tuned for one
pattern ([Built-in devices](built-in-devices.md)).

## The pattern track is a leftover

Rhino began as one pattern on one track, and some of that remains. `applyPatternPreset` loads a preset into
`Pattern 1` on track 0, and `preparePresetTrack` treats track 0 specially. The edit carries a `rhinoPatternInstrument`
property (`synth`, `drums` or `wave`) for that track, and `patternTrackOf` finds it. `patternTrackOf` skips group
buses on purpose: an instrument on a bus replaces the sum of everything feeding it, so a group at the top of the
stack would turn a whole project silent ([Track groups (bus tracks)](track-groups.md)).

## Not yet content files

Samples live in the content library, but patterns are still compiled in. `AGENTS.md` expects presets and patterns to
move into `library/` later ([Content is files, never compiled in](content-is-files.md)). Until they do, adding a
pattern means a note table in `SessionPatches.cpp`, a `PatternPreset` value, and a browser row.

## Related

- [Browser and library preview](browser.md)
- [Note editor (StepGrid)](note-editor.md)
- [One instrument per track](one-instrument-per-track.md)
- [Track kinds: audio and MIDI](track-kinds.md)
- [Built-in devices](built-in-devices.md)
