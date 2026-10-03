---
title: The main track
type: concept
summary: The main output row is the edit's master plugin list, addressed as track index trackCount() so track-indexed calls need no sentinel.
tags: [rhino, mixer, tracks]
sources: []
updated: 2026-10-03
---

# The main track

The main track (`Main` on screen, *master* in the code) is not a `te::AudioTrack`. It wraps the edit's master plugin list and master volume plugin, and `Session` addresses it as **one past the last track**: `masterTrackIndex()` returns `trackCount()`, and `isMasterTrack(track)` compares against it.

## Why an index, not a sentinel

Every device, parameter and automation call takes a track index, and `DeviceTarget::isValid()` reads a negative index as "nothing". Giving the main row the next index lets all of them reach it with no second code path: `Session::pluginListForTrack` resolves that index to `edit->getMasterPluginList()` and every other index to the track's own list.

## What it takes

- **Audio effects only.** `addDevice` asks the engine's `canBeAddedToMaster()` for an audio effect; instruments and MIDI effects go to calls that accept only real track indices and are refused there, as are clips, audio files and patterns. A browser drop on it says "The main track takes audio effects only."
- **Level and pan**, from the edit's master volume plugin, the same in the arrangement and the session view. No mute, solo, arm or input: `trackRecordInput` answers none.
- It cannot be renamed, coloured, moved or removed; each attempt returns a message.
- Exported WAVs include its devices and fader because the render sets `useMasterPlugins` ([Project files (.rhinoedit)](project-files.md)).

## In the arrangement

The row is pinned under the scrolling lanes, so `laneContentHeight` excludes it and the timeline grid carries on through it. Clicking it opens its chain in the Device View. Its height is a view setting held by `Arrangement`, not the document: 28 px by default, dragged from its top edge between 26 and 120 px. Track header controls are children of `laneHeaders`, which crops them so a tall row never draws over the main row.

## Pitfalls

- **An index can silently mean main.** On a one-track document index 1 *is* the main row, so a hard-coded `1` targets main instead of failing; that is why `addAudioEffect` lost its `= 1` default (`HANDOVER.md`). Always name the track.
- **No automation lanes yet.** `showTrackAutomation` refuses a main-row target because the pinned row has nowhere to stack a lane, though lane storage (on the edit's own state) and playback already treat it like any track ([Track automation](automation.md)).

## Related

- [Mixer](mixer.md)
- [Track automation](automation.md)
- [Device chain order](device-chain-order.md)
- [Session, the model](session-model.md)
- [Arrangement view](arrangement-view.md)
