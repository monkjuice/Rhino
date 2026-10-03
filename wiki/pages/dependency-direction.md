---
title: Dependency direction
type: convention
summary: Knowledge flows one way in both products, UI to model to devices to core, and every rule lives in the model so each UI path inherits it.
tags: [both, architecture, includes]
sources: []
updated: 2026-10-03
---

# Dependency direction

## The rule

In Rhino, knowledge flows downward only: UI, then model, then devices, then core.

| Layer | May depend on | Must not know |
| --- | --- | --- |
| UI (`Arrangement*`, `StepGrid*`, `DeviceRack`, `Main.cpp`) | `Session` | audio scheduling |
| Model (`Session.h`, `Session*.cpp`) | Tracktion, `DeviceCatalog.h` | any UI header |
| Devices (`native/src/devices/`) | Tracktion, `RhinoCore` | `Session.h`, any UI header |
| `RhinoCore` (`native/src/core/`) | JUCE only | Tracktion, `Session`, UI |

`Session.h` reaches the device library through `DeviceCatalog.h` alone. The private `SessionInternal.h` includes the three device headers the implementation genuinely manipulates (Utility, Drums and Rhino Wave).

## Why

- **Rebuild cost.** `Session.h` used to include all six device headers, so editing any device rebuilt nearly every translation unit. Now a device edit rebuilds only the device library.
- **One answer per rule.** A rule written in the model is inherited by every UI path, so refusals live there: a mismatched drop on a track ([Track kinds: audio and MIDI](track-kinds.md)), `chainRank` refusing a move that would break MIDI FX, then instrument, then audio FX ([Device chain order](device-chain-order.md)), and `makeRoomForClip` ([Clips never overlap](clip-placement.md)). A UI that checked for itself would be a second answer, free to disagree.
- **Two views, one project.** The arrangement and the session view read the same `Session` and never cache a copy of tracks, devices or mixer state. Selection, focus, scroll and zoom are the only per-view state.
- **Reporting, not deciding.** `Arrangement` reports `clipSelected`, `clipOpened`, `trackSelected` and `trackFocused`; the shell in `Main.cpp` decides which panel a click opens.

## Forge

- Forge includes no Tracktion or Rhino header. Rhino finds Forge by name (`SessionExternalPlugins.cpp`) and never names one of its parameters, so Forge's parameters can change without breaking the hosting ([Forge is an independent VST3](forge-is-an-independent-vst3.md)).
- `Core` (`core/ForgeCore.h`) owns no `AudioProcessor`, UI, state tree or filesystem. It never calls the arpeggiator: the `Processor` owns the `Arp` and hands `Core` the notes it produces (`ForgeCore.h` includes `ForgeArp.h` only as the umbrella header). Its panel readings are plain members, which the `Processor` publishes through atomics.
- Pure DSP headers depend on nothing else in their product, so the display and the audio can read one file: `core/ForgeFilter.h`, `core/ForgeNoise.h` and `core/ForgeMidiMap.h` in Forge, `core/EqFilter.h` and `core/ScaleQuantizer.h` in Rhino ([Displays draw from the DSP](displays-draw-from-the-dsp.md)).

## How to apply

Before adding an include, ask which layer the file belongs to. If a device wants something from `Session`, or a model function wants an answer from the UI, the dependency points the wrong way. Move the rule down a layer, or pass the value in.

## Related

- [Rhino's build targets](rhino-build-targets.md)
- [Session, the model](session-model.md)
- [Forge engine (Core)](forge-engine.md)
- [Hosting Forge in Rhino](forge-hosting.md)
