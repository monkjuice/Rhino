---
title: Time warp
type: component
summary: Makes an audio clip follow the song's tempo, with five warp modes over two stretchers, a clip tempo and warp markers.
tags: [rhino, audio-clips, tempo]
sources: []
updated: 2026-10-05
---

# Time warp

An audio clip that follows the song's tempo instead of its recorded speed. Model `native/src/SessionWarp.cpp`, UI `AudioClipWarp.cpp` (strip and markers in the [audio clip editor](audio-clip-editor.md)), tests `ClipWarp.inc`. The engine had the parts (`setAutoTempo`, `setTimeStretchMode`, `WarpTimeManager`); Rhino adds Live's vocabulary, a clip tempo that rescales the clip, and single-step undo for marker edits.

## Rhino's properties, the engine's state derived

The switch and the mode are Rhino's own clip properties, `rhinoWarpOn` and `rhinoWarpMode` (default Beats), and `Session::applyWarpState` is the one place the engine's state is written from them. The reason is Repitch: four modes are auto-tempo plus a stretcher, but the engine refuses a disabled stretcher while auto-tempo is on (`getActualTimeStretchMode` substitutes the default). So a warped Repitch clip is a plain speed ratio, song tempo over clip tempo, recomputed by `Session::updateRepitchedClips` whenever `setTempo` runs, as a record player would.

| Mode | For | Engine |
|---|---|---|
| Repitch | speed changes the pitch | speed ratio, no stretcher |
| Beats | drums and loops | SoundTouch, normal |
| Tones | melody and voice | Signalsmith, cheaper |
| Texture | pads and ambience | SoundTouch, better |
| Complex | a whole mix | Signalsmith, default |

Each mode is a different algorithm, not a label. Both stretchers are compiled in (`TRACKTION_ENABLE_TIMESTRETCH_*` in `native/CMakeLists.txt`); Elastique is licensed separately and absent.

## Decisions and traps

- **A warped clip skips the proxy.** `applyWarpState` calls `setUsesProxy(!on)`, so only while warping is on does a clip stretch live instead of playing a rendered copy; an unwarped clip keeps the engine's default. Every tempo change and marker drag would invalidate that copy, leaving the clip silent or stale. A proxy also made the first offline render of a warped clip never finish ([Offline renders that never return](renders-that-never-return.md)).
- **Clip tempo is length in beats.** Raising it makes the clip longer, the direction that fixes a loop detected an octave out (**:2** and **\*2**). `rescaleWarpedClip` keeps a trimmed clip trimmed by the same fraction. **Detect** blocks the message thread on a file read. Every warp edit that can lengthen a clip (the switch, the mode, the clip tempo, detect) then runs `makeRoomForClip`, since commit `b5d17e0` ([Clips never overlap](clip-placement.md)).
- **A song tempo change cannot make a Repitch clip overlap.** Its speed is song tempo over clip tempo, so it keeps its length in beats, and the engine keeps the next clip on its beats as well. A review claimed otherwise; `ClipWarp.inc` now pins it (commit `6eaa5ba`).
- **Speed is not `getSpeedRatio`.** The engine leaves that at 1 for an auto-tempo clip, so drawing code asks `Session::clipPlaybackSpeed`; reading the ratio drew half the file across the whole clip.
- **Markers span the whole file.** The engine seeds one at each end and straightens rather than deletes them, and maps linearly between markers. A new marker snaps to an attack within 50 ms; Repitch takes none. The editor draws the warp exactly, segment by segment; the arrangement draws one speed, so after marker drags it is locally out by up to the drag distance.

Live's per-mode parameters are not offered: these stretchers expose no such knobs.

## Related

- [Audio clip editor](audio-clip-editor.md)
- [Arrangement view](arrangement-view.md)
- [Offline renders that never return](renders-that-never-return.md)
- [Tracktion Engine with a native JUCE UI](tracktion-and-juce.md)
- [Transport, tempo and loop](transport.md)
