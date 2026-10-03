---
title: Build and test Forge
type: guide
summary: Configure Forge against Rhino's JUCE checkout, build around the toolchain's traps, and run one test area at a time.
tags: [forge, build, testing, windows]
sources: []
updated: 2026-10-03
---

# Build and test Forge

Forge is its own CMake project under `instruments/rhino-forge/`, with no Tracktion or Rhino code, but it borrows
Rhino's pinned JUCE: `RHINO_JUCE_DIR` defaults to `native/.deps/juce`, so run `python native/scripts/fetch-dependencies.py`
first ([Build and test Rhino](build-and-test-rhino.md)). The long form is the *Tests* and *Build on Windows* sections of
`instruments/rhino-forge/README.md`.

## Build

```powershell
cmake -S instruments/rhino-forge -B instruments/rhino-forge/build -G "Visual Studio 17 2022" -A x64
cmake --build instruments/rhino-forge/build --config Release --parallel 2 -- /p:BuildInParallel=false
```

- `/p:BuildInParallel=false` serialises project-reference resolution, which otherwise fails silently with a
  `GetTargetPath` error; compiling still uses two jobs.
- `CMAKE_SUPPRESS_REGENERATION ON` is set here too, so re-run the configure line after editing `CMakeLists.txt`
  ([A new source file needs an explicit CMake configure](cmake-does-not-reconfigure.md)).
- `VST3_AUTO_MANIFEST FALSE` is deliberate: the pinned JUCE generator's manifest-helper project fails before the VST3
  target runs. Development hosts do not need the manifest; re-enable it for release packaging.
- Every source compiles against the precompiled `src/ForgePch.h` (JUCE and the standard library, no Forge header),
  roughly halving a translation unit's cost.
- A full rebuild took about eight minutes (2026-09-22), and any `core/` or `ui/` header change is one, because the
  engine is headers only ([Forge's engine splits into headers only](forge-engine-headers-only.md)). Start such a build in
  the background: a foreground call that times out kills MSBuild and leaves orphaned `cl.exe` processes holding source
  files ([Build locks from MSBuild nodes and orphaned compilers](orphaned-build-processes.md)).

Outputs land under `instruments/rhino-forge/build/RhinoForge_artefacts/Release/`: `Standalone/Rhino Forge.exe` and
`VST3/Rhino Forge.vst3`. The VST3 cannot relink while RhinoDAW or a DAW has it loaded, which is the normal state
mid-session; build `--target RhinoForgeTests RhinoForge_Standalone` instead and report the VST3 as stale
([LNK1104 means a running binary holds the file](locked-executable-lnk1104.md)). Never commit a bundle, and install one
only after validating it in a host.

## Test

One binary, one file per area, one CTest case per area: 18 areas as of 2026-10-03 (`forge_test_areas` in
`CMakeLists.txt`).

```powershell
cmake --build instruments/rhino-forge/build --config Release --target RhinoForgeTests --parallel 2 -- /p:BuildInParallel=false
ctest --test-dir instruments/rhino-forge/build -C Release -R forge_fx --output-on-failure
ctest --test-dir instruments/rhino-forge/build -C Release -j 8 --output-on-failure
```

- While working, run your area (`forge_<area>`: one translation unit rebuilt, about a second of checks); at a
  milestone, run them all. `-j 8` is safe because no two areas share a `Processor`.
- Run directly, the binary takes area names with or without dashes (`fx`, `--fx`), several at once in the order given,
  or none to run everything; an unknown name exits 2 with the list. Only `--list` and the tools below need their dashes.
- Adding an area touches four files: the new `tests/ForgeTests<Area>.cpp`, its declaration in `tests/ForgeSuites.h`, a
  row in the table in `tests/ForgeTestMain.cpp`, and two lines of `CMakeLists.txt` (the file in `target_sources`, the
  name in `forge_test_areas`). Nothing self-registers; reconfigure afterwards.
- The test binary links with an 8 MB stack because suites keep several `Processor`s as locals; a CTest `SegFault` can
  still be an overflow ([A CTest SegFault may be a stack overflow](stack-overflow-reports-as-segfault.md)).
- To prove a new check can fail, break the code temporarily and then restore it by editing, not with `Copy-Item`. A
  copied-back file keeps the backup's older timestamp, so MSBuild keeps the object built from the broken code
  ([Editing only a scenario .inc does not rebuild the tests](inc-edits-do-not-rebuild.md)).
- Never filter the build and run `ctest` in one command (`cmake --build ... | Select-String ...; ctest ...`): a failed
  build leaves the old binary and CTest reports it all passed. Redirect the build to a log, check `$LASTEXITCODE`, and
  only then run CTest ([Editing only a scenario .inc does not rebuild the tests](inc-edits-do-not-rebuild.md)).
- `--snapshot`, `--profile` and `--render` are development tools in the same binary, not test cases
  ([Proving a Forge change changed nothing](proving-a-forge-change-changed-nothing.md)).

## Related

- [Rhino Forge](forge.md)
- [Forge is an independent VST3](forge-is-an-independent-vst3.md)
- [Driving Forge's standalone on Windows](driving-the-forge-standalone.md)
- [Seeing the UI without taking the screen](headless-ui-snapshots.md)
- [Hosting Forge in Rhino](forge-hosting.md)
