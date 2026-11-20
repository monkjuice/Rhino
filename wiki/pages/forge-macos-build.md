---
title: Forge's macOS build and installer
type: guide
summary: Since 2026-11-20 a GitHub-hosted macos-15 runner builds Forge's app, VST3 and AU universal (arm64 and x86_64, macOS 11 and up), packs them into one Rhino Forge.pkg signed ad hoc and not notarized, keeps it 30 days as the run's artifact, then installs it on the runner and runs auval; whoever installs it must allow it once under Privacy & Security, and the first macos-15 run was not yet verified when this was written.
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
- It configures with the default generator (Unix Makefiles): Release, `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"`, `CMAKE_OSX_DEPLOYMENT_TARGET=11.0` and `BUILD_TESTING=OFF`. It then builds `RhinoForge_Standalone`, `RhinoForge_VST3` and `RhinoForge_AU`. AU is added to `FORMATS` only when `APPLE` is set (`forge_formats` in `CMakeLists.txt`). Forge's tests do not run on macOS yet.
- It packages, uploads the artifact `Rhino-Forge-macOS` (`Rhino Forge.pkg`, kept 30 days), then runs `sudo installer -pkg` on the runner and `auval -v aumu Frge Rhno` on the installed AU. The upload happens before that check, so a failing auval turns the run red but the package can still be downloaded.

**Reading a failure without signing in.** Through the public API, a run's logs and artifacts need authentication, but its annotations do not (`check-runs/{job id}/annotations`). So when a step fails, a later step greps `configure.log` and `build.log` for error lines and sends them as one `::error` annotation, with the newlines escaped. Downloading the package still needs a signed-in browser or a token.

## The package

`instruments/rhino-forge/packaging/macos/build-pkg.sh <artefacts folder> <output .pkg>`, where the folder is `build/RhinoForge_artefacts/Release`, runs the same way on a Mac.

- It copies each bundle with `ditto` to where it installs: the app to `/Applications`, the VST3 to `/Library/Audio/Plug-Ins/VST3` and the AU to `/Library/Audio/Plug-Ins/Components`. Each bundle gets `presets/Factory` in `Contents/Resources/Factory`, which is where the bank looks when no source tree is above the binary ([Forge preset browser and bank](forge-preset-browser.md#the-bank)).
- It re-signs each bundle ad hoc (`codesign --force --sign -`). Adding the resources breaks the signature the linker made, and Apple Silicon runs nothing unsigned.
- It turns `BundleIsRelocatable` off for every component. The app and both plugins share `BUNDLE_ID com.rhino.forge`, so Installer would otherwise "relocate" one onto any existing bundle with that id elsewhere on the disk, instead of the path given.
- The version comes from `project(RhinoForge VERSION ...)`, and the package identifier is `com.rhino.forge.installer`.
- `scripts/postinstall` clears `com.apple.quarantine` from the three installed bundles, because a host refuses a quarantined plugin without saying why. `xattr` failing on a bundle without the flag is the normal case. The script then kills `AudioComponentRegistrar`, so Logic and GarageBand see the new AU without a log-out.

No wavetable files ship. Presets embed their tables as `TABLE` nodes, the ten built-in tables are compiled in, and the git-ignored local table folders (168 MB) are left out ([Forge oscillators and wavetables](forge-oscillators.md)).

## Signing and what is still missing

The package is signed ad hoc only, with no Developer ID and no notarization. Whoever installs it must allow it once under System Settings > Privacy & Security > Open Anyway. A Developer ID ($99 a year) and notarization are the production step still to do. The package also carries no licence text and no source offer, both of which the AGPL asks of a distributed binary. It is a test build, not the production installer targeted for December 2026 to January 2027 ([Documentation ownership](documentation-ownership.md#release-stage-wording)).

## Gotchas

- **GitHub retired the `macos-14` image.** A run on it is cancelled within two seconds, and its only annotation reads "The configuration 'macos-14-arm64-us-default' is not supported". The pinned JUCE is 8.0.13 (read from the pinned commit's `CMakeLists.txt` on GitHub), which builds with the macOS 15 SDK.

## Status

As of 2026-11-20, the first run on `macos-15` (`22c7b03`) is in progress. Whether the build, the package, the install and auval pass there is **not yet verified**.

## Related

- [Build and test Forge](build-and-test-forge.md)
- [Rhino Forge](forge.md)
- [Forge preset browser and bank](forge-preset-browser.md)
- [Rhino is licensed under the AGPL v3 or later](agpl-licence.md)
