---
title: LNK1104 means a running binary holds the file
type: gotcha
summary: A link that fails with LNK1104 after every file compiled means a running RhinoDAW, DAW or Forge standalone holds the output.
tags: [both, build, windows]
sources: []
updated: 2026-10-10
---

# LNK1104 means a running binary holds the file

## Symptom

`cmake --build native/build --config Release` compiles every translation unit and then fails at the link:

```
LINK : fatal error LNK1104: cannot open file '...\RhinoNative_artefacts\Release\RhinoDAW.exe'
```

A full Forge build fails the same way on `Rhino Forge.vst3`, or on the standalone's `Rhino Forge.exe`.

## Cause

Windows will not let the linker overwrite an image that a running process has loaded. An open RhinoDAW holds `RhinoDAW.exe`. Any host that has loaded Forge holds its `.vst3` for as long as the host runs, and a standalone left open after driving the panel holds its own exe. For Forge this is the normal state of a working session. Rhino's discovery in `native/src/SessionExternalPlugins.cpp` checks the known-plugin list first and then Forge's own build tree (`instruments/rhino-forge/build/RhinoForge_artefacts/<config>/VST3/`), before the system VST3 folders. So the file the linker is trying to write is often the one RhinoDAW has loaded.

## Confirm

- `tasklist | findstr /i rhino` names the holder. Also check for any DAW the user has open.
- Grep the build output for `error C`. If the only error is `LNK1104`, the code compiled and is fine. The compiler diagnostics from the same run can still be trusted.

## What to do

- **Rename the running binary.** Windows refuses to overwrite a running image but lets it be renamed. `Move-Item RhinoDAW.exe RhinoDAW.exe.inuse` frees the name, and the link succeeds while the app keeps running from the renamed file. This worked twice on `RhinoDAW.exe` in September 2026; it has not been tried on a `.vst3`. Tell the user the app in front of them is now the old build, and delete the `.inuse` copy once they close it.
- **For Forge, build the targets that are not locked.** `--target RhinoForgeTests RhinoForge_Standalone` covers everything except the plugin wrapper. Report that the VST3 is out of date, and relink it later.
- **A leftover `Rhino Forge.exe` standalone is safe to kill.** RhinoDAW and a DAW are not. They are the user's working session and may hold unsaved work, so always ask before `taskkill`.
- **Do not configure a second build tree to get round it.** On 2026-10-10, with RhinoDAW holding the file, the user asked for the app to be closed and for no more build directories to be made; `native/build-dj` was deleted and `native/build` is the one tree ([Build and test Rhino](build-and-test-rhino.md)).

## Why it misleads

It comes as a fatal error at the end of a long build, which invites a search through code that compiled cleanly. Other locks look similar but have different owners: an `.obj` or a source file held by a leftover build process ([Build locks from MSBuild nodes and orphaned compilers](orphaned-build-processes.md)). A wall of `LNK2019` errors is a different problem ([A new source file needs an explicit CMake configure](cmake-does-not-reconfigure.md)).

## Related

- [Build and test Rhino](build-and-test-rhino.md)
- [Build and test Forge](build-and-test-forge.md)
- [Hosting Forge in Rhino](forge-hosting.md)
- [Driving Forge's standalone on Windows](driving-the-forge-standalone.md)
- [Build locks from MSBuild nodes and orphaned compilers](orphaned-build-processes.md)
