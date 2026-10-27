---
title: A long command runs in slices behind a modal screen
type: decision
summary: A command that must render the live document (Bounce to New Track and Export WAV, so far) stays on the message thread but works a slice per frame behind the shell's modal LoadingScreen, so the window draws and can cancel while nothing can edit the document under it.
tags: [rhino, threads, rendering, ui]
sources: []
updated: 2026-10-27
---

# A long command runs in slices behind a modal screen

## Context

[Bounce to New Track](bounce-to-new-track.md) first rendered in one loop on the message thread (commit `fcaa581`), so the window froze for the whole render. A worker cannot render the live edit: the message thread edits it meanwhile, and that is a race. The DJ deck's old bounce took the other way out, a worker against a copy of the document, with the costs its page lists ([A deck's bounce renders on a worker against a copy of the document](dj-bounce-on-a-copy.md)).

## The choice

Since commit `d6f53ba` (2026-10-14) the bounce stays on the message thread but is stepped: `Session::continueBounce(ms)` renders for about that long and returns, and the shell's `LoadingScreen` calls it once a frame, 24 ms of every 33 ([App shell and control bar](app-shell.md#the-loading-screen)). Slicing is natural because one `te::Renderer::RenderTask::runJob` call renders one block (`NodeRenderContext::renderNextBlock`, Tracktion at `4536d8a`).

Export WAV followed in commit `b6758b4` (2026-10-15): `Session::startExport`/`continueExport`/`cancelExport` in `SessionExport.cpp`, the same shape as the bounce ([Project files (.rhinoedit)](project-files.md#wav-export)). Until then it rendered the live edit on a `ProjectFiles` worker while the window was merely disabled, the very race this decision avoids.

Safety comes from nothing being able to edit while it runs:

- the screen is modal, so no component, title-bar menu or other window takes input, and every key but Esc is swallowed;
- the Push's `commandsAllowed` is false while it runs;
- in the model, `Session::undo`/`redo` do nothing while `isBouncing() || isExporting()`, `newProject`, `restoreProject` and `~Session` call `cancelBounce()` and `cancelExport()` first, and neither render starts while either runs;
- closing the window (`MainComponent::requestClose`) cancels both before asking about unsaved changes.

## Consequences

- The bounce keeps `te::Clip*` and track indices from one slice to the next, and the export a render task over the edit, an exception to [Hold ids, not pointers](ids-not-pointers.md) that holds only while every route above stays blocked. The model guards only undo, redo, replacing the document and the other render, so a new route that can edit meanwhile (a timer, another thread's callback) must ask `Session::isBouncing()` and `isExporting()`.
- Cancel and failure leave the document as it was: the bounce's edit is made only after the last job has rendered, in one undo step, and the files written are deleted; the export writes a `juce::TemporaryFile` beside the destination and swaps it in only once the render has finished, so the file that was there survives.
- A render that keeps returning without progress holds the screen, not a frozen window, until the 15 s stall budget, and Cancel works meanwhile. The export took the bounce's 15 s (`exportStallSeconds`) over the worker's old 60 s for that reason: the person can cancel, so a long wait buys nothing. A step that blocks inside one `runJob` call still freezes the window ([Offline renders that never return](renders-that-never-return.md)).
- The work has about three quarters of each frame, so it takes somewhat longer than the blocking loop did (by the budget; not measured).
- Not applied yet (2026-10-15): Ctrl+J merge (`SessionMerge.cpp`) still renders in one blocking loop, as does [Slice to New MIDI Track](slice-to-new-midi-track.md) (`SessionSliceToMidi.cpp`, 2026-10-27, which refuses to start while a bounce or export runs), and opening a project still only disables `MainComponent` (`ProjectFiles::loadingChanged`, now its only use).

## Related

- [Bounce to New Track](bounce-to-new-track.md)
- [App shell and control bar](app-shell.md)
- [Session, the model](session-model.md)
- [Project files (.rhinoedit)](project-files.md)
