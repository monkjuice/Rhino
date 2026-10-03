---
title: A CTest SegFault may be a stack overflow
type: gotcha
summary: CTest reports a Windows stack overflow (0xC00000FD) as a bare SegFault, and both test binaries put large objects on the stack.
tags: [both, testing, windows]
sources: []
updated: 2026-10-03
---

# A CTest SegFault may be a stack overflow

## Symptom

CTest reports a case as `***Exception: SegFault` and shows no output at all. The crash is often in code nowhere near the last change.

## Cause

When a thread overflows its stack, Windows ends the process with `0xC00000FD` (`STATUS_STACK_OVERFLOW`, exit code -1073741571), and CTest reports that as "SegFault". The overflow usually happens before anything is printed. Both test binaries build large objects as local variables:

- **Forge.** A `Processor` holds a `Core` by value. A `Core` holds sixteen voices (`std::array<Voice, 16>` in `core/ForgeCore.h`), and each voice carries real state. `oscillatorSuite()` in `tests/ForgeTestsOscillator.cpp` keeps eight `Processor`s alive on one stack frame to compare their measurements. That sat a few kilobytes under the 1 MB stack a Windows console app gets, until the filter's per-voice state pushed it over. The plugin was never at risk, because a host allocates the `Processor`.
- **Rhino.** Every workflow scenario shares its runner function's stack frame. A `StepGrid` carries about 200 KB of note caches. The third one declared across the scenarios crashed the runner partway through `Rendering.inc`, far from the scenario that added it (`native/src/tests/README.md`).

## Confirm

The Application event log records the exception code, which separates an overflow from a null dereference:

```
Get-WinEvent -FilterHashtable @{LogName='Application'; ProviderName='Application Error'; StartTime=(Get-Date).AddMinutes(-20)}
```

Read the real exit code too. In bash, `RhinoForgeTests.exe --oscillator | head -20; echo $?` prints the exit status of `head`, not of the test. A crashing suite showed 0 that way and looked like a pass. Use `$LASTEXITCODE` in PowerShell, or redirect the output to a file instead of piping it.

## What was done, and what to do

- `RhinoForgeTests` links with `/STACK:8388608` (8 MB), declared beside `juce_add_console_app` in `instruments/rhino-forge/CMakeLists.txt`. The fix belongs to the binary that builds synths as locals, so no suite has to change.
- The spectral oscillator's per-voice state, about 3.5 MB per `Core`, lives in one heap block sized at `prepare()`, partly so that it cannot overflow these frames ([Forge spectral oscillator](forge-spectral.md)).
- Rhino's runners are compiled into the app, which keeps the default 1 MB stack. Declare a `StepGrid`, an `Arrangement` or anything of similar size with `std::make_unique`.
- If the overflow comes back in Forge, suspect whatever was last added to `Voice` or `Core` before suspecting a pointer.

A SegFault in a Rhino scenario can also be a genuine dangling pointer. The intermittent crash in `native_arrangement_workflow` turned out to be a raw `te::Clip*` used after an undo had rebuilt the clips ([Hold ids, not pointers](ids-not-pointers.md)).

## Related

- [Writing Rhino tests](writing-rhino-tests.md)
- [Build and test Forge](build-and-test-forge.md)
- [Forge engine (Core)](forge-engine.md)
- [Note editor (StepGrid)](note-editor.md)
- [Debugging a crash only one project triggers](debugging-a-crashing-project.md)
