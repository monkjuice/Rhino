---
title: A plugin's static Direct2D images deadlock NVIDIA's driver at unload
type: gotcha
summary: A juce::Image with static storage in a plugin is a Direct2D bitmap released by the DLL's detach under the loader lock, where NVIDIA's driver waits forever; four in Forge's UI froze RhinoDAW on 2026-10-12, and the fix belongs in Forge.
tags: [both, forge, vst3, hosting, juce, windows]
sources: []
updated: 2026-10-12
---

# A plugin's static Direct2D images deadlock NVIDIA's driver at unload

## Symptom

On 2026-10-12 RhinoDAW froze rather than crashed. The Application log held an `AppHangB1` for `RhinoDAW.exe`, with no `Application Error` event and no dump. A later instance, started at 18:43:54, was still running with no visible window. Every window it owned was hung, and its CPU was at 0. Its live stacks show the mechanism below; they were read as described in [Reading a hung RhinoDAW's stacks without a debugger](reading-a-hung-process.md).

## Cause: a loader-lock deadlock

1. The main thread was destroying a window during shutdown (`NtUserDestroyWindow`). The MSCTF text-services hook called `PeekMessageW`, the kernel delivered USER32's `_ClientFreeLibrary`, and that called `FreeLibrary` on `Rhino Forge.vst3`.
2. Forge's `DLL_PROCESS_DETACH` ran its static destructors (ucrtbase `execute_onexit_table`) under the loader lock. One destroyed a `juce::Direct2DPixelData`. From there the chain ran to the `ID3D11Device`'s `Release` through what the disassembly shows as a `std::shared_ptr` and a `juce::ReferenceCountedObject`. These two links come from the disassembly, not from symbols. The destructor decrements a two-counter control block at +8 and +0xC, which is `std::shared_ptr`'s pattern. A later frame's function loads `juce::ReferenceCountedObject`'s vftable. The driver's teardown (`NDXGI CDevice DestroyDriverInstance`) went into `nvwgf2umx.dll`, which waits in `WaitForSingleObjectEx` forever.
3. A dozen other threads sat in `LdrShutdownThread` and `LdrpDrainWorkQueue`, waiting for the loader lock the main thread held.

The images are four function-local statics in Forge's UI. On Windows a plain `juce::Image` is Direct2D-backed ([Profile paint on a software image](profile-paint-on-a-software-image.md)).

- In `instruments/rhino-forge/ui/ForgeChrome.h`: the `brushedGrain()` tile, and `fastenerImage()`'s `cross` and `dome`.
- In `instruments/rhino-forge/ui/ForgePerformanceWheel.h`: `wheel`, in `ForgePerformanceWheel::paint`.

They exist only once Forge's editor has painted. The deadlock was seen with NVIDIA's D3D11 driver; no other driver has been checked. The DJ live chain, which the user's steps also used, plays no part.

## Keeping the module loaded is no fix

Forge links the dynamic runtime (`/MD`). With it, `__scrt_dllmain_uninitialize_c` (`crt/src/vcruntime/utility.cpp` in the Visual C++ runtime source) runs a DLL's onexit table on every detach, process termination included. So pinning plugin modules in Rhino would only move the freeze to exit. It is hardening at most.

## The fix: proposed 2026-10-12, not yet applied

The fix is a recommendation; the user has not chosen one. The change belongs in Forge. There are two options: tie the images to the editor's lifetime, for example with a shared holder the editors own, or make them software images (`juce::SoftwareImageType`). In a plugin, never give a Direct2D-backed `juce::Image` static storage.

## Not proven

The user's original freeze was deleting Forge from the rack after opening its window, in the 18:30 session. It is believed, not proven, to be the same deadlock. That depends on whether JUCE's VST3 hosting frees the module when the last instance goes, which was still being researched.

## Related

- [Reading a hung RhinoDAW's stacks without a debugger](reading-a-hung-process.md)
- [Hosting Forge in Rhino](forge-hosting.md)
- [Rhino Forge](forge.md)
- [Forge editor (panel)](forge-editor.md)
- [Profile paint on a software image](profile-paint-on-a-software-image.md)
- [The executable is RhinoDAW, not Rhino](executable-named-rhinodaw.md), the other NVIDIA driver trap
