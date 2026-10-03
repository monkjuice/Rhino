---
title: Content library
type: component
summary: Samples live as files under library/, found at runtime by ContentLibrary, stored with Git LFS and never compiled in.
tags: [rhino, library, content]
sources: []
updated: 2026-10-03
---

# Content library

Rhino's content is files under `library/` at the repository root, located at runtime by `native/src/core/ContentLibrary.*`. Nothing there is compiled; why is the decision [Content is files, never compiled in](content-is-files.md). `ContentLibrary` sits in `RhinoCore` and knows nothing of `Session`, the UI or the engine, so the app (the browser) and the device library (`DrumDevice`) can both use it without depending on each other ([Rhino's build targets](rhino-build-targets.md)).

## Finding the root

`ContentLibrary::root()` resolves once per process and logs the answer, or every path it tried, to `rhino.log`. Candidates, in order:

1. the `RHINO_LIBRARY_DIR` environment variable, for a packaged build or a test;
2. a `Library` folder beside the executable (on macOS also the bundle's `Resources/Library`), for an installed build;
3. the repository's `library/`, found as the sibling of the compile-time `RHINO_SOURCE_DIR` (which is `native/`), for a development build.

A candidate counts only if it contains a `Samples` folder. Otherwise an empty `Library` beside a development executable would shadow the real one and silence every device.

## Layout

`Samples/<pack>/<group>/<file>`, with one level of grouping or loose files in a small pack. `samples()` scans once on first use for `.wav`, `.flac`, `.aif`, `.aiff`, `.ogg` and `.mp3`, ignoring the `SOURCE.md` and licence files kept beside the audio, and sorts by pack, group and name. The browser splits camel case for display ("VinylDrums" reads "Vinyl Drums"). Packs as of 2026-10-03: `Cumbia`, `HandClap`, `TR808`, `VinylDrums`. Presets and patterns are meant to join them; today the pattern presets are still compiled tables in `SessionPatches.cpp`.

## Rules

- **Git LFS for audio.** `.gitattributes` routes every audio extension under `library/` through LFS, set up before the library had any history. Set LFS up *before* adding a new content type: converting afterwards means rewriting history ([Git workflow](git-workflow.md)).
- **Read in `initialise()`, never in the audio callback, and survive absence.** Content is data on disk and can be missing; `ContentLibrary::file` returns a non-existent `File` then, and `DrumDevice::loadSample` logs `drum sample missing` and leaves that pad silent. `AGENTS.md` measures the TR-808 kit at 525 KB in 1.2 ms warm, once per device instance ([Real-time audio rules](real-time-audio-rules.md)).
- **The browser mirrors the scan.** `--self-test` asserts that the browser's sample rows equal `ContentLibrary::samples().size()`, which also holds with no library present.
- **An installed build needs `Library` beside the executable**; no build step or script copies it there yet (as of 2026-10-03).

## Related

- [Content is files, never compiled in](content-is-files.md)
- [Browser and library preview](browser.md)
- [Built-in devices](built-in-devices.md)
- [Rhino's build targets](rhino-build-targets.md)
- [Git workflow](git-workflow.md)
- [Pattern presets](pattern-presets.md)
