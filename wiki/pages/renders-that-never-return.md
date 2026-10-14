---
title: Offline renders that never return
type: gotcha
summary: Five known causes make an offline render hang for good, a clip whose file is missing among them, and the Moved render timeout failure is usually just a busy machine.
tags: [rhino, testing, rendering]
sources: []
updated: 2026-10-14
---

# Offline renders that never return

An offline render is a `te::Renderer::RenderTask`, driven by calling `runJob()` until it returns `jobHasFinished`. Five known causes stop it from ever finishing. A sixth error only looks like a hang.

## Symptom

A workflow case stops with no failed assertion, and CTest kills it at its time limit. The limit is 120 s for `native_pattern_workflow` and `native_arrangement_workflow`, and `native/CMakeLists.txt` says it is there to catch exactly this. In the app, Ctrl+J merge loops on the message thread (`SessionMerge.cpp`) with no time limit, so there the same fault is a window that stops responding. Ctrl+B [Bounce to New Track](bounce-to-new-track.md) renders on the message thread too, but a slice per frame behind the shell's loading screen (2026-10-14), so a stalled bounce holds that screen, still drawing and cancellable, and gives up once the render has made no progress for 15 s (`bounceStallSeconds`). The WAV export loops on a worker thread (`ProjectFiles.cpp`) and gives up after 60 s without progress (`Session::renderTimelineWav`), where it used to run for good. Both stall budgets are one guard in `SessionInternal.cpp`: `stopRenderWhenStalled(task, progress, seconds)` arms the task's own cancellation check and returns a shared `stalled` flag, and `renderTaskOutcome(task, stalled, seconds)` turns the finished task into a `juce::Result`. Cancelling that way lets the engine close and delete the partial file. The bounce calls the two around its slices; `runRenderTask` (the export) is the two around one loop. Only a render that keeps returning is caught: one that blocks inside a single `runJob` call freezes the bounce's window as before. A DJ deck's bounce had a 60 s budget until decks stopped bouncing on 2026-10-12 ([superseded](dj-bounce-on-a-copy.md)).

## Known causes

1. **The audio device is closed.** `GesturesAndPersistence.inc` calls `Session::releaseAudioDevice`, and no render after that point ever finishes. Anything that renders must be included before it in the runner. `GroupBusRouting.inc`, `GroupBusReload.inc`, `ClipWarp.inc` and `ClipBounce.inc` sit where they do for this reason ([Writing Rhino tests](writing-rhino-tests.md)).
2. **A deleted group bus.** When a bus is deleted, its members still name the missing track. `getDestinationTrack()` returns null, but the engine still tries to resolve that destination on every render. So `reconcileTrackGroups` in `SessionGroups.cpp` asks `usesDefaultAudioOut()` before sending a track back to the main output ([Track groups (bus tracks)](track-groups.md)).
3. **A warp proxy.** With proxies on, the engine renders a stretched copy of each warped clip. The first offline render of a warped clip waited forever on a proxy that needed the message thread, which the render was blocking. `SessionWarp.cpp` now turns the proxy off for warped clips with `clip.setUsesProxy(!on)` ([Time warp](time-warp.md)).

4. **A clip imported onto a track added late in the arrangement runner.** Found on 2026-10-09 while the DJ booth's scenario was placed after `GroupBusReload.inc`: from the point that scenario restores its entry snapshot, a clip imported onto a *newly added* track never renders, and while it exists in the edit no render of any clip returns; remove the track and renders return. A clip imported onto an existing track renders, a fresh `Session` renders both, and the booth has nothing to do with it (the probe hung before a deck existed). The cause was not found; the suspicion was an item id reissued after the restore. A likelier one was found on 2026-10-11 by a probe in the arrangement runner, and fits this hang without having been run against it. The runners reopen documents from a `juce::TemporaryFile` path that is never written, as `GroupBusReload.inc` does. A file imported after such a reopen is stored relative to that path as if it were a folder (`..\name.wav`: JUCE's `getRelativePathFrom` takes the folder only of a file that exists), and the engine's default resolver reads the path as the file's sibling, one folder too high, so the clip names a file that does not exist. An offline render then waits on that clip's wave node for good: `WaveNode::isReadyToProcess` (read at `4536d8a`) returns false while it cannot open a reader, unless the path is empty. That is cause 5 below, reached through the runners' own path. A scenario that imports onto a new track and renders it belongs before `GroupBusReload.inc`; one that needs a reopened document to render writes the document to disk first, as `DjBounces.inc` did until it went with the bounces on 2026-10-12 ([A deck's bounce renders on a worker against a copy of the document](dj-bounce-on-a-copy.md)).
5. **A clip whose audio file is missing.** The render's leaf nodes must all be ready before a block is rendered, and a wave node whose file names something that cannot be opened is never ready (`WaveNode::isReadyToProcess` and `WaveNodeRealTime`'s at `4536d8a`), muted clip or not; a clip whose file is empty has no node at all and is skipped. A sample deleted or moved does it, and so, since 2026-10-11, does a document moved away from the samples stored beside it: a source stored relative to the document used to resolve to no file in a reopened document and in a deck's copy, and now resolves against the document's file ([A deck's bounce renders on a worker against a copy of the document](dj-bounce-on-a-copy.md)). A Ctrl+J merge and a Ctrl+B bounce ask `checkClipFile` (`SessionInternal.h`) of every clip they will render and refuse at once, naming the file; `DjBounces.inc`, which checked that and the DJ bounce's same refusal, went with the bounces on 2026-10-12. A DJ deck asks it of an audio cell before reading the file, though no render is involved. The WAV export asks it of every timeline clip before starting (`Session::checkTimelineFiles`, in the working tree of 2026-10-12 and recorded here only as far as this page needs; it used to run for good, with save and open refused while busy). The check asks only whether the file is there: a file that is there and cannot be read holds a render the same way, and the engine's own answer cannot be trusted for it, since `AudioFileManager::getInfo` (read at `4536d8a`) answers from what it read the first time and does not look at the file again.

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
- [Bounce to New Track](bounce-to-new-track.md)
- [A long command runs in slices behind a modal screen](long-commands-run-in-slices.md)
- [A track that feeds a bus renders nothing on its own](bus-member-renders-nothing-alone.md)
