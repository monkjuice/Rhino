---
title: Writing Rhino tests
type: guide
summary: Choose between a unit check and a workflow scenario, follow the rules that keep scenarios sharing one Session honest, and time the interface with --profile-ui.
tags: [rhino, testing, conventions]
sources: []
updated: 2026-10-10
---

# Writing Rhino tests

Rhino's tests are compiled into the app and selected by flag ([Build and test Rhino](build-and-test-rhino.md)).
`native/src/tests/README.md` describes two layers; prefer the first wherever it reaches the behaviour.

- **Unit-style checks**, for whatever needs no `Session`, render, desktop peer or pointer sequence. Geometry over pure
  headers such as `ClipGeometry.h` goes in `tests/Arrangement/ClipGeometryTest.cpp` (`--arrangement-geometry-test`, no
  audio device). DSP lives in `src/core` as a plain class driven from `--self-test` (`SelfTest.cpp`, `AutoTuneTest.cpp`,
  `EqTest.cpp`, `VocoderTest.cpp`, `FmTest.cpp`, `DrumRackTest.cpp`). Every device Rhino makes is also rendered by
  `--device-test` (`DeviceConformance.cpp`) against [the native device standard](native-device-standard.md); a new
  device gets those checks without writing any. A device that is silent at its defaults, as a blank
  [Drum Rack](drum-rack.md) is, needs a branch in that file's `prime()`, or it fails every check that listens for a
  note.
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
that session's `releaseAudioDevice()` before the block ends (`ClipEdits.inc`, `GroupUndo.inc`, `PartialRepaint.inc`, and
`DjBooth.inc` for the group it bounces).

## Rules

1. **Render before `GesturesAndPersistence.inc`.** It calls `Session::releaseAudioDevice`, after which a `RenderTask`
   never finishes and the runner times out with no assertion ([Offline renders that never return](renders-that-never-return.md)).
   `DjBooth.inc` renders too, so it sits between `GroupBusReload.inc` and `Recording.inc`; and from that point on a clip
   imported onto a newly added track never renders, so a scenario that imports onto a new track and renders it belongs
   before `GroupBusReload.inc`.
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
   [Seeing the UI without taking the screen](headless-ui-snapshots.md). Such a check reads every child, shown or not,
   so a control the layout hides must take empty bounds (`setBounds({})`), as `DjDeckPanel` does with the jog wheel
   on a short or narrow console and with the clips grid when no cell fits; left at its old bounds, a control nobody sees fails the check. Look at the PNG too: the Drum Rack's envelope
   line drew nothing, and only a snapshot showed it
   ([A juce::Path holding only a start point is empty](juce-path-isempty-ignores-a-lone-point.md)).
7. **Render only what you measure.** A render that measures one clip names that clip's track in `tracksToDo`, as
   `SessionMerge` does. Once the arp's defaults let track 0 play through its window, a whole-edit render failed
   "persistence: trimmed render" on every run, with the product never at fault (commit `89dfd1c`).
8. **Ask a face where its parts are.** `runDrumRackFaceTest` finds pads, map cells, markers and knobs through the
   panel's own `drumPadAt` and `drumTooltip` rather than repeating its layout arithmetic, so a layout change cannot
   leave the test clicking stale coordinates. Live MIDI cannot reach a device in the Pattern runner, so the key-flash
   and Auto Select checks nudge the panel's own counter (`drumNotesSeen`) instead of sending a note.
9. **A direct handler call skips JUCE's dispatch.** The face tests call `panel->mouseDown` and `mouseWheelMove`
   themselves, so they cannot see a bug in how an event reaches the handler: every face heard each of its own clicks
   twice, and every test passed ([A JUCE component listening to itself hears its own clicks twice](juce-self-listener-hears-clicks-twice.md)).
   For a real click, put the component on the desktop off-screen at (-10000, -10000) and send a press and a release
   through `ComponentPeer::handleMouseEvent`, as `runDrumRackFaceTest` does. A desktop peer is needed, so that check
   runs only with `RHINO_NATIVE_INPUT_TEST=1`, as the playhead's Direct2D check in `Rendering.inc` runs only with
   `RHINO_NATIVE_RENDER_TEST=1` ([Playhead rendering](playhead.md)); a plain `ctest` skips both. Set it in the shell
   that runs `ctest --test-dir native/build -C Release -R native_pattern_workflow`.
10. **Leave the clip slots empty.** `SessionView.inc` opens with `Slots start empty` and runs after `DjBooth.inc`, so a
    scenario that fills a slot deletes it before its block ends (`dj booth: clips on the console` clears its cells and
    puts the deck back on SONG), or the session view's scenario fails on a clip it never made.

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
`AudioClipPanel`, `SessionView`, `DjClipGrid` and the DJ view's panels; `friend int runUiProfile();` in `Arrangement`, `StepGrid` and `DeviceRack`), one reason
big classes are split across translation units rather than into new types ([Keeping files small](keeping-files-small.md)).
The four runner flags, `--device-test`, `--arp-snapshot` and `--profile-ui` set `Session::setCommandLineTestMode`: machine
preferences (browser preview, last track kind) are neither read nor written, and `pickTrackColour` takes the first
unused colour, so pixel comparisons repeat.

Nothing in `Main.cpp` is reachable by a test ([App shell and control bar](app-shell.md)), so a shell behaviour is
checked at the boundary the shell wires, and whatever it needs is put in the view or the model: `DjBooth.inc` replaces
`DjView::deckSelected` with a lambda to see which deck the shell would be told of, presses a console through its
`selected` callback and calls `togglePlayFocused` as Space would; what the shell then does with that deck
(`followDeck`) is unchecked.

## Related

- [Build and test Rhino](build-and-test-rhino.md)
- [Offline renders that never return](renders-that-never-return.md)
- [Hold ids, not pointers](ids-not-pointers.md)
- [Reproducing a live timing bug offline](reproducing-live-timing-offline.md)
- [Seeing the UI without taking the screen](headless-ui-snapshots.md)
- [What a change costs the interface](ui-cost-of-a-change.md)
- [JUCE's rasteriser is not clip-invariant](juce-rasteriser-not-clip-invariant.md)
- [A JUCE component listening to itself hears its own clicks twice](juce-self-listener-hears-clicks-twice.md)
