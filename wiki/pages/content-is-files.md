---
title: Content is files, never compiled in
type: decision
summary: Samples and other content live as files under library/ and are found at runtime; only fonts and app icons are embedded.
tags: [rhino, content, build, git-lfs]
sources: []
updated: 2026-10-07
---

# Content is files, never compiled in

## Context

Rhino used to embed its samples (the TR-808 kit and a hand clap) with `juce_add_binary_data`, which turns an asset into a C++ array at roughly three bytes of source per byte of data and recompiles all of it on a clean build. The 700 KB then embedded had become 2.1 MB of generated source, and the 282-file vinyl drum pack about to be added would have been roughly 54 MB of C++ (commit `98b750b`, 2026-09-19). The rule is also stated in `AGENTS.md` (*Content is files, never compiled in*).

## Decision

Samples, device presets, drum kits and drum presets now, and patterns later, live as files under `library/` at the repository root and are found at runtime by `ContentLibrary` (`native/src/core/ContentLibrary.*`). It knows nothing of `Session`, the UI or the engine, so the app and the device library can both use it. `ContentLibrary::root()` tries the `RHINO_LIBRARY_DIR` override, then a `Library` folder beside the executable (inside the bundle on macOS), then the repository's `library/` through the compile-time `RHINO_SOURCE_DIR`. A candidate counts only if it holds `Samples/`, so an empty folder cannot shadow the real library. No build step copies anything. Details are on [Content library](content-library.md).

Only what the UI needs before it can read from disk stays embedded. In Rhino that is the two Inter cuts (`RhinoNativeAssets` in `native/CMakeLists.txt`) and the app icons; the metronome glyph is SVG path data pasted into `native/src/ControlBarIcons.cpp`. Forge embeds its wordmark, two screw images, the performance wheel and four typefaces subset from 988 KB to 79 KB (`instruments/rhino-forge/ui/assets/fonts/README.md`).

Rejected: keeping audio embedded, for the build cost above.

## Consequences

- **Set up Git LFS before adding a content type.** `.gitattributes` sends WAV, FLAC, AIFF, OGG and MP3 under `library/` to LFS, configured before the library had any history; a type added later would have to be converted by rewriting history. `SOURCE.md` and licence files stay plain text so provenance reads in a diff. Forge's ten factory tables are ordinary `.wav` files marked binary, not LFS ([Git workflow](git-workflow.md)).
- **Devices read content off the audio thread** and must survive its absence. The Drum Rack reads a pad's sample on the message thread when the pad changes; a missing file is logged and the pad falls silent ([Drum Rack](drum-rack.md)). Reading the 808 Kit's eight samples onto their pads took 2.8-4.0 ms warm over six runs on 2026-10-06, and 5.3-6.3 ms warm (51.6 ms cold) on 2026-10-07, after the rack grew to 768 controls; `--self-test` writes the figure to `rhino.log`. Rhino Drums, its predecessor, read its kit in `initialise()`: 525 KB in 1.2 ms warm.
- **Content names content by place, never by copy.** A kit or a drum preset names its samples as `library:<path>` and embeds none, so the library can move and a kit still opens ([Content library](content-library.md)).
- **The suite proves the read is real.** When the library moved out, hiding `library/` made `--self-test` fail at its kick assertion, and the browser's sample rows must equal `ContentLibrary::samples()`; zero on both sides passes, so a build without the library still has a working browser.
- An installed build needs a `Library` folder beside the executable, or `RHINO_LIBRARY_DIR`.
- App icons are not exempt from build surprises: they are baked at configure time ([App icons are baked at configure time](app-icon-baked-at-configure.md)).

## Related

- [Content library](content-library.md)
- [Built-in devices](built-in-devices.md)
- [Drum Rack](drum-rack.md)
- [Rhino's build targets](rhino-build-targets.md)
- [Git workflow](git-workflow.md)
- [Colours and typography](colours-and-typography.md)
