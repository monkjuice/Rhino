---
title: Rhino's build targets
type: component
summary: Rhino builds as three targets (RhinoCore, RhinoDevices, the app) so that editing a device does not rebuild the app.
tags: [rhino, build, cmake, architecture]
sources: []
updated: 2026-10-06
---

# Rhino's build targets

`native/src` builds as three CMake targets so that editing one part does not rebuild the others. Each has its own explicit source list; nothing is globbed.

| Target | Source list | Links | Holds |
| --- | --- | --- | --- |
| `RhinoCore` (static) | `native/src/core/CMakeLists.txt` | `juce::juce_core` only | `ContentLibrary`, `SystemUsage`, the DSP of Rhino Tune, Rhino EQ, Rhino Vocoder, Rhino FM and the Drum Rack, and the `.rnd`, `.rdk` and `.rdp` file formats |
| `RhinoDevices` (static) | `native/src/devices/CMakeLists.txt` | `RhinoCore`, Tracktion | Rhino's plugins under `instruments/`, `audio/`, `midi/`, and `DeviceCatalog` |
| `RhinoNative` (app) | `native/CMakeLists.txt` | both, plus JUCE and Tracktion | `Session`, the UI, the shell, and every test runner |

## The include rules, and why

A device may not include `Session.h` or any UI header, and `Session.h` reaches the device library only through `DeviceCatalog.h` (it forward-declares `UtilityDevice`). Until commit `24826df` (2026-09-19) `Session.h` included all six device headers, so editing any device rebuilt nearly every translation unit. `RhinoCore` likewise knows nothing of Session, the UI or Tracktion, so both other targets can link it without depending on each other.

The rule is narrower than "the app never sees a device header". `native/src/SessionInternal.h`, private to the `Session*.cpp` files, includes the Utility and Drum Rack headers and the SDK's. The `DeviceEditorPanel*.cpp` faces include the five devices they draw, and `DeviceRack.cpp` includes the Drum Rack's to find a dropped sound's pad. Editing one of those headers rebuilds those files, not the whole app.

Inside `RhinoCore` the EQ, Tune, Vocoder and FM DSP use the standard library alone, which is what keeps them testable without an engine. `juce_core` is included by `ContentLibrary`, the file formats (`DevicePreset`, `DrumKitFile`, `ExactFloatText.h`), `DeviceDisplay.h` and `DrumSynth.h`, which `DrumRackEngine` includes in turn (checked 2026-10-06). Only the `devices/` root is a public include directory, so an include names the kind: `#include "audio/RhinoEqDevice.h"`.

## The app target

`juce_add_gui_app(RhinoNative ...)` produces `RhinoDAW.exe` ([The executable is RhinoDAW, not Rhino](executable-named-rhinodaw.md)). The runners in `native/src/tests/` are compiled into that binary, and each CTest case runs it with a flag such as `--self-test`. Its only JUCE binary data is the two Inter fonts ([Content is files, never compiled in](content-is-files.md)). The app and `RhinoCore` are compiled with `RHINO_SOURCE_DIR`, the absolute path of `native/`, which is how a development build finds `library/` and the Forge VST3 without a copy step.

## Pitfalls

- A new `.cpp` needs a line in one of the three lists and then an explicit configure: `CMAKE_SUPPRESS_REGENERATION ON` means `cmake --build` never re-runs CMake, so the file silently never compiles ([A new source file needs an explicit CMake configure](cmake-does-not-reconfigure.md)).
- `RhinoCore` holds `ContentLibrary`, `SystemUsage`, the pitch-correction DSP, `EqEngine`, `SpectrumAnalyser`, `VocoderEngine`, `FmEngine`, `DrumSynth`, `DrumRackEngine`, `DrumKitFile` and `DevicePreset` (checked 2026-10-06).

## Related

- [Dependency direction](dependency-direction.md)
- [Device catalog](device-catalog.md)
- [Adding a device to Rhino](adding-a-device.md)
- [Keeping files small](keeping-files-small.md)
- [Build and test Rhino](build-and-test-rhino.md)
