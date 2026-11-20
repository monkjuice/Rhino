---
title: clang refuses `= {}` on a juce::File
type: gotcha
summary: juce::File has operator= from const File&, File&& and const String&, and an empty braced list can become either a File or a String, so clang rejects `file = {}` as ambiguous while MSVC quietly picks one; Forge's first macOS build failed on it, and since 547aaec (2026-11-20) a juce::File is cleared with `= juce::File {}`, never `= {}`.
tags: [forge, juce, clang, macos, portability, convention]
sources: []
updated: 2026-11-20
---

# clang refuses `= {}` on a juce::File

`juce::File` has three assignment operators: from `const File&`, from `File&&` and from `const String&`. In `file = {};` the empty braced list can value-initialise either a `File` or a `juce::String`, so the call is ambiguous. clang says so, correctly: "use of overloaded operator '=' is ambiguous (with operand types 'juce::File' and 'void')". MSVC silently picks one, so the Windows build never shows it.

Forge's first macOS build stopped on it at `currentPresetFile = {};` in `src/ForgeProcessorState.cpp` ([Forge's macOS build and installer](forge-macos-build.md)). Commit `547aaec` (2026-11-20) rewrote all ten such assignments in Forge:

- `ForgeProcessorState.cpp`: `currentPresetFile`, twice.
- `ForgePresetBrowser.cpp`: `highlighted`, `confirmReplace` twice, `renaming` twice, and `dragging`.
- `ForgePresetBrowserViews.cpp`: `pressed`, twice.

The browser's assignments had not been reported yet, because make stops at the first file that fails. The next run (`c9c48f3`) compiled all of Forge's plugin sources, so these ten were the only lines clang refused there. Forge's tests are not built on macOS yet.

**Rule:** clear a `juce::File` with `= juce::File {};`, never `= {};`.

These are not affected:

- `return {};`
- `= {}` in a declaration
- passing `{}` to a `const File&` parameter
- `= {}` on a type whose `operator=` takes only its own type, such as `juce::String`, `juce::Image` or a plain struct

The same check found none of the other usual MSVC-only constructs in Forge:

- a nested struct's default member initialiser used as a default argument inside the enclosing class
- braced initialisation from `std::lround` or `std::lrint` (`long` is 64-bit on macOS and 32-bit on Windows)
- `static_assert(false)`

Rhino's own code under `native/src` has not been built with clang, so it has not been checked for any of these.

## Related

- [Forge's macOS build and installer](forge-macos-build.md)
- [Build and test Forge](build-and-test-forge.md)
