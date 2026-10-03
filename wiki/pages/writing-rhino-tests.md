---
title: Writing Rhino tests
type: guide
summary: Choose between a unit check and a workflow scenario, and follow the rules that keep scenarios sharing one Session honest.
tags: [rhino, testing, conventions]
sources: []
updated: 2026-10-03
---

# Writing Rhino tests

Rhino's tests are compiled into the app and selected by flag ([Build and test Rhino](build-and-test-rhino.md)).
`native/src/tests/README.md` describes two layers; prefer the first wherever it reaches the behaviour.

- **Unit-style checks**, for whatever needs no `Session`, render, desktop peer or pointer sequence. Geometry over pure
  headers such as `ClipGeometry.h` goes in `tests/Arrangement/ClipGeometryTest.cpp` (`--arrangement-geometry-test`, no
  audio device). DSP lives in `src/core` as a plain class driven from `--self-test` (`SelfTest.cpp`, `AutoTuneTest.cpp`,
  `EqTest.cpp`, `VocoderTest.cpp`).
- **Workflow scenarios**, for contracts that cross the engine, undo, rendering, persistence or real pointer events.

## How a scenario works

A scenario is a bare block of statements in `tests/Pattern/scenarios/` or `tests/Arrangement/scenarios/`, included
inside the runner function of the matching `WorkflowTest.cpp` after a `scenario("name")` call that prints
`Arrangement scenario: <name>` (or `Pattern scenario:`) to stderr. All scenarios share one `Session` and one stack
frame and run in include order, so a scenario may rely on what an earlier one left. `require` throws, so the first
failure ends the runner with one message. `.inc` files are deliberately absent from CMake. Split one nearing 200 lines
or mixing unrelated behaviour, and read the runner's comments before moving one: they say why each sits where it does.

## Rules

1. **Render before `GesturesAndPersistence.inc`.** It calls `Session::releaseAudioDevice`, after which a `RenderTask`
   never finishes and the runner times out with no assertion ([Offline renders that never return](renders-that-never-return.md)).
2. **Allocate anything big.** Every scenario shares the runner's 1 MB stack. A `StepGrid` carries about 200 KB of
   caches, and the third one declared by value overflowed it inside an unrelated scenario. Declare a `StepGrid`, an
   `Arrangement` or anything that size with `std::make_unique`
   ([A CTest SegFault may be a stack overflow](stack-overflow-reports-as-segfault.md)).
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

## Access and determinism

Classes under test befriend the runner (`friend int runArrangementTest();` in `Arrangement`, `Session`, `StepGrid`,
`AudioClipPanel`, `SessionView`), one reason big classes are split across translation units rather than into new types
([Keeping files small](keeping-files-small.md)). The four runner flags set `Session::setCommandLineTestMode`: machine
preferences (browser preview, last track kind) are neither read nor written, and `pickTrackColour` takes the first
unused colour, so pixel comparisons repeat.

## Related

- [Build and test Rhino](build-and-test-rhino.md)
- [Offline renders that never return](renders-that-never-return.md)
- [Hold ids, not pointers](ids-not-pointers.md)
- [Reproducing a live timing bug offline](reproducing-live-timing-offline.md)
- [Seeing the UI without taking the screen](headless-ui-snapshots.md)
