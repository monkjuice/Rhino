---
title: Editing only a scenario .inc does not rebuild the tests
type: gotcha
summary: An edit to a workflow scenario alone has left the old test binary in place, so CTest passed a scenario that was never compiled.
tags: [rhino, forge, testing, build]
sources: []
updated: 2026-10-10
---

# Editing only a scenario .inc does not rebuild the tests

## Symptom

You change a workflow scenario, build, run `ctest`, and the case reports **Passed**. But the binary that ran does not contain your change. On 2026-09-28 a scenario passed that had never been compiled.

The mirror image is as misleading. On 2026-10-10 a new check in `DjBooth.inc` and the code it tests were written together and `ctest` was run against the binary built before either; it reported the check failing, and a rebuild was the whole fix. A failure in a check you have just written is first a question of whether the binary is newer than the edit.

## Cause

Scenarios are bare statement blocks in `native/src/tests/Pattern/scenarios/*.inc` and `native/src/tests/Arrangement/scenarios/*.inc`. Each is included inside the runner function in that folder's `WorkflowTest.cpp`, and none appears in any CMakeLists. When only an `.inc` had changed, the build compiled nothing and CTest ran the previous executable. Why the build missed the change is not established, since MSBuild's file tracker normally records included files as inputs. The habits below guard against every route to a stale binary.

A second route produces the same green line. If you pipe the build through a filter (`cmake --build ... | grep error`), the shell reports the filter's exit status, not the build's. A failed link, for example [LNK1104](locked-executable-lnk1104.md), then looks clean and the old executable runs. This hit Forge on 2026-10-03: `cmake --build ... | Select-String` followed by `ctest` in the same PowerShell command ran the previous binary after a failed compile and reported 100% passed.

A third route caught a Forge check on 2026-10-03. To prove a test could fail, a source file was edited temporarily and then restored with `Copy-Item` from a backup. `Copy-Item` keeps the backup's older `LastWriteTime`, so the restored file looked older than the object compiled from the temporary edit. MSBuild skipped it, and the binary kept the temporary behaviour, so a snapshot showed the bug "still there".

## What to do

- After editing a scenario, touch its runner so the build has a source file to recompile:

  ```
  touch native/src/tests/Arrangement/WorkflowTest.cpp
  ```

  (or `native/src/tests/Pattern/WorkflowTest.cpp`).
- After a temporary edit, restore the file by editing it back. If you copy it back, touch it before rebuilding (`(Get-Item <file>).LastWriteTime = Get-Date`). This applies to Forge and Rhino alike.
- Check the build's own exit status. Redirect its output to a log, read `$?` (or `$LASTEXITCODE` in PowerShell), then grep the log.
- Check that the run reached your scenario. Each runner writes a line to stderr before every scenario, `Arrangement scenario: <name>` or `Pattern scenario: <name>`, using the name given to `scenario(...)` in `WorkflowTest.cpp`. `ctest --output-on-failure` prints nothing for a passing case, so run the case with `-V` and look for the line:

  ```
  ctest --test-dir native/build -C Release -R native_arrangement_workflow -V
  ```

Adding a new scenario means adding its `scenario(...)` line and `#include` to the runner, and that edit recompiles the runner anyway. The risk is in the later edits to the same `.inc`.

## Why it misleads

Every other failure in this suite is loud. This one shows up as a pass, and nobody re-checks a pass.

## Related

- [Writing Rhino tests](writing-rhino-tests.md)
- [Build and test Rhino](build-and-test-rhino.md)
- [Build and test Forge](build-and-test-forge.md)
- [A new source file needs an explicit CMake configure](cmake-does-not-reconfigure.md)
- [LNK1104 means a running binary holds the file](locked-executable-lnk1104.md)
- [Build locks from MSBuild nodes and orphaned compilers](orphaned-build-processes.md)
