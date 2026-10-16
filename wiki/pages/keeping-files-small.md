---
title: Keeping files small
type: convention
summary: Split a .cpp past about 600 lines or a second responsibility by defining one class across several translation units, not by inventing types.
tags: [both, code-structure, cmake]
sources: []
updated: 2026-10-16
---

# Keeping files small

## The rule

Split a `.cpp` when it passes roughly 600 lines or takes on a second responsibility (`AGENTS.md`, *Keeping files small*). Do it with the mechanism both products already use: **one class defined across several translation units**. That needs no header change and no call-site change. It keeps `friend` declarations working, which is how `friend int runArrangementTest();` lets the tests reach private members. The only cost is one line in `CMakeLists.txt`.

## Why

Five files once held 68% of Rhino, and `Session.cpp` alone was 2,661 lines. Each of its responsibilities already had a different caller (`StepGrid` used only the note API, `DeviceRack` only the device API), so the split followed seams the callers had already drawn. Small units also rebuild cheaply. Forge's precompiled header, `src/ForgePch.h`, is why twenty translation units now build faster than three did.

## Where it is applied

As of 2026-10-03:

- **Rhino.** `Session` (`native/src/model/`) spans 24 `Session*.cpp` files: 22 define its members, and `SessionInternal.cpp` and `SessionPatches.cpp` hold its private helpers (29 units by 2026-10-13, [Session, the model](session-model.md)). `Arrangement` spans 11 `Arrangement*.cpp` files (`ui/arrangement/`) and `StepGrid` spans four (`ui/notes/`). `DeviceEditorPanel` (`ui/rack/`) keeps its device faces in `DeviceEditorPanelEq.cpp`, `DeviceEditorPanelAutoTune.cpp` and `DeviceEditorPanelVocoder.cpp`. On 2026-10-07 the Drum Rack's face took six of its units (`DeviceEditorPanelDrums.cpp`, `...DrumGestures.cpp`, `...DrumParts.cpp`, `...DrumMenus.cpp`, `...DrumSample.cpp`, `...DrumSampleControls.cpp`; the pointer handling left `DeviceEditorPanelDrums.cpp` for `...DrumGestures.cpp` when it reached 694 lines), sharing `DeviceEditorPanelDrumsInternal.h`: an `Internal.h` can serve one face's units within a larger class. `DrumRackDevice` (`devices/instruments/`) spans two (`DrumRackDevice.cpp`, `DrumRackDeviceEditing.cpp`) with `DrumRackDeviceInternal.h` ([Drum Rack](drum-rack.md)). Since commit `db88b5c` (2026-10-13) the shell's `MainComponent` (`app/`, the former `ControlWindow` of about 1,600 inline lines) is declared in `MainComponent.h` and defined across six units by responsibility (`MainComponent.cpp`, `...Layout.cpp`, `...ControlBar.cpp`, `...Panes.cpp`, `...Commands.cpp`, `...Timer.cpp`), every body moved verbatim and every unit under 400 lines ([App shell and control bar](app-shell.md)).
- **Forge.** `Editor` spans 16 `src/ForgeEditor*.cpp` files, and `Processor` spans five (`ForgeProcessor.cpp`, `ForgeParameters.cpp`, `ForgeProcessorState.cpp`, `ForgeProcessorLfo.cpp`, `ForgeProcessorMidi.cpp`). The engine is the exception and splits into headers only, never into translation units ([Forge's engine splits into headers only](forge-engine-headers-only.md)).

## How to apply it

- Helpers shared by one class's own units go in a private `*Internal.h` beside it: `SessionInternal.h`, `SessionDjInternal.h` (the DJ booth's state, included only by `SessionDj.cpp` and `SessionDjSources.cpp`), `StepGridInternal.h`, `ArrangementInternal.h` and Forge's `ForgeEditorInternal.h`. Nothing else includes them.
- A class's units live in one folder and keep the class as a prefix (`ui/arrangement/ArrangementGestures.cpp`, never `ui/arrangement/Gestures.cpp`). The move of 2026-10-13 kept the prefixes because stripping them would have produced at least ten colliding basenames (`Internal`, `Painter`, `Gestures`, `Drops`, `Groups`, `Automation`, `Presets`, `Warp`, `Drums`, `Panel`) and invalidated every bare file name in this wiki ([The source tree mirrors the layers](source-tree-mirrors-the-layers.md)).
- Helpers shared by *different* classes get an ordinary header. `BrowserIds.h` (`ui/browser/`) serves both the arrangement and the device rack; before it existed, their two copies of the tables drifted apart. Its `isDroppedSoundFile` (2026-10-07) serves the rack and the Drum Rack window, but two copies of the same extension list remain, in `ArrangementDrops.cpp` and `SessionDrums.cpp`: a possible cleanup. `SessionDrums.cpp` cannot take it from `BrowserIds.h`, which includes `Session.h` and is UI-side ([Dependency direction](dependency-direction.md)), so a shared one would live lower down.
- Extract a new type only when it buys testability. `model/ClipGeometry.h`, `ui/notes/VelocityLaneGeometry.h`, `core/dsp/ScaleQuantizer.h` and `core/dsp/EqFilter.h` are pure headers with no JUCE, so they are tested without a `Session`.
- List every `.cpp` in a `CMakeLists.txt`, because nothing is globbed. Then reconfigure explicitly, since `cmake --build` never does it for you ([A new source file needs an explicit CMake configure](cmake-does-not-reconfigure.md)).
- Test scenarios split earlier, near 200 lines ([Writing Rhino tests](writing-rhino-tests.md)).

## Still to do

The rule is a target. As of 2026-10-03, nine `.cpp` files under `native/src` are past 600 lines. The largest were `app/Main.cpp` at about 2,200 (204 since `db88b5c`, below), `ArrangementGestures.cpp` at about 1,060 and `Arrangement.cpp` at about 890. The DJ booth of 2026-10-09 ([DJ view and the booth](dj-view.md)) added six more: `core/dj/DjMixer.cpp` at about 1,150 holds every unit of the mixer (strip, compressor, isolator, filter, master, mic, pitch shifter, send unit and fourteen beat effects) and is the first to split, the send unit and the beat effect being natural files of their own; then `SessionDj.cpp` at about 1,060, `tests/DjTest.cpp` at about 870, `SessionDjSources.cpp` at about 760, and `core/dj/DjDeck.cpp` and `core/dj/DjAnalysis.cpp` at about 670 each. `DjControls.h` defines its pads, faders, meters and jog wheel inline, about 520 lines of header. Step 2 of the DJ live-chain work (2026-10-11) split the deck the repo's way, one class across `DjDeck.cpp`, `DjDeckTransport.cpp`, `DjDeckLoops.cpp` and `DjDeckRender.cpp`, and the engine across `DjEngine.cpp`, `DjEngineClock.cpp` and `DjEngineMix.cpp`, none past 370 lines ([DJ deck engine](dj-deck-engine.md)). `SessionDj.cpp` and `SessionDjSources.cpp` (about 1,200 and 1,240 after the step's review) and `tests/DjTest.cpp` (about 2,280) waited, since splitting them means new lines in `native/CMakeLists.txt`, which another change held at the time; dropping the bounce and the preview on 2026-10-12 brought the two session files to about 1,110 and 860. Of the two classes that were defined inline in a single file, `ControlWindow` in `Main.cpp` was split on 2026-10-13 (`db88b5c`): declarations in `app/MainComponent.h`, definitions across six `MainComponent*.cpp` units, the nested `Window` as `app/MainWindow.*`, every body moved verbatim ([App shell and control bar](app-shell.md), [The source tree mirrors the layers](source-tree-mirrors-the-layers.md)). The velocity lane (2026-10-16) took `StepGrid` to seven units, but left `StepGrid.cpp` at 679 lines and `StepGridGestures.cpp` at 727, both past the line ([Note editor (StepGrid)](note-editor.md)). The remaining candidate is `FloatingDeviceWindow` in `ui/rack/DeviceRack.cpp`. Splitting it means turning inline bodies into declarations plus definitions. That is real restructuring: do it deliberately, never as a side effect.

## Related

- [Dependency direction](dependency-direction.md)
- [Session, the model](session-model.md)
- [Arrangement view](arrangement-view.md)
- [Forge editor (panel)](forge-editor.md)
- [Rhino's build targets](rhino-build-targets.md)
