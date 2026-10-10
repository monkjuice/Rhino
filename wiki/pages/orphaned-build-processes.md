---
title: Build locks from MSBuild nodes and orphaned compilers
type: gotcha
summary: An object or source file that another process holds is usually another session's build, an idle MSBuild node or an orphaned cl.exe.
tags: [both, build, windows]
sources: []
updated: 2026-10-10
---

# Build locks from MSBuild nodes and orphaned compilers

## Symptom

Two failures that look like code or permission problems:

- the build stops with `MSB6003 ... WorkflowTest.cpp.obj ... used by another process`;
- writing a plainly writable source file fails with `EPERM: operation not permitted, rename` from an editing tool, or `PermissionError` from Python.

## Cause

Another process has the file open. Three owners have been found on the development machine, and killing the wrong one breaks your own build:

1. **Another agent session's build.** A `cmake.exe --build native/build` that you did not start belongs to a second session working in the same tree. Its parent shell's command line names that session's own `shell-snapshots/snapshot-bash-<id>.sh`.
2. **Idle MSBuild nodes.** `MSBuild.exe ... /nodemode:1 /nodeReuse:true` with a dead parent is a reusable node, not an orphan. The next build picks it up, so a `cl.exe` under it may be compiling *your* build. Tens of seconds of CPU (`(Get-Process -Id <pid>).CPU`) means it is working. These idle nodes cause the short-lived `.obj` lock.
3. **Orphaned compilers.** A `cl.exe` or `Tracker.exe` whose MSBuild parent has died holds the sources it was reading, and nothing will ever close those handles. On 2026-09-22 nine came from an agent's tool timeout during a full Forge rebuild, which takes about eight minutes. The timeout killed MSBuild and stranded its compilers.

## Confirm

List the build processes, with their parents and command lines, before touching anything:

```
Get-CimInstance Win32_Process -Filter "Name='cmake.exe' OR Name='MSBuild.exe' OR Name='cl.exe' OR Name='Tracker.exe'" | Select-Object ProcessId, ParentProcessId, Name, CreationDate, CommandLine
```

`[IO.File]::Open($f,'Open','ReadWrite','None')` throws when another handle holds the file. A `cl.exe` that is tens of minutes old but has used only a few seconds of CPU is hung, not busy.

## What to do

- Set `MSBUILDDISABLENODEREUSE=1` before `cmake --build`. With node reuse off, the `.obj` lock does not occur.
- Start any Forge build larger than one translation unit in the background, rather than letting a foreground call time out.
- If one `.obj` is still unwritable after every process has gone, nothing holds it. Delete it and build again.
- A dead run locks only the files it was compiling, so finish the rest of the change first.
- Ask before killing a process you did not start, because the user and other sessions share the machine. Killing a confirmed orphan loses nothing: the next build regenerates its object files.
- Do not build into a separate `native/build-*` directory to get round a lock. Nothing would contend there, but JUCE and Tracktion build from scratch, which takes minutes, and the user asked on 2026-10-10 that only `native/build` exist (`native/build-dj` was deleted; [Build and test Rhino](build-and-test-rhino.md)).

## Related

- [LNK1104 means a running binary holds the file](locked-executable-lnk1104.md)
- [Build and test Rhino](build-and-test-rhino.md)
- [Build and test Forge](build-and-test-forge.md)
- [Development environment and reference material](development-environment.md)
