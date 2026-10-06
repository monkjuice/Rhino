---
title: Pattern presets
type: component
summary: The eleven built-in one-bar patterns, each a set of notes plus the instrument and patch that play them, and what dropping one does to a track.
tags: [rhino, presets, browser]
sources: []
updated: 2026-10-06
---

# Pattern presets

The browser's **Patterns** section offers eleven one-bar patterns, grouped Synth, Bass, Lead and Drums
(`native/src/BrowserPanel.cpp`). Each is a `Session::PatternPreset`. The model side is two files:
`native/src/SessionPatches.cpp` holds the note tables (`presetPattern`) and the 4OSC patches (`applySynthPatch`);
`native/src/SessionPresets.cpp` puts them on a track. The three Wave presets (pad, bass, pluck) left with Rhino Wave
on 2026-10-05 ([Built-in devices](built-in-devices.md)). A device's `.rnd` presets are a different thing, in
`SessionDevicePresets.cpp` ([Device presets (.rnd)](device-presets.md)).

## A pattern is notes plus the sound that plays them

A `PresetPattern` (declared in `SessionInternal.h`) carries the notes, a name, and which instrument plays them: the
Drum Rack (`useDrums`), or 4OSC with one of its patches. So dropping a pattern on a lane does two things at once.

1. `preparePresetTrack` readies the track. It refuses an audio track ("Drop patterns on a MIDI track instead"), then
   switches the track's instrument through `switchTrackInstrument` — replacing whatever was there, since a track has
   [one instrument](one-instrument-per-track.md) — and applies the pattern's patch to it.
2. `insertPatternPreset` inserts a one-bar MIDI clip at the drop position, fills it with the notes, and runs
   `makeRoomForClip`, so the new clip wins the ground it lands on ([Clips never overlap](clip-placement.md)).

The clip-slot path in the session view (`insertPatternPresetInSlot`) calls the same `preparePresetTrack`, so the two
cannot drift apart. The cost is that dropping a pattern into one slot changes the instrument for every clip on that
track.

A drum pattern switches the track to the [Drum Rack](drum-rack.md). A rack is blank when made, and a blank rack would
play nothing, so `preparePresetTrack` loads `Drums/Kits/808 Kit.rdk` into it (`loadDefaultDrumKit`, in
`SessionDrums.cpp`). That is the kit the patterns were written for: kick on C2, toms on D2, E2 and F#2, snare on F2,
clap on G#2, hats on A#2 and B2. A rack that already holds sounds keeps them. The other kits are files under Drums >
Kits in the browser.

## Loading into the open clip

`applyPatternPreset` loads a preset into the clip the note editor has open, and its sound onto that clip's own track,
through the same `preparePresetTrack`: it does exactly what a drop on that lane does. No track is special for being
first. The pattern-track bookkeeping that once made track 0 the preset's home, including the edit's
`rhinoPatternInstrument` property, was retired in commit `a04b407`.

## Not yet content files

Samples, device presets and drum kits live in the content library, but patterns are still compiled in. `AGENTS.md` expects presets and patterns to
move into `library/` later ([Content is files, never compiled in](content-is-files.md)). Until they do, adding a
pattern means a note table in `SessionPatches.cpp`, a `PatternPreset` value, and a browser row.

## Related

- [Browser and library preview](browser.md)
- [Note editor (StepGrid)](note-editor.md)
- [One instrument per track](one-instrument-per-track.md)
- [Track kinds: audio and MIDI](track-kinds.md)
- [Built-in devices](built-in-devices.md)
- [Device presets (.rnd)](device-presets.md)
- [Drum Rack](drum-rack.md)
