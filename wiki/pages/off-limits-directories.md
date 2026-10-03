---
title: Directories not to read
type: convention
summary: native/.deps and research/sources are not Rhino's code and are never searched; Tracktion signatures are checked in curated header snapshots.
tags: [both, workspace, search]
sources: []
updated: 2026-10-03
---

# Directories not to read

Two large trees in the workspace hold no Rhino or Forge code. Nothing in either is ever the answer to "where is this implemented", and reading them costs context and returns misleading results (`AGENTS.md`, *Do not read these directories*).

## `native/.deps/`

This holds the pinned JUCE and Tracktion Engine checkouts. `python native/scripts/fetch-dependencies.py` fetches them, pins each by hash and leaves a `.rhino-revision` marker in each tree, and git ignores the directory. `AGENTS.md` puts it at over 400,000 lines. The builds do use it: `native/CMakeLists.txt` adds both trees as subdirectories, and Forge finds JUCE there through `RHINO_JUCE_DIR`. `AGENTS.md` says neither tree is compiled; it means neither is Rhino's code. Because the directory is gitignored, a default ripgrep search skips it. An explicit path, or a tool that ignores `.gitignore`, does not, so never point one there.

## `research/sources/`

This holds read-only snapshots of Ardour, LMMS, Zrythm and Tracktion Engine, taken on 2026-09-08 by `scripts/research-sources.py` for the [architecture study](tracktion-and-juce.md). `.gitattributes` keeps them byte-for-byte so their recorded hashes stay valid. Unlike `.deps`, this tree is **tracked**, so default searches *do* include it. Exclude it deliberately, for example with a `!research/sources/**` glob. Reach it only through the index in `research/SOURCE_MAP.md`, and only when prior art is explicitly wanted.

## Confirming a Tracktion signature

When a Tracktion API must be confirmed rather than guessed, read `native/README.md` first for engine behaviour, then grep `research/sources/tracktion/`. That is the one sanctioned way in. It holds nine engine headers, among them `tracktion_Plugin.h`, `tracktion_Edit.h`, `tracktion_AutomatableParameter.h`, `tracktion_WaveAudioClip.h`, `tracktion_PluginNode.h` and `tracktion_PluginManager.h`, plus the engine's `FEATURES.md` and `LICENSE.md`. They come from the same revision the build pins. Two things to know:

- The files have flattened names with a `.txt` suffix, such as `modules__tracktion_engine__plugins__tracktion_Plugin.h.txt`, so a glob for `*.h` finds nothing. List the directory first.
- They are headers only. `AutomatableParameter::valueToString` can be confirmed there; what a method body does cannot. When only a body would answer, design so that a wrong guess cannot change behaviour already on screen: add the new path beside the existing call sites rather than rewriting them to share one.

## Also scratch

Build directories (`native/build*`, `instruments/rhino-forge/build`), `artifacts/` and `temp/` are generated or temporary, ignored by git, and hold no source.

## Related

- [Tracktion Engine with a native JUCE UI](tracktion-and-juce.md)
- [Development environment and reference material](development-environment.md)
- [Build and test Rhino](build-and-test-rhino.md)
- [Git workflow](git-workflow.md)
