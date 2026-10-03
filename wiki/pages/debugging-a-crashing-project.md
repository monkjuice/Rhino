---
title: Debugging a crash only one project triggers
type: guide
summary: Turn a project file that crashes Rhino into a ten-second repro, name the faulting module, and bisect the XML against a control.
tags: [rhino, debugging, project-files, windows]
sources: []
updated: 2026-10-03
---

# Debugging a crash only one project triggers

`RhinoDAW.exe` opens a `.rhinoedit` passed as its only argument (`Application::initialise` in `native/src/Main.cpp`),
which turns "it crashes when I open my project" into a repro that needs no clicking and runs in about ten seconds.
This is the method of the *Debugging a crash that only one project file triggers* section of `AGENTS.md`.

## Steps

1. **Reproduce from the command line**, on a copy in the scratchpad so bisecting never touches the original:
   ```powershell
   & ".\native\build\RhinoNative_artefacts\Release\RhinoDAW.exe" "<scratch>\broken.rhinoedit"
   ```
2. **Name the faulting module before blaming anything.**
   ```powershell
   Get-WinEvent -FilterHashtable @{LogName='Application'; ProviderName='Application Error'; StartTime=(Get-Date).AddHours(-6)}
   ```
   A project that loads a VST invites blaming the VST; `RhinoDAW.exe` faulting in `RhinoDAW.exe` rules that out in one
   step. The entry also gives a fault offset: the same offset on every attempt means a deterministic crash worth
   bisecting, not a race.
3. **Bracket it with `%APPDATA%\Rhino\rhino.log`.** `ProjectFiles::load` logs `Rhino: Opening <name>...` before the
   load and `Rhino: Opened <name>` after it, so the first without the second puts the fault inside
   `Session::restoreProject`. Another project opening fine in the same session says the data, not the build, is the
   trigger.
4. **Bisect the XML** one structure at a time — notes, plugin nodes, plugin state, tracks — re-running after each cut.
   Run a project that opens in every round as a control: a harness that silently stops reproducing is worse than none.
   If the log says `This is not a supported Rhino native project.`, the cut broke the XML or the root's
   `rhinoFormatVersion`; it cured nothing.
5. **Re-run before believing a non-crash.** One clean launch is noise: the app can sit on a dialog or lose a race and
   look like success. Repeat the same file, and run the good and bad files in both orders. One such false pass nearly
   sent the original investigation at the wrong file.

## The case it was written for

A project with a single track crashed; the demo, with four, opened. Removing every `<NOTE>` and the whole VST node, and
switching `rhinoPatternInstrument`, changed nothing, which pointed at the track count. `restoreProject` read track 1's
Utility device behind a `tracks.size() > 1` guard, but the branch that created a missing one wrote to `tracks[1]`
unguarded (fixed in commit 93502da, 2026-09-16).

`juce::Array::operator[]` returns a default-constructed value for an out-of-range index instead of asserting in
Release, so an out-of-bounds track or plugin lookup surfaces later as a null dereference somewhere else. When a guard
protects one access to an index, check every other access to the same index.

## Fix the save path, not the old file

Rhino owes older `.rhinoedit` documents nothing. When a project fails to open or plays wrong, fix what a current save
writes, or what the load re-derives that the document already records, rather than adding a tolerance for an old shape
([No backward compatibility for .rhinoedit](no-rhinoedit-back-compat.md)).

## Related

- [Project files (.rhinoedit)](project-files.md)
- [No backward compatibility for .rhinoedit](no-rhinoedit-back-compat.md)
- [Build and test Rhino](build-and-test-rhino.md)
- [Reproducing a live timing bug offline](reproducing-live-timing-offline.md)
- [The executable is RhinoDAW, not Rhino](executable-named-rhinodaw.md)
