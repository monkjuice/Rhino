---
title: The source tree mirrors the layers
type: decision
summary: Since commit b68fa0e (2026-10-13) native/src is laid out as app, model, ui/<view>, core/{dsp,dj,content}, devices and tests; an include is spelled from its target's root, files keep their class prefix, and a JUCE module for RhinoCore was rejected.
tags: [rhino, code-structure, cmake, includes, architecture]
sources: []
updated: 2026-10-13
---

# The source tree mirrors the layers

## Context

Until 2026-10-13 `native/src` was about 150 files in one flat folder beside `src/core` and `src/devices`. The user asked for a files-and-folders revamp "following JUCE/Tracktion practice" with more elegant naming; the flat `src` beside `src/core` made no sense to them.

What that practice turns out to be: JUCE and Tracktion prescribe only the format of a reusable *module* (umbrella header, unity-build root `.cpp` files, `juce_`/`tracktion_` file prefixes, lowercase subsystem folders such as `model/`, `playback/`, `plugins/`) and say nothing about an application's tree; the Projucer template is one flat `Source/`, and pluginval is flat. Large JUCE and DAW applications (Helio `Source/Core` and `Source/UI`, Surge `src/common` and `src/surge-xt/gui`, Element `engine/gui/session`, Ardour `libs/ardour` and `gtk2_ardour`, LMMS `core/gui`, Vital `synthesis/interface`) split by layer and then by feature, name files after the class, keep the class prefix on a class spread over files (Surge's `SurgeGUIEditorMenuStructures.cpp`, Ardour's `editor_drag.cc`), and keep `Main.cpp` to the application object.

## The choice

Commit `b68fa0e` on `chore/reorganise-native-src` (407 paths: 187 files moved, 2 renamed, 33 edited) lays the tree out by layer, then by view. The layers may only look down: `app` and `ui` see `model`; `model` sees `devices` and `core`; `devices` sees `core`; `core` sees JUCE alone ([Dependency direction](dependency-direction.md)).

| Folder | Holds |
| --- | --- |
| `app/` | The shell: `Main.cpp` (`Application` alone, 204 lines since `db88b5c`), `MainWindow.*`, `MainComponent.h` with its six `MainComponent*.cpp` units, `ControlBarButtons.h`, `AppPlatform.*` (the one unit including `windows.h`), `ControlBarFields.*`, `ControlBarIcons.*`, `TransportDisplay.*`, `ProjectFiles.*`, `ComputerKeyboard.*`, `StartupScreen.h`, `InfoHints.h` |
| `model/` | `Session.h`, every `Session*.cpp`, `SessionInternal.h/.cpp`, `SessionDjInternal.h`, `DeviceIds.h`, `ClipGeometry.h`, `CountInClick.*` |
| `ui/shared/` | `Theme.h`, `ShapedText.*`, `SelectionInput.h`, `UiVisibility.h`, `WallClock.h`, `WaveformLanes.h`, `DevicePixels.h`, `ClipDrag.h`, `Playhead.*`, `ComponentSnapshot.h` (since `db88b5c`) |
| `ui/arrangement/`, `ui/notes/`, `ui/dj/`, `ui/rack/`, `ui/browser/`, `ui/audioclip/` | One folder per view: `Arrangement*` with `ArrangementGrid.h` and `MonitorSelector.*`; `StepGrid*`; every `Dj*` UI file; `DeviceRack.*`, `DeviceEditorPanel.h` with every `DeviceEditorPanel*.cpp`, and `DrumRackWindow.*`; `BrowserPanel.*`, `BrowserDrops.*`, `BrowserIds.h`; `AudioClipPanel.*`, `AudioClipWarp.cpp` |
| `core/dsp/`, `core/dj/`, `core/content/` | `RhinoCore` by responsibility: the engines behind Rhino's devices (`EqEngine`, `EqFilter.h`, `SpectrumAnalyser`, `PitchTracker`, `PsolaShifter`, `ScaleQuantizer.h`, `AutoTuneEngine`, `VocoderEngine`, `FmEngine`, `DrumRackEngine`, `DrumSynth`, `DrumSlicer`); the booth's fifteen `Dj*` engine files; `ContentLibrary`, `DevicePreset`, `DrumKitFile`, `ExactFloatText.h`. `DeviceDisplay.h` and `SystemUsage.*` stay at the `core/` root |
| `devices/`, `tests/` | Unchanged by the move; `tests/TestRunners.h/.cpp` (the test-flag dispatch) arrived with `db88b5c`. `tests/Arrangement` and `tests/Pattern` keep their capitals on purpose: a case-only folder rename is unsafe with git on Windows |

Two files were renamed because their names misled: `SessionPresets.cpp` is `model/SessionPatternPresets.cpp` (it is the *pattern* presets, and the old name once had it overwritten by a write meant for the device presets, [Session, the model](session-model.md)); `DeviceMacros.cpp` is `model/SessionDeviceMacros.cpp` (it includes `SessionInternal.h`, so it is a `Session` unit).

**Includes.** Each target keeps its one public include root, `src` for the app, `src/core` for `RhinoCore`, `src/devices` for `RhinoDevices`, so an include is spelled from the root of the target that owns the header: `"model/Session.h"`, `"ui/shared/Theme.h"`, `"app/ProjectFiles.h"`; `"dsp/EqFilter.h"`, `"dj/DjEngine.h"`, `"content/ContentLibrary.h"`; `"audio/RhinoEqDevice.h"` as before. A header in the same folder is included bare, as `devices/` already did; tests spell them the same way, and the 44 `"../"` includes are gone. A device still cannot spell an include of the model or the UI, and a wrong-way include in the app now reads wrong in its path. 193 include lines in 90 files changed and nothing else in any file, verified blob by blob against the old tree ([Rhino's build targets](rhino-build-targets.md)).

**Names.** Files keep the class as a prefix (`ArrangementGestures.cpp`, never `Gestures.cpp`). Stripping prefixes would have produced at least ten colliding basenames (`Internal`, `Painter`, `Gestures`, `Drops`, `Groups`, `Automation`, `Presets`, `Warp`, `Drums`, `Panel`), broken the `ClassPart.cpp` convention of [Keeping files small](keeping-files-small.md), and invalidated most of this wiki's bare file-name mentions.

## Rejected

- **`RhinoCore` as a JUCE module.** `juce_add_module` makes an INTERFACE library compiled into every consumer; JUCE and Tracktion already compile twice in this tree and `juce_core` three times (seen in the generated `.vcxproj` files), and a unity build defeats the small-translation-unit rebuild rule.
- **Stripping class prefixes**, for the collisions above.
- **Renaming the `native/` folder or the `RhinoNative` target.** It touches the build path `RhinoNative_artefacts`, `.gitignore`, Forge's `RHINO_JUCE_DIR` and every document; deferred.

## The follow-up, done the same day

Commit `db88b5c` split `app/Main.cpp`, which the move had left at 2,353 lines with `ControlWindow` defined inline: `ControlWindow` became `MainComponent` (JUCE's name for a window's content), declared in `app/MainComponent.h` and defined across six `MainComponent*.cpp` units by responsibility; the nested `Window` became `app/MainWindow.*`; the platform calls went to `app/AppPlatform.*`, the buttons to `app/ControlBarButtons.h`, `writeSnapshot` to `ui/shared/ComponentSnapshot.h` and the test-flag dispatch to `tests/TestRunners.*`. `Main.cpp` is 204 lines of `Application`. Every body moved verbatim and nothing behaves differently ([App shell and control bar](app-shell.md)). A chore commit then removed the shell's dead code: an unshown hint label, four fallback menu `TextButton`s, two stale comments.

## What it cost to prove

- The preceding commit `0bff66b` fixed a `devices/CMakeLists.txt` that a merge had left unparseable ([Build and test Rhino](build-and-test-rhino.md)).
- Committing through the HARBIZ tool meant listing all 407 paths ([Git workflow](git-workflow.md)).
- Snapshot hashes could not show the move changed nothing; a per-pixel comparison against a same-binary rerun did ([Seeing the UI without taking the screen](headless-ui-snapshots.md)).
- Pages of this wiki written before 2026-10-13 and `log.md`'s older entries name the flat paths; the log is history and keeps them.

## Related

- [Rhino's build targets](rhino-build-targets.md)
- [Dependency direction](dependency-direction.md)
- [Keeping files small](keeping-files-small.md)
- [Session, the model](session-model.md)
- [App shell and control bar](app-shell.md)
- [A new source file needs an explicit CMake configure](cmake-does-not-reconfigure.md)
