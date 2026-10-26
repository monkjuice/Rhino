---
title: Rhino's build targets
type: component
summary: Rhino builds as three targets (RhinoCore, RhinoDevices, the app) so that editing a device does not rebuild the app.
tags: [rhino, build, cmake, architecture]
sources: []
updated: 2026-10-26
---

# Rhino's build targets

`native/src` builds as three CMake targets so that editing one part does not rebuild the others. Each has its own explicit source list; nothing is globbed. Since commit `b68fa0e` (2026-10-13) the folders under `native/src` follow the targets and, inside the app, the layers ([The source tree mirrors the layers](source-tree-mirrors-the-layers.md)).

| Target | Source list | Links | Holds |
| --- | --- | --- | --- |
| `RhinoCore` (static) | `native/src/core/CMakeLists.txt` | `juce::juce_core` only | Three folders by responsibility: `dsp/` (the DSP of Rhino Tune, Rhino EQ, Rhino Vocoder, Rhino FM and the Drum Rack), `dj/` (the booth's engine) and `content/` (`ContentLibrary` and the `.rnd`, `.rdk` and `.rdp` file formats); `SystemUsage` and `DeviceDisplay.h` at the root |
| `RhinoDevices` (static) | `native/src/devices/CMakeLists.txt` | `RhinoCore`, Tracktion | Rhino's plugins under `instruments/`, `audio/`, `midi/`, the DJ booth's hidden track ends under `infra/` (since 2026-10-12; they reach the booth only through `core/dj/DjTrackPort.h`), and `DeviceCatalog` |
| `RhinoNative` (app) | `native/CMakeLists.txt` | both, plus JUCE and Tracktion | `Session` (`src/model/`), the UI (`src/ui/<view>/` and `src/ui/shared/`), the shell (`src/app/`), and every test runner (`src/tests/`) |

## The include rules, and why

A device may not include `Session.h` or any UI header, and `Session.h` reaches the device library only through `DeviceCatalog.h` (it forward-declares `UtilityDevice`). Until commit `24826df` (2026-09-19) `Session.h` included all six device headers, so editing any device rebuilt nearly every translation unit. `RhinoCore` likewise knows nothing of Session, the UI or Tracktion, so both other targets can link it without depending on each other.

The rule is narrower than "the app never sees a device header". `native/src/model/SessionInternal.h`, private to the `Session*.cpp` files, includes the Utility, Drum Rack and DJ track-end headers and the SDK's. The `DeviceEditorPanel*.cpp` faces include the five devices they draw, and `DeviceRack.cpp` includes the Drum Rack's to find a dropped sound's pad. Editing one of those headers rebuilds those files, not the whole app.

Inside `RhinoCore` the EQ, Tune, Vocoder and FM DSP use the standard library alone, which is what keeps them testable without an engine. `juce_core` is included by `ContentLibrary`, the file formats (`DevicePreset`, `DrumKitFile`, `ExactFloatText.h`), `DeviceDisplay.h` and `DrumSynth.h`, which `DrumRackEngine` includes in turn (checked 2026-10-06).

Each target has exactly one public include root, `src` for the app, `src/core` for `RhinoCore` and `src/devices` for `RhinoDevices`, so an include is spelled from the root of the target that owns the header: `"model/Session.h"`, `"ui/shared/Theme.h"`, `"app/ProjectFiles.h"` in the app; `"dsp/EqFilter.h"`, `"dj/DjEngine.h"`, `"content/ContentLibrary.h"` for anything in core; `"audio/RhinoEqDevice.h"` for a device. A header in the same folder is included bare. Tests spell them the same way, and no include climbs with `../` (since 2026-10-13). A device therefore cannot even spell a model or UI include, since neither root is on its path.

The `te` alias (`namespace te = tracktion::engine;`) is Rhino's, not Tracktion's: each device header declares it at its top (`devices/DeviceCatalog.h`, `devices/sdk/NativeParameter.h`, `devices/audio/AutoTuneDevice.h` and the rest), and every file that writes `te::` gets it transitively through `DeviceCatalog.h` and `model/Session.h`. A unit that includes `<tracktion_engine/tracktion_engine.h>` on its own must declare the alias itself, as `native/src/app/AppPlatform.cpp` does (the first such unit, commit `db88b5c`, 2026-10-13); without it the build fails with `'te' is not a class or namespace name`.

## The app target

`juce_add_gui_app(RhinoNative ...)` produces `RhinoDAW.exe` ([The executable is RhinoDAW, not Rhino](executable-named-rhinodaw.md)). The runners in `native/src/tests/` are compiled into that binary, and each CTest case runs it with a flag such as `--self-test`. Its only JUCE binary data is the two Inter fonts ([Content is files, never compiled in](content-is-files.md)). The app and `RhinoCore` are compiled with `RHINO_SOURCE_DIR`, the absolute path of `native/`, which is how a development build finds `library/` and each external plugin's development VST3 (Forge's, Duck's) without a copy step ([Hosting Forge in Rhino](forge-hosting.md)).

## Pitfalls

- A new `.cpp` needs a line in one of the three lists and then an explicit configure: `CMAKE_SUPPRESS_REGENERATION ON` means `cmake --build` never re-runs CMake, so the file silently never compiles ([A new source file needs an explicit CMake configure](cmake-does-not-reconfigure.md)).
- `RhinoCore` holds, under `dsp/`: `EqEngine`, `EqFilter.h`, `SpectrumAnalyser`, `PitchTracker`, `PsolaShifter`, `ScaleQuantizer.h`, `AutoTuneEngine`, `VocoderEngine`, `FmEngine`, `DrumRackEngine`, `DrumSynth`, `DrumSlicer`; under `dj/`: the fifteen `Dj*` engine files; under `content/`: `ContentLibrary`, `DevicePreset`, `DrumKitFile`, `ExactFloatText.h`; at the root: `DeviceDisplay.h` and `SystemUsage` (checked 2026-10-13).

## Related

- [The source tree mirrors the layers](source-tree-mirrors-the-layers.md)
- [Dependency direction](dependency-direction.md)
- [Device catalog](device-catalog.md)
- [Adding a device to Rhino](adding-a-device.md)
- [Keeping files small](keeping-files-small.md)
- [Build and test Rhino](build-and-test-rhino.md)
