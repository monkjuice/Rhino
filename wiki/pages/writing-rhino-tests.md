---
title: Writing Rhino tests
type: guide
summary: Choose between a unit check and a workflow scenario, follow the rules that keep scenarios sharing one Session honest, and time the interface with --profile-ui and --profile-stress.
tags: [rhino, testing, conventions]
sources: []
updated: 2026-10-28
---

# Writing Rhino tests

Rhino's tests are compiled into the app and selected by flag ([Build and test Rhino](build-and-test-rhino.md)).
`native/src/tests/README.md` describes two layers; prefer the first wherever it reaches the behaviour.

- **Unit-style checks**, for whatever needs no `Session`, render, desktop peer or pointer sequence. Geometry over pure
  headers such as `model/ClipGeometry.h` goes in `tests/Arrangement/ClipGeometryTest.cpp` (`--arrangement-geometry-test`, no
  audio device). That runner also calls the checks other headers keep in files of their own, such as
  `tests/VelocityLaneTest.*` for `ui/notes/VelocityLaneGeometry.h` (2026-10-16; [Note editor velocity lane](note-velocity-lane.md)). It ends with the layer lint, `tests/LayerLintTest.*` (2026-10-23), which reads every source under `src/ui` and `src/app` and fails on a view that reaches past `Session` into the engine ([Dependency direction](dependency-direction.md#the-views-ask-session-not-the-engine)); tests under `src/tests` are not scanned and may still spell `session.edit`. DSP lives in `src/core/dsp` as a plain class driven from `--self-test` (`SelfTest.cpp`, `AutoTuneTest.cpp`,
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
   written, a file imported after it resolves one folder too high and the render waits for it for good. The engine
   names a source relative to a document that does not exist as though the document were a folder (`..\Loop.wav`; the
   quirk `sourceNameFor` in `Session.cpp` notes for Save As), so any check of where a source resolves writes the
   document before `restoreProject(state, file)`, as `ProjectFileMedia.inc` and `TrackClipboard.inc` do.
2. **Allocate anything big.** Every scenario shares the runner's one stack frame. A `StepGrid` carried about 200 KB of
   caches inline (its float caches have been on the heap since commit `d872f0b`, 2026-10-25, when 128 rows made them
   256 KB each), and the third one declared by value overflowed the old 1 MB stack inside an unrelated scenario; twice more in
   the week of 2026-10-05 a scenario that added two panels did the same. `RhinoDAW.exe` now reserves 8 MB on Windows
   (commit `3f6bb4e`), but declare a `StepGrid`, an `Arrangement`, a `Session` or anything that size with
   `std::make_unique` all the same ([A CTest SegFault may be a stack overflow](stack-overflow-reports-as-segfault.md)).
   Declare a listener that a panel's teardown can reach, such as a change counter on the session, before the panel.
   Locals are destroyed in reverse order, and when a `require` fails mid-drag, the `StepGrid` destructor ends the
   drag's gesture, which announces the change. A counter declared after the grid is already gone by then. The
   velocity-lane scenarios were fixed for this on 2026-10-16.
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
   leave the test clicking stale coordinates. The arrangement is the same: never spell a lane's x in pixels, ask
   `xFor`. `GesturesAndPersistence.inc` pressed and dragged a clip at fixed x positions (240 to 346.5, 360 to 500)
   that assumed a 228 px header and its pixels per second, and widening the header to 264 (commit `14c9c77`) broke
   "Pointer drag moves audio" until it read them off `view.xFor`. Live MIDI cannot reach a device in the Pattern runner, so the key-flash
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
13. **Compare a picture with itself under one changed state**, not one row's pixel with another row's. The
    selected-row check in `TrackManagement.inc` first read one pixel low in rows 0 and 1, assuming nothing was drawn
    there: row 0 read the lane plus the wash as intended, but row 1 carried a mark of its own, so the check failed on
    a correct paint. It now paints twice, row 0 selected and then row 1, and requires that over half of the selected
    row's lane pixels, and over 90% of a bare strip of its card, are lighter than in the other picture: translucent
    clip fills let the lit ground through, and opaque marks are equal in both (commit `83135f7`). The same idea as
    comparing a culled repaint under the same clip ([JUCE's rasteriser is not clip-invariant](juce-rasteriser-not-clip-invariant.md)).
14. **A move preview posed by hand needs a selection covering the clip, not just `selected`.** Since 2026-10-23 a
    move preview follows the time selection, which a header press always leaves, so a check that sets `original`,
    `preview`, `gesture = ClipGesture::move` and `dragging` itself must first call `view.setSelection({id}, id)`;
    naming the clip in `selected` alone previews nothing. The selection's edges and insert line are drawn in the lime
    the MIDI-pixel checks count and travel with the preview, so "Dragged MIDI clip contents must follow the live
    preview before mouse-up" in `Rendering.inc` skips the columns under the previewed selection's two edges
    (`xFor(1.0) + 4` to `xFor(1.0 + length) - 3`) and clears the selection when it is done, because the left-trim
    check after it counts lime pixels across the whole lane and still names the clip rather than selecting it
    ([Region editing](region-editing.md#one-selection)).
15. **Restore `trackScroll` as you restore the zoom, and set the view after the fixtures exist.** The shared `view`
    of the arrangement workflow is 1000 x 354 px by the time `RegionMove.inc` runs (246 px before the returns were
    pinned; rule 23), so its lanes are 62 px, two rows fit and the row stack scrolls. `RegionClipboard.inc`'s autoscroll block holds a drag at `lane(1).getBottom() - 8`,
    inside the 30 px band above the main row, so every `autoScrollDrag()` frame also pushed `trackScroll`; it restored
    `viewStart` and `viewSpan` but not `trackScroll`, leaving the stack about 54 px up. The next scenario's press at
    `lane(0).getBottom() - 8` then landed at y 59, one pixel above `lanesTop` (60), where `beginRegionGesture` refuses
    it as not a lane press, and the sweep selected nothing. `RegionMove.inc` now saves `trackScroll`, sets it to 0
    with `updateScroll()` and `resized()` at its start and restores it at its end (2026-10-23). It also sets
    `viewStart` only after its fixtures at 32-36 s exist, because `updateScroll` clamps the view to the song's end, and
    a view set before them was clamped back to 0. When a lane press selects nothing, print the selection, `lane(track)`
    and the press y against `lanesTop` in the require message before suspecting the gesture code; that diagnostic is
    what found this.
16. **Copy before moving the insert point.** Copy, cut, duplicate and delete read the selection rectangle only, and
    `setInsertPoint` leaves a point selection, so a test that selects a clip, moves the insert point and then presses
    Ctrl+C copies nothing (since 2026-10-23; the old `effectiveRegion()` fell back to the selected clip's span).
    `GesturesAndPersistence.inc`'s "clip clipboard" block does what the app does: `setSelection({id})`, Ctrl+C,
    `setInsertPoint(2.0, 1)`, Ctrl+V, and checks the pasted rectangle is the clip's span at 2 s
    ([Region editing](region-editing.md#the-operations)).
17. **Read the editor's clip before making MIDI fixtures.** `Session::createClip` points the note editor at the clip
    it makes, so `session.pattern().itemID` read afterwards is the new fixture. `RegionMove.inc`'s swept-span check
    (2026-10-24, commit `4bd0f45`) first read it after `createClip(0, 32.0, &midiID)`, got `midiID`
    back and failed its own precondition, "The note editor shows another clip before the sweep"; the read now comes
    before the fixtures ([Note editor](note-editor.md#showing-a-swept-span)).
18. **Catch a context menu before it reaches the screen.** A scenario must not open a real `PopupMenu` on the desktop
    of whoever runs the suite. `StepGrid` shows its right-button menus through a private
    `std::function<void(juce::PopupMenu, std::function<void(int)>)> presentMenu`; when it is set, a right-click hands it
    the menu and the chooser instead of calling `showMenuAsync`. `NoteMenu.inc` (commit `4783232`) keeps both, reads
    the items, submenus and headers included, with `PopupMenu::MenuItemIterator`, chooses by command id, and resets
    `presentMenu` before its block ends. Give a new panel's menu the same seam to test it
    ([Note editor](note-editor.md#gestures)).
19. **Log what a component repaints, without a window.** Give it a `juce::CachedComponentImage` whose
    `invalidate(area)` records the area and returns false, whose `invalidateAll` records the whole, and whose `paint`
    and `releaseResources` do nothing: `Component::internalRepaintUnchecked` hands every repaint of a visible
    component to its cached image, and false stops it there. Make the component visible (a hidden one repaints
    nothing), install the image, then clear the log just before the action: `setVisible(true)` and
    `setCachedComponentImage` each repaint everything, and a log holding the whole component passes any containment
    check. The DJ scenarios clear `region` (the `recordDamage` helper of `UiCost.inc`) before every action. Also
    require what must stay untouched, and see the check fail without the fix. `setCachedComponentImage(nullptr)`
    deletes the image. `NoteGrid.inc` checks the marquee's repaint on release this way (commit `d872f0b`): a first
    draft that cleared nothing passed whatever `mouseUp` did, and with the release's repaint taken out the committed
    check fails at "and letting go repaints where it stood, so it disappears" ([Note editor](note-editor.md#pitfalls)).
20. **Size a note editor for the rows a scenario presses, and press only whole rows.** Since commit `d872f0b` rows keep
    their height, so the editor's height decides which pitches have rows, and the top row may be cut short with its
    centre inside the ruler (y < `headerHeight`), where a press seeks the transport and starts a loop drag instead of
    a marquee or a note ([Note editor](note-editor.md#drawing)). A scenario that needs sixteen whole rows sets its
    editor to `StepGrid::openingHeight`, exactly sixteen whole rows with none cut short, so it follows the row height
    if that changes again: `NoteEditor.inc`, `NoteSelection.inc`, `NoteGestures.inc` and `NoteMenu.inc`, since commit
    `c257d42`. Fixed pixel heights broke twice in one day. With 11 px rows, the shared `Session`'s open clip when
    `NoteSelection.inc` began, a drum clip on the bank from 48, showed only pads 48..59 in a 250 px editor, and
    `NoteGestures.inc`'s 48-step clip brought in the scrollbar, which left pad 63 a sliver at 300 px (it moved to
    320 px). With 13 px rows a 300 px editor shows 13 whole rows and a top row 1 px short (a 181 px area), not 16. A
    press aimed at `cell(step, row).getCentre()` needs a row whose top is at or below `headerHeight`.
21. **Know which kind of clip the shared note editor is on.** Since commit `3418c1b` a drum clip's rows are only its
    rack's assigned keys and the keys its notes use ([Note editor drum rows](note-editor-drum-rows.md)), so a
    scenario about the piano roll must not run on one. By `NoteSelection.inc` the shared `Session`'s open clip is on
    the lane `DrumKitDrops.inc` dropped a kit on, a drum clip. `NoteSelection.inc` therefore opens a piano clip on a
    MIDI lane of its own (`addTrack(TrackType::midi, -1, &lane)`, `createClip`, `selectPatternClip`), and the
    clipboard and gesture scenarios after it go on using that clip. Rows no longer go below 8 px
    (`minimumRowHeight`), so `NoteKeys.inc`'s thin-rows check now expects that floor.
22. **The newest playing track is `firstReturnTrack() - 1`.** Since commit `b7bd73c` a new document has four
    playing tracks and two return tracks under them, six in all (`firstReturnTrack() == 4`), and every lane a command
    makes lands above the returns ([Return tracks and sends](return-tracks.md)). About 40 scenarios read a new track
    as `trackCount() - 1` or counted four tracks, and were changed to use `firstReturnTrack()`. `ReturnTracks.inc`
    renders, so it sits before `GesturesAndPersistence.inc`, and each of its scenarios builds a document of its own.
    `--profile-ui` and `--profile-stress` add tracks until `firstReturnTrack()` reaches their count, so they keep the
    same number of playing tracks, with the two starter returns and a send per return on every track besides.
    `StressProfile`'s `readLayout` skips returns. After the change both tools were compiled but not run.
23. **Leave room for the pinned returns.** Since commit `5632644` the arrangement pins the returns between the lanes
    and the main row, and the lanes fit only the room left (`fullLaneContentHeight` subtracts `pinnedRowsHeight`). To
    keep a scenario's old lane geometry, make its panel taller by exactly the block: the starter's two returns are
    108 px, so the 246, 400, 520 and 620 px test panels became 354, 508, 628 and 728. A lane partly under the block is
    covered, and a click there finds the return. `AutomationLanes.inc` therefore sizes its panel to
    `900 + footHeight + pinnedRowsHeight` so that the whole stack can be clicked. Do not remove a track by an index
    read earlier in the shared document, which may now be a return: `TrackManagement.inc`'s "a new card arrives
    complete" removed a return through a stale `before` and now removes the card it added
    ([Return tracks and sends](return-tracks.md#in-the-arrangement)).
24. **Give a scenario's locals names the enclosing scopes do not already use.** A scenario's block sits inside the
    runner's function, so a local with the same name as one declared further out hides it and raises warning C4456.
    `ClipLoop.inc` and `NotePlayingKeys.inc` did this when they were added in `4a6a17b`, and `640d90c` renamed their
    locals.
25. **Pump the message loop only before any other document exists.** The runners never dispatch messages, so
    anything JUCE or Tracktion posts for later never runs under them. That includes the engine's re-sorts through the
    undo manager, its 350 ms undo timer, and a cell's follow actions made as the graph is built. A check on those must
    pump the queue itself. `UndoSteps.inc` does this with a `settle` lambda, a `PeekMessage`/`DispatchMessage` loop for
    a given time that compiles only on Windows ([Undo and the history](undo-history.md#tests)). On 2026-10-28 the
    scenario first ran after `GroupUndo.inc`. There, its pump delivered what earlier scenarios had posted for documents
    long destroyed: one full CTest run crashed with 0xC0000005 inside `RhinoDAW.exe`, at a fault offset the linker map
    named as `te::ExternalControllerManager::detachFromEdit`, while two runs before it passed. Whether it crashes
    depends on what happens to be queued. Such a scenario therefore runs first in its runner, before `Session session;`,
    on documents of its own, and nothing after it pumps.

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

`RhinoDAW.exe --profile-stress` (`native/src/tests/StressProfile.cpp`, 2026-10-23) is the same kind of tool at the size of a finished song: 120 tracks in 15 groups, 60 of them MIDI with 103,072 notes, 60 audio with 1,002 clips, seven minutes at 120 BPM, 30 4OSC, 10 Rhino FM, 40 Rhino EQ and 24 Compressor, built from fixed seeds so two runs make the same document. Beside what `--profile-ui` times it times what a medium document cannot show: the getters a sync asks of each track, a save and a load, an offline render, the engine's graph rebuild after an edit while the graph is allocated, and the live graph's blocks through the hosted audio device at 512 and 128 samples. `RHINO_STRESS_TRACKS`, `RHINO_STRESS_MINUTES`, `RHINO_STRESS_CLIPS`, `RHINO_STRESS_DEVICES` and `RHINO_STRESS_RENDER_SECONDS` shrink or bisect it. Run it from a shell, never through `Start-Process`, and alternate two builds. Its figures: [The 120-track stress audit](stress-audit-2026-10.md).

A timed check inside a CTest case spends that case's limit. `--self-test` has 30 s (`native/CMakeLists.txt`), and the
DJ booth's cost check (`checkBoothCost` in `tests/DjTest.cpp`, which logs its numbers and requires no allocation) renders
10 s of silence rather than 30 for that reason: while the mixer's filters take denormals in silence, 30 s cost the case
eight seconds and left it at 25 s of its 30 (2026-10-11; [DJ deck engine](dj-deck-engine.md)). On 2026-10-23 the case took 21 s alone and 29 s run straight after a profile, one second under its limit: run the suite on an idle machine.

## Access and determinism

Classes under test befriend the runner (`friend int runArrangementTest();` in `Arrangement`, `Session`, `StepGrid`,
`AudioClipPanel`, `DjClipGrid` and the DJ view's panels; `friend int runUiProfile();` in `Arrangement`, `StepGrid` and `DeviceRack`; `friend int runStressProfile();` in `Arrangement`), one reason
big classes are split across translation units rather than into new types ([Keeping files small](keeping-files-small.md)).
`DjDeck` keeps its audio-thread methods private to `DjEngine` and to `DjDeckTest`, a class in `tests/DjTest.cpp` whose
static checks drive one deck alone, as the engine does at a block boundary.
The runner flags `--device-test`, `--arp-snapshot`, `--profile-ui` and `--profile-stress` set `Session::setCommandLineTestMode`: machine
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
- [Undo and the history](undo-history.md)
- [The 120-track stress audit](stress-audit-2026-10.md)
- [Dependency direction](dependency-direction.md)
