---
title: Reading a hung RhinoDAW's stacks without a debugger
type: guide
summary: Tell a hang from a crash, find every RhinoDAW including a frozen one with no visible window, read its live threads' stacks with dbghelp from Python, naming unloaded modules and unsymbolled classes by hand, and reproduce a shutdown hang through UI Automation without taking the pointer.
tags: [rhino, debugging, windows, tooling]
sources: []
updated: 2026-10-22
---

# Reading a hung RhinoDAW's stacks without a debugger

A freeze leaves less evidence than a crash, and no debugger is installed on the development machine. This is the method that found [A plugin's static Direct2D images deadlock NVIDIA's driver at unload](plugin-static-images-deadlock-on-unload.md) on 2026-10-12 and confirmed it on 2026-10-22.

## 1. Hang or crash

A hang writes no `Application Error` event. On 2026-10-12 the filter in [Debugging a crash only one project triggers](debugging-a-crashing-project.md) returned nothing. Windows Error Reporting logs a hang instead:

```powershell
Get-WinEvent -FilterHashtable @{LogName='Application'; ProviderName='Windows Error Reporting'; StartTime=(Get-Date).AddHours(-12)} | Select-Object -First 5 | Format-List TimeCreated, Message
```

The freeze showed as `Event Name: AppHangB1` with `P1: RhinoDAW.exe`, and the report listed no dump.

## 2. Find every instance

List them with `Get-Process RhinoDAW` and their `StartTime`. A frozen instance can stay alive with no visible window, and it still holds `RhinoDAW.exe` and every plugin it loaded ([LNK1104 means a running binary holds the file](locked-executable-lnk1104.md)). One whose `rhino.log` session ends at `closing MIDI output: ...` and `Cleaning up temp files..` got through `Application::shutdown` and stuck in a plugin's unload ([A JUCE plugin's DLL is freed at the host's next PeekMessage](plugin-dll-freed-at-next-peekmessage.md)).

`Get-Process` reports `Responding` as True for a process with no main window, so it cannot be trusted. Instead, enumerate the process's top-level windows (`EnumWindows`, `GetWindowThreadProcessId`) and call `IsHungAppWindow` on each. If every window is hung and the CPU is at 0, it is a deadlock, not a busy loop.

There is no dump, so the live process is the only evidence. Read it before anything kills it, and ask the user before killing a RhinoDAW.

## 3. Read the stacks with dbghelp from Python

A Python `ctypes` script does it:

1. Call `SymInitializeW` on the process with the symbol path `srv*<cache in the scratchpad>*https://msdl.microsoft.com/download/symbols`.
2. Walk each thread's context with `StackWalk64`.
3. Name each frame with `SymFromAddrW`.

The traps:

- **Use Visual Studio's dbghelp.** The folder `Common7\IDE\CommonExtensions\Platform\DiagnosticsHub\amd64` in the Visual Studio install holds both `dbghelp.dll` and `symsrv.dll`. System32 has no `symsrv.dll`, so its dbghelp cannot fetch Microsoft's public symbols. Load the DLL by full path.
- **`SizeOfStruct` must be 88.** That is the size of `SYMBOL_INFOW` with a one-character `Name`, however large the buffer behind it. With any other value, `SymFromAddrW` fails silently.
- **Declare 64-bit `argtypes` for every handle.** ctypes otherwise passes a process or thread handle as a 32-bit int.
- **A module being unloaded has already left the module list**, so frames inside it come back unnamed. `VirtualQueryEx` on the address followed by `GetMappedFileNameW` still names the file. That is how `Rhino Forge.vst3` was found in the middle of its `FreeLibrary`.

## 4. Name a class without matching symbols

Forge's symbols could not be rebuilt by relinking: its shared-code library had been rebuilt after the loaded plugin was linked, and the link's tlog was empty. Forge's own RTTI named the classes instead:

- Disassemble the frame's function with Visual Studio's `dumpbin /disasm /range:<start>,<end>`. Give the image's own addresses: the live address, minus the module's load base, plus its preferred base.
- For each vftable the function loads, `vftable[-1]` points to its RTTI complete object locator. That pointer is absolute, at the image's preferred base, so subtract the image base before reading the locator. On x64 the locator holds image-relative offsets, with the type descriptor's at +12. The descriptor's decorated name starts at +16, for example `.?AVDirect2DPixelData@juce@@`.
- Under Git Bash, dumpbin's slash options are rewritten as paths, and `/nologo` was. Set `MSYS_NO_PATHCONV=1`, or run dumpbin from PowerShell ([Development environment](development-environment.md)).

## Reproducing a shutdown hang without the pointer

On 2026-10-22 the close-button ghost was reproduced without touching the developer's pointer, on a scratch copy of a project with Forge on track 1:

- **UI Automation drives JUCE's buttons and menus.** From PowerShell, `System.Windows.Automation` can `Invoke` JUCE `TextButton`s and popup-menu items: the title bar's File, Edit, View and Help are exposed as Buttons and the menu rows as MenuItems. Posted mouse messages are ignored, because JUCE takes `WM_POINTER` input ([Driving Forge's standalone on Windows](driving-the-forge-standalone.md)).
- **Two buttons are named "Edit".** The title bar's Edit menu and the Device View's Edit, which opens the plugin's window, share the name; pick the one well below the title bar. An open menu makes the window modal and swallows the `WM_CLOSE`.
- **Post `WM_CLOSE` to the main window.** That is the close button's path (`closeButtonPressed` → `systemRequestedQuit`). Then check whether the process is still alive some seconds later; 25 s was used.

## A headless replay, never built

A temporary scenario, `DjDeleteHangRepro.inc` (since deleted), replayed the user's steps: a Forge track bound to a deck, two Rhino Arps, then deleting them and the instrument. It had three parts:

- an audio thread driving Tracktion's hosted device and the booth;
- a hand-pumped Win32 message queue, because the runners build with `JUCE_MODAL_LOOPS_PERMITTED` at 0, so `runDispatchLoopUntil` is unavailable there;
- a watchdog that printed stalled threads' stacks through `StackWalk64`.

It was never built, because two running RhinoDAW instances held the exe. Treat it as an untried design, not a working harness.

## Related

- [A plugin's static Direct2D images deadlock NVIDIA's driver at unload](plugin-static-images-deadlock-on-unload.md)
- [A JUCE plugin's DLL is freed at the host's next PeekMessage](plugin-dll-freed-at-next-peekmessage.md)
- [Driving Forge's standalone on Windows](driving-the-forge-standalone.md)
- [Debugging a crash only one project triggers](debugging-a-crashing-project.md)
- [LNK1104 means a running binary holds the file](locked-executable-lnk1104.md)
- [Development environment and reference material](development-environment.md)
- [Tracktion's hosted device is silent until its start is dispatched](hosted-device-silent-until-dispatched.md)
