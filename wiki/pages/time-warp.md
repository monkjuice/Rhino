---
title: Time warp
type: component
summary: Makes an audio clip follow the song's tempo, with five warp modes over two stretchers, a clip tempo and warp markers.
tags: [rhino, audio-clips, tempo]
sources: []
updated: 2026-10-26
---

# Time warp

An audio clip that follows the song's tempo instead of its recorded speed. Model `native/src/model/SessionWarp.cpp`, UI `ui/audioclip/AudioClipWarp.cpp` (strip and markers in the [audio clip editor](audio-clip-editor.md)), tests `ClipWarp.inc`. The engine had the parts (`setAutoTempo`, `setTimeStretchMode`, `WarpTimeManager`); Rhino adds Live's vocabulary, a clip tempo that rescales the clip, and single-step undo for marker edits.

## Rhino's properties, the engine's state derived

The switch and the mode are Rhino's own clip properties, `rhinoWarpOn` and `rhinoWarpMode` (default Beats), and `Session::applyWarpState` is the one place the engine's state is written from them; it ends in `Session::applyStretchPlayback`, which pitch edits and pastes call too. The reason is Repitch: four modes are auto-tempo plus a stretcher, but the engine refuses a disabled stretcher while auto-tempo is on (`getActualTimeStretchMode` substitutes the default). So a warped Repitch clip is a plain speed ratio, song tempo over clip tempo, recomputed by `Session::updateRepitchedClips` whenever `setTempo` runs, as a record player would.

| Mode | For | Engine |
|---|---|---|
| Repitch | speed changes the pitch | speed ratio, no stretcher |
| Beats | drums and loops | SoundTouch, normal (starts early, below) |
| Tones | melody and voice | Signalsmith, cheaper |
| Texture | pads and ambience | SoundTouch, better (starts early, below) |
| Complex | a whole mix | Signalsmith, default |

Each mode is a different algorithm, not a label. Both stretchers are compiled in (`TRACKTION_ENABLE_TIMESTRETCH_*` in `native/CMakeLists.txt`); Elastique is licensed separately and absent. The mode applies only while warp is on: an unwarped clip's stretch mode is `disabled` whatever mode is stored, so its pitch goes through the engine's default, Signalsmith. Until commit `5c9c1e1` the mode's stretcher stayed on after warp was switched off, so a clip once warped in Beats pitched through SoundTouch.

## Decisions and traps

- **A stretched clip skips the proxy.** `applyStretchPlayback` turns the proxy off for any clip that is warped or pitched, so it stretches live instead of playing a rendered copy; only an unstretched clip keeps the engine's default. Until `5c9c1e1` this covered warped clips only (`setUsesProxy(!on)`). Every tempo change, marker drag or pitch step would invalidate that copy, leaving the clip silent or stale. Worse, the copy is rendered with no allowance for the stretcher's latency, so it plays about 120 ms late and loses its end to a hard cut ([Tracktion's time-stretched proxy plays late and cuts its end](tracktion-stretched-proxy-plays-late.md)). A proxy also made the first offline render of a warped clip never finish ([Offline renders that never return](renders-that-never-return.md)). The cost is one live stretcher per stretched clip while the transport rolls.
- **Clip tempo is length in beats.** Raising it makes the clip longer, the direction that fixes a loop detected an octave out (**:2** and **\*2**). `rescaleWarpedClip` keeps a trimmed clip trimmed by the same fraction. Since `a1c9607` it also retimes the clip's envelope points by the same content ratio ([Clip envelopes](clip-envelopes.md)). **Detect** blocks the message thread on a file read. Every warp edit that can lengthen a clip (the switch, the mode, the clip tempo, detect) then runs `makeRoomForClip`, since commit `b5d17e0` ([Clips never overlap](clip-placement.md)).
- **A song tempo change cannot make a Repitch clip overlap.** Its speed is song tempo over clip tempo, so it keeps its length in beats, and the engine keeps the next clip on its beats as well. A review claimed otherwise; `ClipWarp.inc` now pins it (commit `6eaa5ba`).
- **Speed is not `getSpeedRatio`.** The engine leaves that at 1 for an auto-tempo clip, so drawing code asks `Session::clipPlaybackSpeed`; reading the ratio drew half the file across the whole clip.
- **Markers span the whole file.** The engine seeds one at each end and straightens rather than deletes them, and maps linearly between markers. A new marker snaps to an attack within 50 ms; Repitch takes none. The editor draws the warp exactly, segment by segment; the arrangement draws one speed, so after marker drags it is locally out by up to the drag distance.

Live's per-mode parameters are not offered: these stretchers expose no such knobs.

## Open: Beats and Texture start early and click

Measured on 2026-10-26 by `ClipPitch.inc`'s method (render, then find onsets and sample steps). SoundTouch, Beats (`soundtouchNormal`) and Texture (`soundtouchBetter`), plays early. A clip pitched and streamed through it lands its hits 13-21 ms early, with jitter. Warped at the clip's own tempo, Beats runs about 9 ms early and Texture about 13 ms. In every case the clip begins partway into its first attack, a sample step of 0.42 at its start, which clicks on entry, pitched or not. SoundTouch's output leads its input, and nothing lines it back up: Tracktion's SoundTouch stretcher does not override `getLatencySamples` (it reports 0), so the live path's compensation does nothing for it. Why SoundTouch leads was not traced. Signalsmith, Tones and Complex, lands within about 2 ms with clean edges. Unwarped clips no longer reach SoundTouch (`5c9c1e1`), but warped Beats and Texture clips still do. The fix is unbuilt, and which algorithm Beats and Texture should use is an open product decision.

## Related

- [Audio clip editor](audio-clip-editor.md)
- [Arrangement view](arrangement-view.md)
- [Offline renders that never return](renders-that-never-return.md)
- [Tracktion's time-stretched proxy plays late and cuts its end](tracktion-stretched-proxy-plays-late.md)
- [Tracktion Engine with a native JUCE UI](tracktion-and-juce.md)
- [Transport, tempo and loop](transport.md)
