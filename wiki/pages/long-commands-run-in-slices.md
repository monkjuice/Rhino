---
title: A long command runs in slices behind a modal screen
type: decision
summary: A command that must render the live document (Bounce to New Track, so far) stays on the message thread but works a slice per frame behind the shell's modal LoadingScreen, so the window draws and can cancel while nothing can edit the document under it.
tags: [rhino, threads, rendering, ui]
sources: []
updated: 2026-10-14
---

# A long command runs in slices behind a modal screen

## Context

[Bounce to New Track](bounce-to-new-track.md) first rendered in one loop on the message thread (commit `fcaa581`), so the window froze for the whole render. A worker cannot render the live edit: the message thread edits it meanwhile, and that is a race. The DJ deck's old bounce took the other way out, a worker against a copy of the document, with the costs its page lists ([A deck's bounce renders on a worker against a copy of the document](dj-bounce-on-a-copy.md)).

## The choice

Since commit `d6f53ba` (2026-10-14) the bounce stays on the message thread but is stepped: `Session::continueBounce(ms)` renders for about that long and returns, and the shell's `LoadingScreen` calls it once a frame, 24 ms of every 33 ([App shell and control bar](app-shell.md#the-loading-screen)). Slicing is natural because one `te::Renderer::RenderTask::runJob` call renders one block (`NodeRenderContext::renderNextBlock`, Tracktion at `4536d8a`).

Safety comes from nothing being able to edit while it runs:

- the screen is modal, so no component, title-bar menu or other window takes input, and every key but Esc is swallowed;
- the Push's `commandsAllowed` is false while it runs;
- in the model, `Session::undo`/`redo` do nothing while `isBouncing()`, and `newProject`, `restoreProject` and `~Session` call `cancelBounce()` first;
- closing the window (`MainComponent::requestClose`) cancels before asking about unsaved changes.

## Consequences

- The run keeps `te::Clip*` and track indices from one slice to the next, an exception to [Hold ids, not pointers](ids-not-pointers.md) that holds only while every route above stays blocked. The model guards only undo, redo and replacing the document, so a new route that can edit while a bounce runs (a timer, another thread's callback) must ask `Session::isBouncing()`.
- Cancel and failure leave the document as it was: the edit is made only after the last job has rendered, in one undo step, and the files written are deleted.
- A render that keeps returning without progress holds the screen, not a frozen window, until the 15 s stall budget, and Cancel works meanwhile. A step that blocks inside one `runJob` call still freezes the window ([Offline renders that never return](renders-that-never-return.md)).
- The work has about three quarters of each frame, so it takes somewhat longer than the blocking loop did (by the budget; not measured).
- Not applied yet (2026-10-14): Ctrl+J merge (`SessionMerge.cpp`) still renders in one blocking loop, and opening a project and the WAV export still only disable `MainComponent` (`ProjectFiles::loadingChanged`); the export renders the live edit on a worker meanwhile.

## Related

- [Bounce to New Track](bounce-to-new-track.md)
- [App shell and control bar](app-shell.md)
- [Session, the model](session-model.md)
- [Project files (.rhinoedit)](project-files.md)
