---
title: A new source file needs an explicit CMake configure
type: gotcha
summary: Both projects suppress CMake regeneration, so a file or a flag added to a CMakeLists is silently left out until you configure again.
tags: [both, build, cmake]
sources: []
updated: 2026-10-05
---

# A new source file needs an explicit CMake configure

## Symptom

You add a `.cpp` to a source list and the build compiles, but the link fails with a wall of `LNK2019: unresolved external symbol` errors naming every function in the new file. It looks like a coding mistake, such as a missing definition or a mismatched signature. It is not: the file was never compiled.

## Cause

Both projects set `CMAKE_SUPPRESS_REGENERATION ON` (in `native/CMakeLists.txt` and `instruments/rhino-forge/CMakeLists.txt`). This removes the generated `ZERO_CHECK` target, which normally re-runs CMake when a CMakeLists file changes. Regeneration was suppressed to avoid a CMake/MSBuild failure on this toolchain, where the parent target failed straight after `ZERO_CHECK` succeeded. The cost is that `cmake --build` never reconfigures. It builds the stale `.vcxproj`, and the new file is not in it.

The comment beside Rhino's setting argues that explicit source lists make `ZERO_CHECK` unnecessary. For a new file the opposite is true: an explicit list reaches the generated project only when CMake runs.

## What it covers

Any edit to any CMakeLists in either project:

- **Rhino:** the application list in `native/CMakeLists.txt`, `RhinoCore` in `native/src/core/CMakeLists.txt`, and `RhinoDevices` in `native/src/devices/CMakeLists.txt`. Adding a device's source is one of [the three edits for a device](adding-a-device.md), so it needs a configure too.
- **Forge:** `forge_sources`, the test sources and `forge_test_areas`. A new test area also gets no CTest case until a configure, because CMake generates the case list at configure time.
- **Renames and deletions** fail the other way. The stale project still names the old file, and the compiler cannot open it.
- **Compile and link options** fail silently. Commit `3f6bb4e` added `/STACK:8388608` to `native/CMakeLists.txt`, and a plain build kept linking the old 1 MB stack reserve until the configure ran. `dumpbin /headers RhinoDAW.exe` shows which one the binary got.

## What to do

Reconfigure after any CMakeLists edit, before reading a line of the new code:

```
cmake -S native -B native/build
cmake -S instruments/rhino-forge -B instruments/rhino-forge/build
```

Then build as usual.

## Tell it apart

- `LNK1104` on the output file means a running binary holds it, not that code is missing. See [LNK1104 means a running binary holds the file](locked-executable-lnk1104.md).
- Two related traps give no error at all. Editing a scenario `.inc` ([Editing only a scenario .inc does not rebuild the tests](inc-edits-do-not-rebuild.md)) and editing app icon art ([App icons are baked at configure time](app-icon-baked-at-configure.md)) can both leave a stale binary behind a green build.

## Related

- [Build and test Rhino](build-and-test-rhino.md)
- [Build and test Forge](build-and-test-forge.md)
- [Rhino's build targets](rhino-build-targets.md)
- [Keeping files small](keeping-files-small.md)
- [Adding a device to Rhino](adding-a-device.md)
