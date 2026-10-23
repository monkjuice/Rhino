---
title: A build's --parallel does not cap the compiler
type: gotcha
summary: Rhino's and Forge's build trees compile with /MP and no count, so cmake --build --parallel N limits only how many projects build at once and cl.exe runs a file per logical core; on the 16 GB second machine two Forge builds ended in GPU-driver blue screens, and /p:CL_MPCount=2 is the cap.
tags: [both, build, windows, msvc, environment]
sources: []
updated: 2026-10-23
---

# A build's `--parallel` does not cap the compiler

`instruments/rhino-forge/build`'s `RhinoForge*.vcxproj` and `native/build`'s `RhinoNative.vcxproj` compile with
`<MultiProcessorCompilation>true</MultiProcessorCompilation>` and no count (checked 2026-10-23). So
`cmake --build ... --parallel N` limits only how many projects MSBuild builds at once; inside one project `cl.exe`
compiles a file per logical core, sixteen on the second machine, whatever N is. The guides' `--parallel 2` is no
memory cap.

## The cap

```powershell
cmake --build <tree> --config Release --target <target> --parallel 1 -- /p:BuildInParallel=false /p:CL_MPCount=2
```

`CL_MPCount` is MSBuild's cap on `/MP`. On the second machine on 2026-10-23 this ran two `cl.exe` workers of about
220 MB each, left 4.3 GB free and raised no driver events; a rebuild of `RhinoForgeTests` after a `ui/` header change
took about three minutes.

## The signature

On 2026-10-23 the second machine (a Windows 11 Home laptop, RTX 3060 Laptop GPU plus Intel UHD, 16 GB of RAM, NVIDIA
driver 551.86 of March 2024) blue-screened during two Forge builds:

- `VIDEO_TDR_FAILURE` (0x116). Its third parameter, 0xC000009A, is `STATUS_INSUFFICIENT_RESOURCES`: the driver's
  timeout recovery could not get memory.
- `VIDEO_SCHEDULER_INTERNAL_ERROR` (0x119).

Docker/WSL, Ollama, Spotify and others were holding most of the memory: 2.3 GB of 16 GB free at idle.

The build was not the only cause. Recovered driver timeouts (System log, `Display` event 4101 for `nvlddmkm`) came
minutes before the build started, and a 0x119 at 21:10 on 2026-10-22 predated that session. After the user updated the
driver to 32.0.16.2101 and closed those apps, the capped build above ran clean. The cap is the part of that load a
build controls.

**Blue screens or `nvlddmkm` timeouts during a build mean stop and cap it**, and check free memory before the next
attempt. The timeouts are in the System log:

```powershell
Get-WinEvent -FilterHashtable @{LogName='System'; Id=4101; StartTime=(Get-Date).AddHours(-6)}
```

## Related

- [Build and test Forge](build-and-test-forge.md)
- [Build and test Rhino](build-and-test-rhino.md)
- [Development environment and reference material](development-environment.md)
- [Build locks from MSBuild nodes and orphaned compilers](orphaned-build-processes.md)
