---
title: Tracktion's time-stretched proxy plays late and cuts its end
type: gotcha
summary: Tracktion renders a stretched clip's proxy copy without allowing for the stretcher's latency, so a pitched clip played every hit about 120 ms late and lost its last 120 ms to a hard cut, a click at its end and every loop wrap; since 5c9c1e1 every stretched clip streams through a live stretcher and an unwarped one pitches through Signalsmith.
tags: [rhino, tracktion, audio-clips, rendering, measurement]
sources: []
updated: 2026-10-26
---

# Tracktion's time-stretched proxy plays late and cuts its end

Measured on 2026-10-26 by rendering, then explained by reading the pinned engine (`4536d8a`). Fixed in commit `5c9c1e1`.

## Symptom

A drum clip and a bass clip bounced with Ctrl+B, then given Pitch in the [audio clip editor](audio-clip-editor.md), clicked "when it enters the clip", most on the drums, and seemed to change tempo. The tempo had not changed: the hits stayed 0.5 s apart, and every one of them was about 120 ms late.

## Cause

`AudioClipBase::usesTimeStretchedProxy` is true for any `getPitchChange() != 0`, so an unwarped pitched clip played a rendered copy of itself (a `clip_` file, [An edit's project id names its temp folder](edit-temp-folder.md)). `StretchSegment` in `tracktion_AudioClipBase.cpp` renders that copy through the stretcher without allowing for the stretcher's latency. The clip's elastiqueMode was `disabled`, so the stretcher was the default, Signalsmith. The result:

- every hit in the copy plays about 120 ms late (118-120 ms measured at 48 kHz, a sixteenth at 125 BPM);
- the last ~120 ms fall off the copy's end, cut hard (a sample step of 0.54), which clicks wherever the clip ends or the loop wraps back into it;
- the clip is silent while the copy renders (a 600 ms timer, then a job).

The live path does compensate the latency (`TimeStretchReader::primeStretcher` under `WaveNodeRealTime`, `tracktion_WaveNode.cpp`): the same clip streamed lands within ~2 ms, with clean edges.

## Fix

`Session::applyStretchPlayback` (`SessionWarp.cpp`) runs wherever a clip's warp or pitch is written: `applyWarpState`, `setAudioClipPitch` and the snapshot paste in `pasteClipRegion`, which duplicate also uses. It sets an unwarped clip's stretch mode back to `disabled`, so its pitch goes through Signalsmith whatever warp mode is stored. It also calls `setUsesProxy(false)` on every stretched clip, warped or pitched, and allows the proxy again on an unstretched one. `proxyAllowed` is a `CachedValue` on the clip's state, so it is saved, undone and copied with the clip (a reopened document was checked). Only a path that rebuilds a clip from a snapshot must call the function. The cost is one live stretcher per stretched clip while the transport rolls, which Live pays too. Nothing applies it when a document opens, so a clip pitched before `5c9c1e1` keeps its proxy until its pitch or warp is written again ([No backward compatibility for .rhinoedit](no-rhinoedit-back-compat.md)).

## Still open: SoundTouch starts early

SoundTouch (Beats and Texture) has the opposite fault and is not fixed: its clips start early and click on entry, pitched or not. See [Time warp](time-warp.md).

## How it was measured

`ClipPitch.inc` in the arrangement runner, right after `ClipWarp.inc` (it renders, so it comes before `GesturesAndPersistence.inc`). Its fixture is 2 s at 48 kHz: 150 Hz hits with sharp attacks on each beat at 120 BPM, plus a short hit a sixteenth before the end. It renders around the clip at block size 480 ([Reproducing a live timing bug offline](reproducing-live-timing-offline.md)) and requires:

- onsets within 3 ms of the unpitched clip's;
- the largest sample-to-sample step within 5 ms of the clip's start and end under 0.05;
- a DFT bin that puts the second hit a minor third up;
- `canUseProxy()` false, checked before the render, since an offline render waits for good on a proxy that only the message loop makes ([Offline renders that never return](renders-that-never-return.md)).

It covers a fresh pitched clip, a clip warped in Beats, unwarped and then pitched, a duplicate, and the document written to disk and reopened. It fails without the fix.

## Related

- [Time warp](time-warp.md)
- [Audio clip editor](audio-clip-editor.md)
- [Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)
- [Offline renders that never return](renders-that-never-return.md)
- [Bounce to New Track](bounce-to-new-track.md)
- [Tracktion's pitch shifter under-reports its latency](tracktion-pitch-shifter-under-reports-latency.md)
