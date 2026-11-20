---
title: Forge's macOS build and installer
type: guide
summary: Since 2026-11-20 a GitHub-hosted macos-15 runner builds Forge's app, VST3 and AU universal (arm64 and x86_64, macOS 11 and up), packs them into one Rhino Forge.pkg signed ad hoc and not notarized, keeps it 30 days as the run's artifact, then installs it on the runner and runs auval; whoever installs it must allow it once under Privacy & Security. Verified end to end on the runner on 2026-11-20 (3333e60), after clang refused `file = {}` and `pkgbuild --analyze` wrote no BundleIsRelocatable key; not yet installed on a real Mac.
tags: [forge, build, macos, ci, packaging, release]
sources: []
updated: 2026-11-20
---

# Forge's macOS build and installer

## Why it runs in CI

Added on 2026-11-20 (commits `1672601`, `f23ae56`, `22c7b03`) so that someone with a Mac can be handed a Forge that installs, although nobody here owns a Mac. macOS binaries need Apple's toolchain, so the build runs on a GitHub-hosted macOS runner. The repository is public, so its macOS minutes are free, and since it is under the AGPL ([Rhino is licensed under the AGPL v3 or later](agpl-licence.md)) a public artifact is allowed.

## The workflow

`.github/workflows/forge-macos.yml` runs on a push to `main` that touches `instruments/rhino-forge/**`, `native/scripts/fetch-dependencies.py` or the workflow, and on demand (`workflow_dispatch`).

- `runs-on: macos-15`, named rather than `macos-latest`, so that a new image arrives as a deliberate change.
- `native/.deps` is cached under a key made from the fetch script's hash. The script is given `GITHUB_TOKEN`, because the runners' shared addresses use up GitHub's unauthenticated API limit of 60 requests an hour.
- It configures with the default generator (Unix Makefiles): Release, `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"`, `CMAKE_OSX_DEPLOYMENT_TARGET=11.0` and `BUILD_TESTING=OFF`. It then builds `RhinoForge_Standalone`, `RhinoForge_VST3` and `RhinoForge_AU`. The build passes `-- -k` to make (since `d9a27af`), so it carries on past a failing file and one run reports every file clang refuses. Without it, make stops at the first. AU is added to `FORMATS` only when `APPLE` is set (`forge_formats` in `CMakeLists.txt`). Forge's tests do not run on macOS yet.
- It packages, uploads the artifact `Rhino-Forge-macOS` (`Rhino Forge.pkg`, kept 30 days), then runs `sudo installer -pkg` on the runner and `auval -v aumu Frge Rhno` on the installed AU. The upload happens before that check, so a failing auval turns the run red but the package can still be downloaded. Since `c76231d` the step also requires `AU VALIDATION SUCCEEDED` in `install.log` (`grep -q`), not only a clean exit: a four-second install and validation could not be confirmed from outside the log, which needs sign-in.
- Since `afa06f5` the Package step first lists the artefacts folder (`find -maxdepth 3`) and writes everything it prints to `package.log`. The install-and-auval step writes to `install.log`.

**Reading a failure without signing in.** Through the public API, a run's logs and artifacts need authentication, but its annotations do not (`check-runs/{job id}/annotations`). The rule since `afa06f5` is that every failure must be readable from the annotation, not only a compile error. So the last step of the job, `Report errors` (`if: failure()`), builds one `::error` annotation with the newlines escaped:

- up to 60 `error:` and `CMake Error` lines from `configure.log` and `build.log`. clang compiles a universal file once per architecture, so every error appears twice, and `awk '!seen[$0]++'` drops the second copy. make's own `Error N` lines are not matched.
- the last 30 lines of `package.log` and of `install.log`, when they exist.

Downloading the package still needs a signed-in browser or a token. Asking the user to read a failed step's log in a signed-in browser was the fastest diagnosis (run 3, below). The annotation is for when nobody is at a browser.

## Getting and installing it

How a person downloads the package, what installs where, the one-time Open Anyway and building it on a Mac are in the *Get Forge on macOS* section of `instruments/rhino-forge/README.md` (since `f17df10`). This page does not repeat them ([Documentation ownership](documentation-ownership.md)).

## The package

`instruments/rhino-forge/packaging/macos/build-pkg.sh <artefacts folder> <output .pkg>`, where the folder is `build/RhinoForge_artefacts/Release`, runs the same way on a Mac.

- It copies each bundle with `ditto` to where it installs: the app to `/Applications`, the VST3 to `/Library/Audio/Plug-Ins/VST3` and the AU to `/Library/Audio/Plug-Ins/Components`. Each bundle gets `presets/Factory` in `Contents/Resources/Factory`, which is where the bank looks when no source tree is above the binary ([Forge preset browser and bank](forge-preset-browser.md#the-bank)).
- It re-signs each bundle ad hoc (`codesign --force --sign -`). Adding the resources breaks the signature the linker made, and Apple Silicon runs nothing unsigned.
- It turns `BundleIsRelocatable` off for every component. The app and both plugins share `BUNDLE_ID com.rhino.forge`, so Installer would otherwise "relocate" one onto any existing bundle with that id elsewhere on the disk, instead of the path given. It runs `pkgbuild --analyze` and walks the entries with `plutil -extract <n>`. For each, `plutil -replace <n>.BundleIsRelocatable -bool NO` adds the key or changes it. If no entry is found, the script fails with "pkgbuild found no bundles under ...".
- The version comes from `project(RhinoForge VERSION ...)`, and the package identifier is `com.rhino.forge.installer`.
- `scripts/postinstall` clears `com.apple.quarantine` from the three installed bundles, because a host refuses a quarantined plugin without saying why. `xattr` failing on a bundle without the flag is the normal case. The script then kills `AudioComponentRegistrar`, so Logic and GarageBand see the new AU without a log-out.

No wavetable files ship. Presets embed their tables as `TABLE` nodes, the ten built-in tables are compiled in, and the git-ignored local table folders (168 MB) are left out ([Forge oscillators and wavetables](forge-oscillators.md)).

## Signing and what is still missing

The package is signed ad hoc only, with no Developer ID and no notarization. Whoever installs it must allow it once under System Settings > Privacy & Security > Open Anyway. A Developer ID ($99 a year) and notarization are the production step still to do. The package also carries no licence text and no source offer, both of which the AGPL asks of a distributed binary. It is a test build, not the production installer targeted for December 2026 to January 2027 ([Documentation ownership](documentation-ownership.md#release-stage-wording)).

## Gotchas

- **GitHub retired the `macos-14` image.** A run on it is cancelled within two seconds, and its only annotation reads "The configuration 'macos-14-arm64-us-default' is not supported". The pinned JUCE is 8.0.13 (read from the pinned commit's `CMakeLists.txt` on GitHub), which builds with the macOS 15 SDK.
- **clang refuses what MSVC accepts.** The first macos-15 run stopped on `currentPresetFile = {};`, an assignment that is ambiguous for a `juce::File` ([clang refuses `= {}` on a juce::File](juce-file-assign-empty-braces.md)). A Windows build proves nothing about the macOS build.
- **`pkgbuild --analyze` may write no `BundleIsRelocatable` key.** On the `macos-15` runner it wrote none for any bundle. PlistBuddy's `Set` refuses a key that does not exist (`Set: Entry, ":0:BundleIsRelocatable", Does Not Exist`), so the package script stopped there. Set the key with `plutil -replace`, which adds a missing key.
- The run warns that `actions/checkout@v4`, `actions/cache@v4` and `actions/setup-python@v5` target Node 20, which GitHub has deprecated and now runs on Node 24. This is harmless for now (2026-11-20).

## Status

**Verified on the runner, 2026-11-20.** Run 44838611409 (`3333e60`) passed every step in about 6 minutes:

| Step | Time |
| --- | --- |
| Fetch (cache hit) | 7 s |
| Configure | 43 s |
| Build | 4 min 29 s |
| Package | 6 s |
| Upload | 3 s |
| Install and `auval` | 4 s |

The artifact, a zip of the `.pkg`, is 47.2 MB. The package has **not yet been installed on a real Mac**; the user plans to try it on someone's laptop. Run 6 (`f17df10`) is confirming the stricter auval check from `c76231d`.

How it got there:

1. **Run 44836075420** (`22c7b03`) failed to compile on the ambiguous `= {}`, at `src/ForgeProcessorState.cpp:392` and `:537`.
   - `547aaec` fixed all ten such lines.
   - `d9a27af` made the workflow report every failing file.
   - After the fix, every Forge area passes on Windows except `forge_theme`, which already failed by two pixels ([A buffered child broke Forge's byte-identical paint](buffered-child-breaks-pixel-stability.md)).
2. **Run 44836970348** (`c9c48f3`) compiled all of Forge's plugin sources. It copied and ad-hoc-signed every bundle, and `lipo -archs` printed `x86_64 arm64` for each. Then packaging stopped on the missing `BundleIsRelocatable` key (Gotchas). The user found this in the signed-in log.
3. **`afa06f5`** added the diagnostics above. It also added an `xattr -cr` before `codesign`, a wrong guess that signing had failed. Signing never failed, so `3333e60` removed it again, and run 44837843204 on `afa06f5` failed the same way.
4. **`3333e60`** set the key with `plutil -replace`, and run 5 passed.

Still open:

- Forge's tests are not built or run on macOS.
- The workflow's actions target Node 20 (a warning only).
- Developer ID signing and notarization.
- The licence text and a source offer in the package.

## Related

- [Build and test Forge](build-and-test-forge.md)
- [clang refuses `= {}` on a juce::File](juce-file-assign-empty-braces.md)
- [Rhino Forge](forge.md)
- [Forge preset browser and bank](forge-preset-browser.md)
- [Rhino is licensed under the AGPL v3 or later](agpl-licence.md)
