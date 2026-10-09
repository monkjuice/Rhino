---
title: Keeping files small
type: convention
summary: Split a .cpp past about 600 lines or a second responsibility by defining one class across several translation units, not by inventing types.
tags: [both, code-structure, cmake]
sources: []
updated: 2026-10-09
---

# Keeping files small

## The rule

Split a `.cpp` when it passes roughly 600 lines or takes on a second responsibility (`AGENTS.md`, *Keeping files small*). Do it with the mechanism both products already use: **one class defined across several translation units**. That needs no header change and no call-site change. It keeps `friend` declarations working, which is how `friend int runArrangementTest();` lets the tests reach private members. The only cost is one line in `CMakeLists.txt`.

## Why

Five files once held 68% of Rhino, and `Session.cpp` alone was 2,661 lines. Each of its responsibilities already had a different caller (`StepGrid` used only the note API, `DeviceRack` only the device API), so the split followed seams the callers had already drawn. Small units also rebuild cheaply. Forge's precompiled header, `src/ForgePch.h`, is why twenty translation units now build faster than three did.

## Where it is applied

As of 2026-10-03:

- **Rhino.** `Session` spans 24 `Session*.cpp` files: 22 define its members, and `SessionInternal.cpp` and `SessionPatches.cpp` hold its private helpers. `Arrangement` spans 11 `Arrangement*.cpp` files and `StepGrid` spans four. `DeviceEditorPanel` keeps its device faces in `DeviceEditorPanelEq.cpp`, `DeviceEditorPanelAutoTune.cpp` and `DeviceEditorPanelVocoder.cpp`. On 2026-10-07 the Drum Rack's face took six of its units (`DeviceEditorPanelDrums.cpp`, `...DrumGestures.cpp`, `...DrumParts.cpp`, `...DrumMenus.cpp`, `...DrumSample.cpp`, `...DrumSampleControls.cpp`; the pointer handling left `DeviceEditorPanelDrums.cpp` for `...DrumGestures.cpp` when it reached 694 lines), sharing `DeviceEditorPanelDrumsInternal.h`: an `Internal.h` can serve one face's units within a larger class. `DrumRackDevice` spans two (`DrumRackDevice.cpp`, `DrumRackDeviceEditing.cpp`) with `DrumRackDeviceInternal.h` ([Drum Rack](drum-rack.md)).
- **Forge.** `Editor` spans 16 `src/ForgeEditor*.cpp` files, and `Processor` spans five (`ForgeProcessor.cpp`, `ForgeParameters.cpp`, `ForgeProcessorState.cpp`, `ForgeProcessorLfo.cpp`, `ForgeProcessorMidi.cpp`). The engine is the exception and splits into headers only, never into translation units ([Forge's engine splits into headers only](forge-engine-headers-only.md)).

## How to apply it

- Helpers shared by one class's own units go in a private `*Internal.h` beside it: `SessionInternal.h`, `SessionDjInternal.h` (the DJ booth's state, included only by `SessionDj.cpp` and `SessionDjSources.cpp`), `StepGridInternal.h`, `ArrangementInternal.h` and Forge's `ForgeEditorInternal.h`. Nothing else includes them.
- Helpers shared by *different* classes get an ordinary header. `BrowserIds.h` serves both the arrangement and the device rack; before it existed, their two copies of the tables drifted apart. Its `isDroppedSoundFile` (2026-10-07) serves the rack and the Drum Rack window, but three copies of the same extension list remain, in `ArrangementDrops.cpp`, `SessionViewGestures.cpp` and `SessionDrums.cpp`: a possible cleanup. `SessionDrums.cpp` cannot take it from `BrowserIds.h`, which includes `Session.h` and is UI-side ([Dependency direction](dependency-direction.md)), so a shared one would live lower down.
- Extract a new type only when it buys testability. `ClipGeometry.h`, `core/ScaleQuantizer.h` and `core/EqFilter.h` are pure headers with no JUCE, so they are tested without a `Session`.
- List every `.cpp` in a `CMakeLists.txt`, because nothing is globbed. Then reconfigure explicitly, since `cmake --build` never does it for you ([A new source file needs an explicit CMake configure](cmake-does-not-reconfigure.md)).
- Test scenarios split earlier, near 200 lines ([Writing Rhino tests](writing-rhino-tests.md)).

## Still to do

The rule is a target. As of 2026-10-03, nine `.cpp` files under `native/src` are past 600 lines. The largest are `Main.cpp` at about 2,200, `ArrangementGestures.cpp` at about 1,060 and `Arrangement.cpp` at about 890. The DJ booth of 2026-10-09 ([DJ view and the booth](dj-view.md)) added six more: `core/DjMixer.cpp` at about 1,150 holds every unit of the mixer (strip, compressor, isolator, filter, master, mic, pitch shifter, send unit and fourteen beat effects) and is the first to split, the send unit and the beat effect being natural files of their own; then `SessionDj.cpp` at about 1,060, `tests/DjTest.cpp` at about 870, `SessionDjSources.cpp` at about 760, and `core/DjDeck.cpp` and `core/DjAnalysis.cpp` at about 670 each. `DjControls.h` defines its pads, faders, meters and jog wheel inline, about 520 lines of header. The two named next candidates, `ControlWindow` in `Main.cpp` and `FloatingDeviceWindow` in `DeviceRack.cpp`, are classes defined inline in a single file. Splitting either means turning inline bodies into declarations plus definitions. That is real restructuring: do it deliberately, never as a side effect.

## Related

- [Dependency direction](dependency-direction.md)
- [Session, the model](session-model.md)
- [Arrangement view](arrangement-view.md)
- [Forge editor (panel)](forge-editor.md)
- [Rhino's build targets](rhino-build-targets.md)
