---
title: Writing Rhino tests
type: guide
summary: Choose between a unit check and a workflow scenario, follow the rules that keep scenarios sharing one Session honest, and time the interface with --profile-ui.
tags: [rhino, testing, conventions]
sources: []
updated: 2026-10-13
---

# Writing Rhino tests

Rhino's tests are compiled into the app and selected by flag ([Build and test Rhino](build-and-test-rhino.md)).
`native/src/tests/README.md` describes two layers; prefer the first wherever it reaches the behaviour.

- **Unit-style checks**, for whatever needs no `Session`, render, desktop peer or pointer sequence. Geometry over pure
  headers such as `model/ClipGeometry.h` goes in `tests/Arrangement/ClipGeometryTest.cpp` (`--arrangement-geometry-test`, no
  audio device). DSP lives in `src/core/dsp` as a plain class driven from `--self-test` (`SelfTest.cpp`, `AutoTuneTest.cpp`,
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
every scenario of `DjLiveChain.inc`).

## Rules

1. **Render before `GesturesAndPersistence.inc`.** It calls `Session::releaseAudioDevice`, after which a `RenderTask`
   never finishes and the runner times out with no assertion ([Offline renders that never return](renders-that-never-return.md)).
   The DJ scenarios - `DjBooth.inc`, `ClipCells.inc`, `DjLiveChain.inc`, `DjBoothEdges.inc` - sit between
   `GroupBusReload.inc` and `Recording.inc`, whose arming allocates a playback context. From `GroupBusReload.inc` on, a
   clip imported onto a newly added track never renders, so a scenario that imports onto a new track and renders it
   belongs before it, or works in a document of its own. A document a scenario reopens and then renders is written to disk first: reopened from a path that was never
   written, a file imported after it resolves one folder too high and the render waits for it for good.
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
10. **Leave the shared session's clip slots empty.** `ClipRoundTrip.inc` works in the first track's cells after
    `DjBooth.inc` has filled and cleared its own, and expects some of them empty, so a scenario that fills a slot of the shared session
    deletes it before its block ends (`dj booth: clips on the console` clears every cell past the first and leaves the
    deck on the first). A check that wants cells it can count on builds its own document, as `ClipCells.inc` does.
11. **A track's chain runs in a runner only if the scenario runs it.** The runners open no audio device, so the
    song's graph never processes a track on its own, and `bindDjTrack` allocates no playback context under them, since
    a context left allocated is one a later offline render must not find. A scenario has two ways in
    ([DJ decks play live through the track's chain](dj-decks-play-live-through-the-track.md)):
    - Play the chain by hand, as most of `DjLiveChain.inc` does with its `run`: one block of the booth through
      `djProcessOffline`, then every enabled plugin's `applyToBuffer` in order over one buffer and one
      `te::MidiMessageArray` with `isPlaying` false, after `initialise` on each. Cheap, and right for levels and knobs.
    - Run Tracktion's real graph through its hosted audio device, in a document of its own whose machine device is
      released first, calling `hosted.processBlock` before the booth each block, as "dj live: the song's own graph
      plays a deck's track while the song stands still" does (2026-10-12). This is what proves what only the graph
      decides: play-in-stop, the transport, mute and solo. It stays silent until the engine's posted device preparation
      is dispatched by hand ([Tracktion's hosted device is silent until its start is dispatched](hosted-device-silent-until-dispatched.md)).

    Levels compare only on a sound that holds: `DjLiveChain.inc` plays Rhino FM's Drawbar Organ preset (sustain 1,
    release 60 ms, no velocity) with chords struck on every beat, and checks the input's guard by the chain's own
    output falling silent while the song plays. One track plays one deck, so two decks need two tracks
    (`DjBoothEdges.inc`).
12. **Find a device by `deviceSlots`, not by slot 0.** Every track's plugin 0 is the hidden DJ input since 2026-10-12,
    so its first visible device is plugin 1; four scenarios that said slot 0 moved to 1. `DjLiveChain.inc` looks a
    device up by kind and takes its `pluginIndex`, and a hidden plugin is found by its type, as `AutomationLanes.inc`
    finds a fresh track's Utility: a channel strip's order differs between starter and added tracks
    ([Device chain order](device-chain-order.md)).

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

A timed check inside a CTest case spends that case's limit. `--self-test` has 30 s (`native/CMakeLists.txt`), and the
DJ booth's cost check (`checkBoothCost` in `tests/DjTest.cpp`, which logs its numbers and requires no allocation) renders
10 s of silence rather than 30 for that reason: while the mixer's filters take denormals in silence, 30 s cost the case
eight seconds and left it at 25 s of its 30 (2026-10-11; [DJ deck engine](dj-deck-engine.md)).

## Access and determinism

Classes under test befriend the runner (`friend int runArrangementTest();` in `Arrangement`, `Session`, `StepGrid`,
`AudioClipPanel`, `DjClipGrid` and the DJ view's panels; `friend int runUiProfile();` in `Arrangement`, `StepGrid` and `DeviceRack`), one reason
big classes are split across translation units rather than into new types ([Keeping files small](keeping-files-small.md)).
`DjDeck` keeps its audio-thread methods private to `DjEngine` and to `DjDeckTest`, a class in `tests/DjTest.cpp` whose
static checks drive one deck alone, as the engine does at a block boundary.
The four runner flags, `--device-test`, `--arp-snapshot` and `--profile-ui` set `Session::setCommandLineTestMode`: machine
preferences (browser preview, last track kind) are neither read nor written, `pickTrackColour` takes the first
unused colour, so pixel comparisons repeat, and the DJ booth never goes on the audio device, so the scenarios and the
profile drive it alone through `djProcessOffline`; its five-argument form hands back what the device would play, which
`DjBoothEdges.inc` compares sample for sample between two documents. A file or an audio cell is still read on the
booth's worker, so a scenario polls `djPoll` until `djBusy` is false. What only a test reads stays private behind the
friendship.

No runner builds the shell's `MainComponent` (its units are ordinary translation units under `app/` since `db88b5c`, but
nothing in `tests/` constructs one yet; [App shell and control bar](app-shell.md)), so a shell behaviour is
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
- [Tracktion's hosted device is silent until its start is dispatched](hosted-device-silent-until-dispatched.md)
