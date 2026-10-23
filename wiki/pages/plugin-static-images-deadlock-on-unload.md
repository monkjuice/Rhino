---
title: A plugin's static Direct2D images deadlock NVIDIA's driver at unload
type: gotcha
summary: A juce::Image with static storage in a plugin is a Direct2D bitmap released by the DLL's detach under the loader lock, where NVIDIA's driver waits forever; four in Forge's UI froze RhinoDAW at shutdown, leaving it running with no window, until commit f2a972e (2026-10-22) moved them into JUCE's ImageCache.
tags: [both, forge, vst3, hosting, juce, windows]
sources: []
updated: 2026-10-22
---

# A plugin's static Direct2D images deadlock NVIDIA's driver at unload

## Symptom

- 2026-10-12: RhinoDAW froze rather than crashed. The Application log held an `AppHangB1` for `RhinoDAW.exe`, no `Application Error` and no dump, and a later instance stayed running with no visible window, every window hung, CPU at 0.
- 2026-10-22: the user reported that closing RhinoDAW with the window's close button very often left a RhinoDAW process running with no window. It holds `RhinoDAW.exe` and Forge's VST3, so builds fail with LNK1104 ([LNK1104 means a running binary holds the file](locked-executable-lnk1104.md)), and it had to be killed from the command line. Its `rhino.log` ends at `closing MIDI output: ...` and `Cleaning up temp files..`.

The live stacks were read as in [Reading a hung RhinoDAW's stacks without a debugger](reading-a-hung-process.md): one process on 2026-10-12, and on 2026-10-22 the user's (PID 17036) and a reproduction's. All show the same deadlock.

## Cause: a loader-lock deadlock

1. Forge's DLL is freed inside a message pump during shutdown, not by the host's own `FreeLibrary` ([A JUCE plugin's DLL is freed at the host's next PeekMessage](plugin-dll-freed-at-next-peekmessage.md)). Its `DLL_PROCESS_DETACH` runs its C++ function-local statics (ucrtbase `execute_onexit_table`) under the loader lock.
2. Four of those were Direct2D-backed `juce::Image`s (on Windows a plain image is one: [Profile paint on a software image](profile-paint-on-a-software-image.md)): the `brushedGrain()` tile and `fastenerImage()`'s `cross` and `dome` in `instruments/rhino-forge/ui/ForgeChrome.h`, and the rescaled wheel in `ui/ForgePerformanceWheel.h`. They existed only once the editor had painted.
3. Releasing the last one released the `ID3D11Device`. The 2026-10-12 disassembly shows the path through a `std::shared_ptr` (a two-counter control block at +8 and +0xC) and a `juce::ReferenceCountedObject` (its vftable loaded); those two links come from disassembly, not symbols.
4. `nvwgf2umx.dll`'s `DestroyDriverInstance` waits in `WaitForSingleObjectEx` for its own worker threads, which sit in `LdrShutdownThread` / `FreeLibraryAndExitThread` waiting for the loader lock. About twenty other threads wait in `LdrpDrainWorkQueue`.

Seen with NVIDIA's D3D11 driver only; no other driver has been checked. The DJ live chain plays no part.

## Keeping the module loaded is no fix

Forge links the dynamic runtime (`/MD`). With it, `__scrt_dllmain_uninitialize_c` (`crt/src/vcruntime/utility.cpp` in the Visual C++ runtime source) runs a DLL's onexit table on every detach, process termination included. So pinning plugin modules in Rhino would only move the freeze to exit.

## The fix: commit f2a972e, 2026-10-22

Neither option proposed on 2026-10-12 (a holder tied to the editors' lifetime, or software images). The images live in JUCE's `ImageCache`, as Merli's textures in `ui/ForgeMerliMaterial.h` already did:

- `fastenerImage()` fetches `ImageCache::getFromMemory` on every call and returns by value; the dome is trimmed with `getClippedImage` per call.
- `brushedGrain()` and the new `performanceWheelArt()` (`ui/ForgePerformanceWheel.h`) go through `cachedImage(key, make)` in `ui/ForgeChrome.h`, under `grainImageKey` and `wheelImageKey` (`0x46f0'0000'0000'0001` and `...0002`). No user-space address sets those top bits, and `getFromMemory` keys an asset by its data pointer, so the two kinds of key cannot meet.
- The cache is safe because its `Pimpl` is a `DeletedAtShutdown` singleton, emptied on the host's message thread when the last plugin object goes. While Forge runs, it lets go of an image that only the cache holds after 5000 ms unused (its timer runs every 2000 ms), and `cachedImage` makes it again.

The rule is written in `ForgeChrome.h`: never give a `juce::Image` static storage in Forge ([Forge editor (panel)](forge-editor.md)).

`releasedImagesSuite` in `tests/ForgeTestsTheme.cpp` (area `theme`) paints every page, closes the panel, marks pixel (0,0) of each image while held, calls `ImageCache::releaseUnusedImages` (which drops every entry at refcount ≤ 1 at once), and requires a fresh fetch to lack the mark. Pinning `brushedGrain` in a static inside the test made it fail ("the brushed grain outlived everything that drew it").

## Verified

- All ten `--snapshot` PNGs (OSC, TABLE, MATRIX, MIX, FX in both themes, 1440x900) were byte-identical to the previous build, and `--profile 1440 900` medians over seven alternating rounds unchanged (tab walk 72.6 ms against 74.8, idle 13.4 against 13.5).
- End to end: a scratch copy of a project with Forge on track 1, Forge's window opened through View > Device View and the rack's Edit, then `WM_CLOSE` posted to the main window (the close button's path: `closeButtonPressed` → `systemRequestedQuit`). The old VST3 was still running 25 s later with no window (1 of 1, its stacks showing the deadlock). The new one exited with code 0 in 3 of 3 runs where Forge's window opened, on two projects. A fourth run, the first load after the relink, whose window never reported open, does not count.

## Not proven

The user's original freeze, deleting Forge from the rack after opening its window (2026-10-12, 18:30 session), is still only believed to be the same deadlock. It was not retested after the fix.

## Related

- [A JUCE plugin's DLL is freed at the host's next PeekMessage](plugin-dll-freed-at-next-peekmessage.md)
- [Reading a hung RhinoDAW's stacks without a debugger](reading-a-hung-process.md)
- [Hosting Forge in Rhino](forge-hosting.md)
- [Rhino Forge](forge.md)
- [Forge editor (panel)](forge-editor.md)
- [Profile paint on a software image](profile-paint-on-a-software-image.md)
- [The executable is RhinoDAW, not Rhino](executable-named-rhinodaw.md), the other NVIDIA driver trap
