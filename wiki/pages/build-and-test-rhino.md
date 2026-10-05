---
title: Build and test Rhino
type: guide
summary: Fetch the pinned engine, build with Visual Studio 2022, and run the five CTest cases without being fooled by a stale or locked build.
tags: [rhino, build, testing, windows]
sources: []
updated: 2026-10-05
---

# Build and test Rhino

Rhino builds with CMake and Visual Studio 2022. The test runners are compiled into the app, so every CTest case is
`RhinoDAW.exe` with a flag. `README.md` has the newcomer quick start; this page and the *Tests* section of `AGENTS.md`
carry the development detail.

## Prerequisites

Python 3.12+, CMake 3.24+, Visual Studio 2022 with the *Desktop development with C++* workload and a Windows SDK, and
Git LFS for the audio under `library/`. Where these live on the development machine:
[Development environment and reference material](development-environment.md).

## Build

```powershell
python native/scripts/fetch-dependencies.py
cmake -S native -B native/build -G "Visual Studio 17 2022" -A x64
$env:MSBUILDDISABLENODEREUSE = 1
cmake --build native/build --config Release --parallel 2
```

1. The fetch script downloads the pinned Tracktion and JUCE revisions into `native/.deps/` and marks each with a
   `.rhino-revision` file. It skips one already at its revision and refuses a folder without the marker. Configure
   stops with an error until it has run.
2. Re-run the configure line after any `CMakeLists.txt` edit. `CMAKE_SUPPRESS_REGENERATION ON` means the build never
   reconfigures itself, so a new `.cpp` is silently skipped and the link fails with `LNK2019`
   ([A new source file needs an explicit CMake configure](cmake-does-not-reconfigure.md)).
3. `MSBUILDDISABLENODEREUSE=1` keeps idle MSBuild nodes from holding `.obj` files between builds
   ([Build locks from MSBuild nodes and orphaned compilers](orphaned-build-processes.md)).
4. Judge the build by its exit code (`$LASTEXITCODE`), not by grepping piped output, which hides a failed link. On a
   failure, search for `error C` first: a lone `LNK1104` on `RhinoDAW.exe` means the app is open, not that the code is
   wrong ([LNK1104 means a running binary holds the file](locked-executable-lnk1104.md)).

The app lands at `native/build/RhinoNative_artefacts/Release/RhinoDAW.exe`
([The executable is RhinoDAW, not Rhino](executable-named-rhinodaw.md)). Any `native/build-*` folder is gitignored and
gives a build nobody else can lock, at the cost of a full engine build of several minutes.

## Test

```powershell
ctest --test-dir native/build -C Release --output-on-failure
ctest --test-dir native/build -C Release -R native_arrangement_workflow --output-on-failure
```

| Case | Flag | Timeout |
| --- | --- | --- |
| `native_arrangement_geometry` | `--arrangement-geometry-test` | 10 s |
| `native_device_correctness` | `--self-test` | 30 s |
| `native_pattern_workflow` | `--pattern-test` | 120 s |
| `native_arrangement_workflow` | `--arrangement-test` | 120 s |
| `native_startup_lifecycle` | `--startup-test` | 30 s |

(From `native/CMakeLists.txt`, 2026-10-03, whose comment puts each workflow run at 20-26 s; what each case covers is in
`native/src/tests/README.md`.) The generous limit is there to catch a render that never returns
([Offline renders that never return](renders-that-never-return.md)).

- An unrecognised flag is not an error. `Application::initialise` takes it for a project path, finds no such file and
  opens the GUI, which waits until killed.
- `--output-on-failure` hides a passing run's output. After editing only a scenario `.inc`, touch the runner `.cpp`
  and confirm its `Arrangement scenario: <name>` line appears
  ([Editing only a scenario .inc does not rebuild the tests](inc-edits-do-not-rebuild.md)).
- A failure that reads only "Moved render timeout" is a 15 s wall-clock budget tripping on a busy machine. Re-run that
  case on an idle machine before investigating.
- `--profile-ui` is a timing tool in the same binary, not a case: it prints medians and checks nothing
  ([Writing Rhino tests](writing-rhino-tests.md)).

## Related

- [Writing Rhino tests](writing-rhino-tests.md)
- [Rhino's build targets](rhino-build-targets.md)
- [Build and test Forge](build-and-test-forge.md)
- [Debugging a crash only one project triggers](debugging-a-crashing-project.md)
- [Development environment and reference material](development-environment.md)
