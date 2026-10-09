---
title: Offline renders that never return
type: gotcha
summary: Four known causes make an offline render hang for good, and the Moved render timeout failure is usually just a busy machine.
tags: [rhino, testing, rendering]
sources: []
updated: 2026-10-09
---

# Offline renders that never return

An offline render is a `te::Renderer::RenderTask`, driven by calling `runJob()` until it returns `jobHasFinished`. Four known causes stop it from ever finishing. A fifth error only looks like a hang.

## Symptom

A workflow case stops with no failed assertion, and CTest kills it at its time limit. The limit is 120 s for `native_pattern_workflow` and `native_arrangement_workflow`, and `native/CMakeLists.txt` says it is there to catch exactly this. In the app, nothing has a time limit. WAV export loops on a worker thread (`ProjectFiles.cpp`) and Ctrl+J merge loops on the message thread (`SessionMerge.cpp`). There, the same fault means an export that never completes or a window that stops responding. A DJ deck's bounce loops on the booth's worker with a 60 s budget (`SessionDjSources.cpp`), so there it means a deck that reports "did not finish in time" ([A deck's bounce renders on a worker against a copy of the document](dj-bounce-on-a-copy.md)).

## Known causes

1. **The audio device is closed.** `GesturesAndPersistence.inc` calls `Session::releaseAudioDevice`, and no render after that point ever finishes. Anything that renders must be included before it in the runner. `GroupBusRouting.inc`, `GroupBusReload.inc` and `ClipWarp.inc` sit where they do for this reason ([Writing Rhino tests](writing-rhino-tests.md)).
2. **A deleted group bus.** When a bus is deleted, its members still name the missing track. `getDestinationTrack()` returns null, but the engine still tries to resolve that destination on every render. So `reconcileTrackGroups` in `SessionGroups.cpp` asks `usesDefaultAudioOut()` before sending a track back to the main output ([Track groups (bus tracks)](track-groups.md)).
3. **A warp proxy.** With proxies on, the engine renders a stretched copy of each warped clip. The first offline render of a warped clip waited forever on a proxy that needed the message thread, which the render was blocking. `SessionWarp.cpp` now turns the proxy off for warped clips with `clip.setUsesProxy(!on)` ([Time warp](time-warp.md)).

4. **A clip imported onto a track added late in the arrangement runner.** Found on 2026-10-09 while the DJ booth's scenario was placed after `GroupBusReload.inc`: from the point that scenario restores its entry snapshot, a clip imported onto a *newly added* track never renders, and while it exists in the edit no render of any clip returns; remove the track and renders return. A clip imported onto an existing track renders, a fresh `Session` renders both, and the booth has nothing to do with it (the probe hung before a deck existed). The cause was not found; the suspicion is an item id reissued after the restore. `DjBooth.inc` therefore makes its group in a session of its own, and a scenario that imports onto a new track and renders it belongs before `GroupBusReload.inc`.

## The one that is only load

`"Moved render timeout"` comes from `renderClipRms` in `TrackManagement.inc`, which `GroupBusRouting.inc` also uses. It gives a render a 15 s wall-clock budget. Under load the budget runs out: a build finishing, headless `--startup-test` runs, or the user working. A minute later it passes. Re-run the case on its own before changing anything:

```
ctest --test-dir native/build -C Release -R native_arrangement_workflow --output-on-failure
```

Treat it as a regression only if it fails on an idle machine. The other scenario renders have the same kind of budget (15 s, or 20 s for the warp render), so treat their timeout messages the same way. The deadline is checked only between `runJob()` calls. A render that keeps returning unfinished fails with one of these messages; one that blocks inside a call runs until CTest's time limit.

## Related

- [Writing Rhino tests](writing-rhino-tests.md)
- [Build and test Rhino](build-and-test-rhino.md)
- [A group is an ordinary bus track](group-is-a-bus-track.md)
- [Time warp](time-warp.md)
- [Audio clip editor](audio-clip-editor.md)
- [Project files (.rhinoedit)](project-files.md)
- [DJ view and the booth](dj-view.md)
