---
title: Writing Rhino tests
type: guide
summary: Choose between a unit check and a workflow scenario, follow the rules that keep scenarios sharing one Session honest, and time the interface with --profile-ui.
tags: [rhino, testing, conventions]
sources: []
updated: 2026-10-05
---

# Writing Rhino tests

Rhino's tests are compiled into the app and selected by flag ([Build and test Rhino](build-and-test-rhino.md)).
`native/src/tests/README.md` describes two layers; prefer the first wherever it reaches the behaviour.

- **Unit-style checks**, for whatever needs no `Session`, render, desktop peer or pointer sequence. Geometry over pure
  headers such as `ClipGeometry.h` goes in `tests/Arrangement/ClipGeometryTest.cpp` (`--arrangement-geometry-test`, no
  audio device). DSP lives in `src/core` as a plain class driven from `--self-test` (`SelfTest.cpp`, `AutoTuneTest.cpp`,
  `EqTest.cpp`, `VocoderTest.cpp`). Every device Rhino makes is also rendered by `--device-test`
  (`DeviceConformance.cpp`) against [the native device standard](native-device-standard.md); a new device gets those
  checks without writing any.
- **Workflow scenarios**, for contracts that cross the engine, undo, rendering, persistence or real pointer events.

## How a scenario works

A scenario is a bare block of statements in `tests/Pattern/scenarios/` or `tests/Arrangement/scenarios/`, included
inside the runner function of the matching `WorkflowTest.cpp` after a `scenario("name")` call that prints
`Arrangement scenario: <name>` (or `Pattern scenario:`) to stderr. All scenarios share one `Session` and one stack
frame and run in include order, so a scenario may rely on what an earlier one left. `require` throws, so the first
failure ends the runner with one message. `.inc` files are deliberately absent from CMake. Split one nearing 200 lines
or mixing unrelated behaviour, and read the runner's comments before moving one: they say why each sits where it does.

A scenario closes every brace it opens. `DeviceParameters.inc` once opened a block that `EditingAndAutomation.inc`
closed, so neither could be moved or skipped without the other (fixed in commit `3f6bb4e`). A scenario that needs a
clean slate builds a document of its own inside its block, `auto document = std::make_unique<Session>();`, and calls
that session's `releaseAudioDevice()` before the block ends (`ClipEdits.inc`, `GroupUndo.inc`, `PartialRepaint.inc`).

## Rules

1. **Render before `GesturesAndPersistence.inc`.** It calls `Session::releaseAudioDevice`, after which a `RenderTask`
   never finishes and the runner times out with no assertion ([Offline renders that never return](renders-that-never-return.md)).
2. **Allocate anything big.** Every scenario shares the runner's one stack frame. A `StepGrid` carries about 200 KB of
   caches, and the third one declared by value overflowed the old 1 MB stack inside an unrelated scenario; twice more in
   the week of 2026-10-05 a scenario that added two panels did the same. `RhinoDAW.exe` now reserves 8 MB on Windows
   (commit `3f6bb4e`), but declare a `StepGrid`, an `Arrangement`, a `Session` or anything that size with
   `std::make_unique` all the same ([A CTest SegFault may be a stack overflow](stack-overflow-reports-as-segfault.md)).
3. **Re-fetch clips by id.** A raw `te::Clip*` does not survive an undo, a cross-track move or a project restore; one
   cached across scenarios was the "intermittent" arrangement segfault. Use `session.findClip` or `findAudioClip`
   ([Hold ids, not pointers](ids-not-pointers.md)).
4. **Touch the runner after editing an `.inc`**, or nothing recompiles and the old binary passes; then check the
   scenario's name is in the output ([Editing only a scenario .inc does not rebuild the tests](inc-edits-do-not-rebuild.md)).
5. **Report what was measured and what was wanted** (`requireCents` in `AutoTuneTest.cpp`, `requireDb` in `EqTest.cpp`),
   and settle sound by measuring it ([Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)).
6. **Snapshot a panel that paints.** Render it with `createComponentSnapshot` on every run as a paint smoke test, assert
   its child controls land inside it and off each other (`checkEditorFace` in `EqTest.cpp`), and write a PNG only when
   an env var names a path (`RHINO_CLIP_PANEL_SNAPSHOT`, `RHINO_AUTOMATION_SNAPSHOT`) — see
   [Seeing the UI without taking the screen](headless-ui-snapshots.md).
7. **Render only what you measure.** A render that measures one clip names that clip's track in `tracksToDo`, as
   `SessionMerge` does. Once the arp's defaults let track 0 play through its window, a whole-edit render failed
   "persistence: trimmed render" on every run, with the product never at fault (commit `89dfd1c`).

## Timing, not checking

`RhinoDAW.exe --profile-ui` (`native/src/tests/UiProfile.cpp`, commit `2a4830f`) builds a large document (16 MIDI
tracks × 20 clips × 64 notes, a few automated synths) and every panel the shell keeps listening, laid out in a parent as
the shell does, with the hidden ones switched off. It prints median message-thread costs: one step of a knob, fader and
tempo drag, an arrangement sync and layout, the automation mirror and the rack's automation follow, full and
playhead-strip paints of the arrangement and the note editor, and a full paint of the rack. It checks nothing, opens no
audio device and is not a CTest case. Build the old revision aside and run the two binaries alternately for a few rounds,
as with Forge's `--profile`, because the machine drifts by more than some differences. Paints go to a software image
([Profile paint on a software image](profile-paint-on-a-software-image.md)); what the numbers mean is in
[What a change costs the interface](ui-cost-of-a-change.md).

## Access and determinism

Classes under test befriend the runner (`friend int runArrangementTest();` in `Arrangement`, `Session`, `StepGrid`,
`AudioClipPanel`, `SessionView`; `friend int runUiProfile();` in `Arrangement`, `StepGrid` and `DeviceRack`), one reason
big classes are split across translation units rather than into new types ([Keeping files small](keeping-files-small.md)).
The four runner flags, `--device-test`, `--arp-snapshot` and `--profile-ui` set `Session::setCommandLineTestMode`: machine
preferences (browser preview, last track kind) are neither read nor written, and `pickTrackColour` takes the first
unused colour, so pixel comparisons repeat.

## Related

- [Build and test Rhino](build-and-test-rhino.md)
- [Offline renders that never return](renders-that-never-return.md)
- [Hold ids, not pointers](ids-not-pointers.md)
- [Reproducing a live timing bug offline](reproducing-live-timing-offline.md)
- [Seeing the UI without taking the screen](headless-ui-snapshots.md)
- [What a change costs the interface](ui-cost-of-a-change.md)
- [JUCE's rasteriser is not clip-invariant](juce-rasteriser-not-clip-invariant.md)
