---
title: Build and test Rhino
type: guide
summary: Fetch the pinned engine, build with Visual Studio 2022 from the shell or from VS Code's CMake Tools, and run the six CTest cases without being fooled by a stale or locked build.
tags: [rhino, build, testing, windows, vscode]
sources: []
updated: 2026-10-25
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
   ([A new source file needs an explicit CMake configure](cmake-does-not-reconfigure.md)). Configure after a merge
   too, before believing the result builds: two branches that each append to the same explicit source list can merge
   without a conflict and still leave the list unparseable. Merging `main` into `feat/dj-view` left
   `native/src/devices/CMakeLists.txt` with `midi/RhinoArpDeviceProcessing.cpp)` *after* the closing parenthesis of
   `add_library(RhinoDevices ...)`, and `main` would not configure at all ("Parse error. Expected a command name")
   until `0bff66b` (2026-10-13).
3. `MSBUILDDISABLENODEREUSE=1` keeps idle MSBuild nodes from holding `.obj` files between builds
   ([Build locks from MSBuild nodes and orphaned compilers](orphaned-build-processes.md)).
4. Judge the build by its exit code (`$LASTEXITCODE`), not by grepping piped output, which hides a failed link. On a
   failure, search for `error C` first: a lone `LNK1104` on `RhinoDAW.exe` means the app is open, not that the code is
   wrong ([LNK1104 means a running binary holds the file](locked-executable-lnk1104.md)).
5. `--parallel 2` caps only how many projects build at once. `RhinoNative.vcxproj` compiles with `/MP` and no count,
   so inside it `cl.exe` runs a file per logical core. Where memory is short, as on the second machine, build with
   `--parallel 1 -- /p:BuildInParallel=false /p:CL_MPCount=2`. Blue screens or `nvlddmkm` timeouts during a build mean
   stop and cap it ([A build's `--parallel` does not cap the compiler](build-parallel-does-not-cap-compiler.md)).

The app lands at `native/build/RhinoNative_artefacts/Release/RhinoDAW.exe`
([The executable is RhinoDAW, not Rhino](executable-named-rhinodaw.md)). `native/build` is the only build tree. A
`native/build-*` folder would be gitignored and give a build nobody else can lock, at the cost of a full engine build
of several minutes, but the user asked on 2026-10-10 that no second tree be configured (`native/build-dj` was deleted
that day): when a running RhinoDAW blocks the link, ask them to close it instead
([LNK1104 means a running binary holds the file](locked-executable-lnk1104.md)).

## Test

```powershell
ctest --test-dir native/build -C Release --output-on-failure
ctest --test-dir native/build -C Release -R native_arrangement_workflow --output-on-failure
```

| Case | Flag | Timeout |
| --- | --- | --- |
| `native_arrangement_geometry` | `--arrangement-geometry-test` | 10 s |
| `native_device_correctness` | `--self-test` | 30 s |
| `native_device_conformance` | `--device-test` | 120 s |
| `native_pattern_workflow` | `--pattern-test` | 120 s |
| `native_arrangement_workflow` | `--arrangement-test` | 300 s |
| `native_startup_lifecycle` | `--startup-test` | 30 s |

(From `native/CMakeLists.txt`, checked 2026-10-25, whose comment puts the pattern run at about 36 s and the arrangement
run, which carries every DJ scenario, at 114 s on 2026-10-11; what each case covers is in
`native/src/tests/README.md`.) The generous limit is there to catch a render that never returns
([Offline renders that never return](renders-that-never-return.md)). The conformance run takes about 3 s and opens no
audio device ([The native device standard](native-device-standard.md)).

- Run the cases through `ctest`. On 2026-10-05, `Start-Process RhinoDAW.exe --device-test` opened the full app on the
  user's screen instead of running the check, and the cause was not found.

- An unrecognised flag is not an error. `Application::initialise` takes it for a project path, finds no such file and
  opens the GUI, which waits until killed.
- `--output-on-failure` hides a passing run's output. After editing only a scenario `.inc`, touch the runner `.cpp`
  and confirm its `Arrangement scenario: <name>` line appears
  ([Editing only a scenario .inc does not rebuild the tests](inc-edits-do-not-rebuild.md)).
- A source file restored with `Copy-Item` keeps the backup's older `LastWriteTime`, so MSBuild does not recompile it
  and the tests run the temporary edit. Restore by editing, or touch the file before building (same page).
- A check that fails only under `ctest -j 3` is not necessarily load: the device SDK's undo race failed in every
  parallel run and passed every sequential one ([A CachedValue can lag its own ValueTree](cachedvalue-lags-its-tree.md)).
- A failure that reads only "Moved render timeout" is a 15 s wall-clock budget tripping on a busy machine. Re-run that
  case on an idle machine before investigating. "project files: the export gives up on a sample it cannot read"
  (`ProjectFileMedia.inc`) is the same kind: it requires the export to give up within 10 s, took 11.3 s once under
  load on 2026-10-25 and passed on the re-run.
- `native_device_correctness` runs close to its limit: on 2026-10-23 it took 21 s alone and 29 s run straight after a
  profile, against 30 s. Run the suite on an idle machine.
- `native_arrangement_geometry` also runs the layer lint (`tests/LayerLintTest.cpp`, 2026-10-23): it reads every
  source under `native/src/ui` and `native/src/app` and fails, naming file and line, on a line that reaches past
  `Session` into the engine ([Dependency direction](dependency-direction.md#the-views-ask-session-not-the-engine)).
- `--profile-ui` and `--profile-stress` are timing tools in the same binary, not cases: they print medians and check
  nothing ([Writing Rhino tests](writing-rhino-tests.md)). `--profile-stress` builds a 120-track, seven-minute song,
  takes the process to about 440 MB, and is run from a shell, never through `Start-Process`
  ([The 120-track stress audit](stress-audit-2026-10.md)).

## From VS Code

Since 2026-10-25 both projects build from VS Code's status bar through CMake Tools (`ms-vscode.cmake-tools`), with the
C/C++ extension taking IntelliSense from it. The settings are `.vscode/settings.json`, which `.gitignore` excludes
(commit `7e55180`: editor settings belong to one machine), so this is the only record of them:

```jsonc
{
    "cmake.sourceDirectory": [
        "${workspaceFolder}/native",
        "${workspaceFolder}/instruments/rhino-forge"
    ],
    "cmake.buildDirectory": "${sourceDirectory}/build",
    "cmake.generator": "Visual Studio 17 2022",
    "cmake.platform": "x64",
    "cmake.useCMakePresets": "never",
    "cmake.defaultVariants": {
        "buildType": {
            "default": "release",
            "choices": {
                "release": { "short": "Release", "buildType": "Release" },
                "debug": { "short": "Debug", "buildType": "Debug" }
            }
        }
    },
    "cmake.parallelJobs": 2,
    "cmake.buildToolArgs": ["/p:BuildInParallel=false"],
    "cmake.ctestArgs": ["--output-on-failure"],
    "C_Cpp.default.configurationProvider": "ms-vscode.cmake-tools"
}
```

- The repository root has no `CMakeLists.txt`. The status bar's *Active Project* switches between the DAW and
  [Forge](build-and-test-forge.md); each reuses its existing `build` tree, so no second tree is made.
- Generator, platform and no presets match the command-line configure both caches were made with (VS 2022 Community,
  empty `CMAKE_GENERATOR_TOOLSET`). Release is the default because the tests expect it.
- `--parallel 2` and `/p:BuildInParallel=false` are the command-line build lines. Forge needs the flag, it is
  harmless for the DAW, and a single-folder workspace cannot set them per project. CTest stays sequential (the extension's
  default) because checks have failed under `ctest -j 3`.
- Launch target: `RhinoNative` (it builds `RhinoDAW.exe`) or `RhinoForge_Standalone`.
- **Choose the [Unspecified] kit.** A Visual Studio kit passes `-T host=x64`, which should make CMake refuse to
  reconfigure the empty-toolset caches ("does not match the toolset used previously"), and both ways out, wiping a
  cache or a second tree, are unwanted. This is expected, not observed: the settings had not been run inside VS Code
  on 2026-10-25.
- Regeneration is still suppressed: after adding a source file, run *CMake: Configure* from the command palette
  ([A new source file needs an explicit CMake configure](cmake-does-not-reconfigure.md)). A running RhinoDAW still
  blocks the link ([LNK1104 means a running binary holds the file](locked-executable-lnk1104.md)).

## Related

- [Writing Rhino tests](writing-rhino-tests.md)
- [The 120-track stress audit](stress-audit-2026-10.md)
- [Rhino's build targets](rhino-build-targets.md)
- [Build and test Forge](build-and-test-forge.md)
- [Debugging a crash only one project triggers](debugging-a-crashing-project.md)
- [Development environment and reference material](development-environment.md)
