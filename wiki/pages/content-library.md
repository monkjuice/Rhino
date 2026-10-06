---
title: Content library
type: component
summary: Samples, device presets, drum kits and drum presets live as files under library/, found at runtime by ContentLibrary, audio stored with Git LFS, nothing compiled in.
tags: [rhino, library, content]
sources: []
updated: 2026-10-06
---

# Content library

Rhino's content is files under `library/` at the repository root, located at runtime by `native/src/core/ContentLibrary.*`. Nothing there is compiled; why is the decision [Content is files, never compiled in](content-is-files.md). `ContentLibrary` sits in `RhinoCore` and knows nothing of `Session`, the UI or the engine, so the app (the browser) and the device library (the Drum Rack) can both use it without depending on each other ([Rhino's build targets](rhino-build-targets.md)).

## Finding the root

`ContentLibrary::root()` resolves once per process and logs the answer, or every path it tried, to `rhino.log`. Candidates, in order:

1. the `RHINO_LIBRARY_DIR` environment variable, for a packaged build or a test;
2. a `Library` folder beside the executable (on macOS also the bundle's `Resources/Library`), for an installed build;
3. the repository's `library/`, found as the sibling of the compile-time `RHINO_SOURCE_DIR` (which is `native/`), for a development build.

A candidate counts only if it contains a `Samples` folder. Otherwise an empty `Library` beside a development executable would shadow the real one and silence every device.

## Layout

`Samples/<pack>/<group>/<file>`, with one level of grouping or loose files in a small pack. `samples()` scans once on first use for `.wav`, `.flac`, `.aif`, `.aiff`, `.ogg` and `.mp3`, ignoring the `SOURCE.md` and licence files kept beside the audio, and sorts by pack, group and name. The browser splits camel case for display ("VinylDrums" reads "Vinyl Drums"). Packs as of 2026-10-03: `Cumbia`, `HandClap`, `TR808`, `VinylDrums`.

`Presets/<device id>/*.rnd` holds the factory device presets, and `presets()` lists them with the person's own from `userPresets()` (`Documents/Rhino/Presets`, or `RHINO_USER_PRESETS_DIR`). They are re-read on every call, unlike samples ([Device presets (.rnd)](device-presets.md)). `.rnd` is XML, so it stays ordinary text rather than going to LFS. Pattern presets are still compiled tables in `SessionPatches.cpp`.

`Drums/Kits/*.rdk` holds the [Drum Rack](drum-rack.md)'s kits and `Drums/Presets/<kind>/*.rdp` its drum presets, the folder naming the drum kind. `drumKits()` and `drumPresets()` return the factory files, then the person's own from `userDrums()` (`Documents/Rhino/Drums`, or `RHINO_USER_DRUMS_DIR`). Like device presets, they are re-read on every call. The factory set on 2026-10-06:
- eight kits: 808, House, Break, Minimal and Clap are Rhino Drums' five kits as files, with decays doubled because Decay now means a 60 dB fall rather than the old quadratic fade; Vinyl Kit is all samples, Analog Kit all synths and Hybrid Kit both;
- 23 drum presets under Kick, Snare, Clap, Hat, Tom, Cymbal and Percussion.

A kit or project names a library sound as `library:<path under the root>` (`storedPath`), so it resolves wherever the library is installed; anything else is a full path. `resolveStoredPath` may return a file that does not exist.

Every sample carries a `drumType`, one of `drumTypes()` (Kick, Snare, Clap, Hat, Tom, Cymbal, Percussion) or empty. `drumTypeOf(group, name)` asks the folder first, so a kick named "Crash" is still a kick, then the words of the name. It matches whole words, splitting at case changes and at letter-digit boundaries: "bd01" is a kick and "that" is not a hat. On 2026-10-06 `--self-test` reported all 291 library samples as drum hits.

## Rules

- **Git LFS for audio.** `.gitattributes` routes every audio extension under `library/` through LFS, set up before the library had any history. Set LFS up *before* adding a new content type: converting afterwards means rewriting history ([Git workflow](git-workflow.md)).
- **Read off the audio thread, and survive absence.** Content is data on disk and can be missing; `ContentLibrary::file` returns a non-existent `File` then. The Drum Rack reads a pad's sample on the message thread when the pad changes. A missing file logs `drum sample missing` and leaves that pad silent. A file that is present but will not decode, which is what a Git LFS pointer that was never pulled looks like, logs `drum sample unreadable` (commit `492cc8b`). `--self-test` logs how long the 808 Kit's eight samples take to read onto their pads, about 3 ms warm on 2026-10-06 ([Real-time audio rules](real-time-audio-rules.md)).
- **The browser mirrors the scan.** `--self-test` asserts that the Samples section's rows equal `ContentLibrary::samples().size()`, which also holds with no library present.
- **An installed build needs `Library` beside the executable**; no build step or script copies it there yet (as of 2026-10-03).

## Related

- [Content is files, never compiled in](content-is-files.md)
- [Browser and library preview](browser.md)
- [Built-in devices](built-in-devices.md)
- [Rhino's build targets](rhino-build-targets.md)
- [Git workflow](git-workflow.md)
- [Pattern presets](pattern-presets.md)
- [Device presets (.rnd)](device-presets.md)
- [Drum Rack](drum-rack.md)
