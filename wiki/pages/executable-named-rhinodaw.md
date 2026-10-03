---
title: The executable is RhinoDAW, not Rhino
type: decision
summary: The binary is RhinoDAW.exe because NVIDIA's driver profile for Rhinoceros 3D, keyed on Rhino.exe, corrupted the app's Direct2D repaints.
tags: [rhino, windows, rendering, debugging]
sources: []
updated: 2026-10-03
---

# The executable is RhinoDAW, not Rhino

`PRODUCT_NAME "RhinoDAW"` in `native/CMakeLists.txt` decides the file on disk: `native/build/RhinoNative_artefacts/Release/RhinoDAW.exe`. The name a person sees is still Rhino, because it comes from `Application::getApplicationName()` in `native/src/Main.cpp`. Commit `9bedbdb` (2026-09-18) made the change. The long form is `AGENTS.md`, *When the same source renders differently on two machines*.

## Context

On the developer's desktop every interaction left stale regions behind, and they piled up until the window was unreadable. A laptop built from the same source rendered correctly. Graphics drivers carry per-application profiles keyed on the executable's file name and apply them to any binary that matches. NVIDIA ships one for Rhinoceros 3D, the CAD package, whose executable is `Rhino.exe`. Its settings break this app's Direct2D repaints.

## Decision

Rename the binary, not the product. Nothing else changed.

## Alternatives considered

- **Treat it as an OS or driver regression.** An OS update and a GPU driver update both landed near the right dates. Both were red herrings.
- **Leave Direct2D.** `RHINO_RENDERER=software` switches to JUCE's software rasteriser, which keeps one persistent surface and so renders correctly where a damage-tracking fault shows. It was kept as a diagnostic escape hatch rather than adopted: it is what separated a backend fault from Rhino's own painting. The engine in use is logged at startup (`Rhino: rendering engine ...`).

## How the cause was proven

- **Copy the binary under a second name before theorising.** A byte-identical copy named `Theta.exe` rendered correctly while `Rhino.exe` corrupted. Hashing both files is the whole proof.
- **Read the profile database.** Search `%ProgramData%\NVIDIA Corporation\Drs\*.bin` for the executable name encoded as UTF-16LE. `rhino.exe` is there beside CATIA, Teamcenter and Houdini.
- **Treat a working machine as a control.** The laptop has hybrid graphics, so a 2D app runs on the Intel iGPU and no NVIDIA profile is ever consulted. Comparing the two machines' GPUs is what brought the driver into the picture.
- **Prefer a control you can run to a correlation you can only argue.** What settled it was restoring the old binary and running it beside the new one.

## Consequences

- Every command line, script and log refers to `RhinoDAW.exe`: the run commands in `README.md`, the headless repro in [Debugging a crash only one project triggers](debugging-a-crashing-project.md), and the `tasklist` check in [LNK1104 means a running binary holds the file](locked-executable-lnk1104.md).
- The Windows Application event log names the faulting application `RhinoDAW.exe`, so search it for that name, not `Rhino.exe`.
- Any future rename of the binary needs the same check against the driver's profile database.

## Related

- [App shell and control bar](app-shell.md)
- [Playhead rendering](playhead.md)
- [Build and test Rhino](build-and-test-rhino.md)
- [Debugging a crash only one project triggers](debugging-a-crashing-project.md)
- [Tracktion Engine with a native JUCE UI](tracktion-and-juce.md)
