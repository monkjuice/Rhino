---
title: Dependency direction
type: convention
summary: Knowledge flows one way in both products, UI to model to devices to core, every rule lives in the model so each UI path inherits it, and a lint in the geometry test keeps the views from reaching the engine behind Session.
tags: [both, architecture, includes]
sources: []
updated: 2026-10-23
---

# Dependency direction

## The rule

In Rhino, knowledge flows downward only: UI, then model, then devices, then core.

| Layer | May depend on | Must not know |
| --- | --- | --- |
| UI (`native/src/ui/<view>/`, `native/src/ui/shared/`) and the shell (`native/src/app/`) | `Session` | audio scheduling, the engine behind `Session` (its edit, transport, tempo map and undo manager) |
| Model (`native/src/model/`: `Session.h`, `Session*.cpp`) | Tracktion, `DeviceCatalog.h` | any UI header |
| Devices (`native/src/devices/`) | Tracktion, `RhinoCore` | `Session.h`, any UI header |
| `RhinoCore` (`native/src/core/`) | JUCE only | Tracktion, `Session`, UI |

`Session.h` reaches the device library through `DeviceCatalog.h` alone. The private `SessionInternal.h` includes the two device headers the implementation genuinely manipulates (Utility and Drums).

Since 2026-10-13 the folders are the layers and an include is spelled from its target's root, so the direction of a dependency reads in the path: `#include "model/Session.h"` in a `ui/` file is right, and `#include "ui/..."` in a `model/` file is wrong at a glance. A device cannot spell either, because only `src/core` and `src/devices` are on its include path ([The source tree mirrors the layers](source-tree-mirrors-the-layers.md)).

## Why

- **Rebuild cost.** `Session.h` used to include all six device headers, so editing any device rebuilt nearly every translation unit. Now a device edit rebuilds only the device library.
- **One answer per rule.** A rule written in the model is inherited by every UI path, so refusals live there: a mismatched drop on a track ([Track kinds: audio and MIDI](track-kinds.md)), `chainRank` refusing a move that would break MIDI FX, then instrument, then audio FX ([Device chain order](device-chain-order.md)), and `makeRoomForClip` ([Clips never overlap](clip-placement.md)). A UI that checked for itself would be a second answer, free to disagree.
- **Two views, one project.** The arrangement and the DJ view read the same `Session` and never cache a copy of tracks, devices or mixer state. Selection, focus, scroll and zoom are the only per-view state.
- **Reporting, not deciding.** `Arrangement` reports `clipSelected`, `clipOpened`, `trackSelected` and `trackFocused`; the shell's `MainComponent` (`app/MainComponentPanes.cpp`) decides which panel a click opens.

## The views ask `Session`, not the engine

Until 2026-10-23 the direction held for devices and the model but not for the views: 65 lines in 18 files of `ui/` and `app/` opened `session.edit` or `session.engine` for the transport, the tempo map, the undo manager, the playback context and the audio device, and the shell subscribed to the engine's transport and undo manager again on every new document. They now ask `Session` (`isPlaying`, `playheadSeconds`, `locate`, `songLengthSeconds`, `loopRange`, `secondsToBeats`, `beatsToSeconds`, `canUndo`, `canRedo`, `restartPlaybackIfRolling`, `audioDeviceManager`, `audioDeviceSummary`, `ownsDevice`) and listen to the relayed `transportState` and `undoHistory` ([Session, the model](session-model.md#reading-the-document)). A new need is met the same way: add the question to `Session`.

- **`Session::edit` and `engine` stay public**, because about 50 test files use them. A lint holds the line instead: `tests/LayerLintTest.h` and `.cpp`, the last check of `--arrangement-geometry-test` (`native_arrangement_geometry`), reads every `.cpp` and `.h` under `src/ui` and `src/app` and fails, naming file and line, on `session.edit`, `session->edit`, `session.engine`, `session->engine`, `->getTransport()`, `.getUndoManager()`, `->restartPlayback()`, `tempoSequence.` or a Tracktion include (`#include <tracktion_engine`). Tests under `src/tests` are not scanned.
- **The allowances** are `app/Main.cpp`, three lines of `session->engine` (it chooses the machine's audio device before there is a document), and `app/AppPlatform.cpp`, one Tracktion include. They are counted per file and spelling, so a fourth line in `Main.cpp` fails.
- **Only a line that starts with `//` is skipped**, so a trailing comment or a block comment that spells a forbidden token fails it too. The lint throws if it reads fewer than 20 files, so a wrong source path cannot make it pass.
- **Still open** (documented, not changed, as of 2026-10-23): views that write a device's view state straight to the device (Rhino EQ's selected band, the Drum Rack's selected pad and auto-select, the pad-playback drag bracket); file choosers and name or overwrite dialogs inside device faces and the DJ deck menu; device ids spelled in a few views; and `findAudioClip != nullptr` used as a clip-kind test in the shell.
- **The model asks nothing of the pointer.** `Session::gestureHeld()` used to call `juce::Component::isMouseButtonDownAnywhere()`; the shell's timer now tells it, with `setPointerHeld()`.

## Forge

- Forge includes no Tracktion or Rhino header. Rhino finds Forge by name (`SessionExternalPlugins.cpp`) and never names one of its parameters, so Forge's parameters can change without breaking the hosting ([Forge is an independent VST3](forge-is-an-independent-vst3.md)).
- `Core` (`core/ForgeCore.h`) owns no `AudioProcessor`, UI, state tree or filesystem. It never calls the arpeggiator: the `Processor` owns the `Arp` and hands `Core` the notes it produces (`ForgeCore.h` includes `ForgeArp.h` only as the umbrella header). Its panel readings are plain members, which the `Processor` publishes through atomics.
- Pure DSP headers depend on nothing else in their product, so the display and the audio can read one file: `core/ForgeFilter.h`, `core/ForgeNoise.h` and `core/ForgeMidiMap.h` in Forge, `core/dsp/EqFilter.h` and `core/dsp/ScaleQuantizer.h` in Rhino ([Displays draw from the DSP](displays-draw-from-the-dsp.md)).

## How to apply

Before adding an include, ask which layer the file belongs to. If a device wants something from `Session`, or a model function wants an answer from the UI, the dependency points the wrong way. Move the rule down a layer, or pass the value in. If a view wants something the engine knows, add the question to `Session` rather than opening the edit: the lint will say so.

## Related

- [Rhino's build targets](rhino-build-targets.md)
- [The source tree mirrors the layers](source-tree-mirrors-the-layers.md)
- [Session, the model](session-model.md)
- [The 120-track stress audit](stress-audit-2026-10.md)
- [Forge engine (Core)](forge-engine.md)
- [Hosting Forge in Rhino](forge-hosting.md)
