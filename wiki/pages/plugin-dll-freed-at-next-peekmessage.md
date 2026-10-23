---
title: A JUCE plugin's DLL is freed at the host's next PeekMessage
type: gotcha
summary: JUCE's Desktop hooks the message thread with the plugin's own module handle, so the host's FreeLibrary is not the last reference and the plugin detaches inside whatever next pumps messages, DestroyWindow or OleUninitialize; by then JUCE's GUI singletons are gone and only the plugin's C++ statics are left to run under the loader lock.
tags: [both, forge, vst3, hosting, juce, windows]
sources: []
updated: 2026-10-22
---

# A JUCE plugin's DLL is freed at the host's next PeekMessage

Found on 2026-10-22 from the live stacks of two RhinoDAW processes stuck after being closed, and read in the pinned JUCE (`37c894f83d379179b2070d437ccd0f1cd9af9576`). It is why Forge's detach ran where it did in [A plugin's static Direct2D images deadlock NVIDIA's driver at unload](plugin-static-images-deadlock-on-unload.md).

## The hook keeps the DLL

1. `Desktop::NativeDarkModeChangeDetectorImpl` (`juce_Windowing_windows.cpp`) calls `SetWindowsHookEx(WH_CALLWNDPROC, ...)` with the module handle of the DLL JUCE is compiled into, Forge's own, for the message thread. The hook holds a reference on that DLL.
2. When JUCE unhooks, Windows does not drop that reference at once. It defers it to the thread's next `PeekMessage`, where USER32's `_ClientFreeLibrary` calls `FreeLibrary`.
3. So the host's own `FreeLibrary` is not the last one. The plugin's `DLL_PROCESS_DETACH` runs, under the loader lock, inside whatever next pumps messages on that thread.

Two such places were seen during RhinoDAW's shutdown:

- `NtUserDestroyWindow` → MSCTF `CtfImeDestroyThreadMgr` → `SYSTHREAD::DestroyThreadMessageWindow` → `PeekMessageW` (2026-10-12, and again in a reproduction on 2026-10-22).
- After `Application::shutdown` returns, JUCE's `MessageManager` shutdown calls `OleUninitialize` → `CoUninitialize` → combase `PeekTillDone` → `PeekMessageW` (the user's stuck process on 2026-10-22).

Both are later than `rhino.log`'s last lines, `closing MIDI output: ...` and `Cleaning up temp files..`. A RhinoDAW still running with no window, whose log ends there, is stuck in a plugin's detach.

## What is left to run at the detach

Only the plugin's C++ statics, run by ucrtbase's `execute_onexit_table`. JUCE's GUI singletons are already gone. In the pinned VST3 wrapper (`juce_audio_plugin_client_VST3.cpp`) the `ScopedJuceInitialiser_GUI` lives in `ScopedRunLoop` members of `JuceVST3EditController`, `JuceVST3Component` and `JuceVST3Editor`, not in a static, and `DllMain` only records the module handle on attach. So `shutdownJuce_GUI`, and every `DeletedAtShutdown` object with it (`ImageCache`'s among them), runs on the host's message thread when the last plugin object goes, outside the loader lock.

So anything whose release may wait on another thread, a GPU object above all, belongs to a plugin object or a JUCE singleton, never to a C++ static.

## Related

- [A plugin's static Direct2D images deadlock NVIDIA's driver at unload](plugin-static-images-deadlock-on-unload.md)
- [Reading a hung RhinoDAW's stacks without a debugger](reading-a-hung-process.md)
- [Hosting Forge in Rhino](forge-hosting.md)
