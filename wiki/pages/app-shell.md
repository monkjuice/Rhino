---
title: App shell and control bar
type: component
summary: The shell under app/ - Application in a Main.cpp of about 200 lines, MainWindow, and MainComponent across six units holding the control bar, the docked browser, the single lower pane and the 30 Hz timer that polls what the engine never broadcasts.
tags: [rhino, ui, shell]
sources: []
updated: 2026-10-13
---

# App shell and control bar

The shell is `native/src/app/` ([The source tree mirrors the layers](source-tree-mirrors-the-layers.md)). Since commit `db88b5c` (2026-10-13) `Main.cpp` is about 200 lines (204 at that commit) and holds only `Application` (the log, the staged start-up, a `.rhinoedit` given as first argument, the `RHINO_RENDERER` choice, the `RHINO_STARTUP_SNAPSHOT` and `RHINO_SHELL_SNAPSHOT` writes) plus `START_JUCE_APPLICATION`; until then it was about 2,200 lines with the window and its content defined inline. The content is `MainComponent`, JUCE's own name for a window's content (it was `ControlWindow`, which was not a window and collided with the nested `Window`), declared in `MainComponent.h` and defined across six units by responsibility:

- `MainComponent.cpp`: the constructor and all the panel wiring, the destructor, `editWillChange`/`editDidChange`, `changeListenerCallback`, `logStatus`.
- `MainComponentLayout.cpp`: `paint`, `resized`, the browser-divider mouse handlers, `toggleBrowser`, `toggleInfoView`, `applyPaneLayout`, `paintedLayout`, `infoViewArea`, `browserColumnBottom`, `isOverSplitter`.
- `MainComponentControlBar.cpp`: `paintControlBar`, `layoutControlBar`, `toggleRecording`, `updateRecordButton`, `showMetronomeMenu`, `showDisplayMenu`.
- `MainComponentPanes.cpp`: `setDjViewOpen`, `toggleClipEditor`, `clipForPane`, `toggleDeviceView`, `showDeviceView`, `followDeck`, `selectedAudioClipID`, `isMidiClipSelected`, `rememberPaneSelection`, `openClip`, `refreshEditorPanes`, `updateEditorLabel`.
- `MainComponentCommands.cpp`: `keyPressed`, the File, Edit, View and Help menus, `sendClipboardShortcut`, `showAudioSettings`.
- `MainComponentTimer.cpp`: the 30 Hz `timerCallback`, `updateReadings`, `loopText`, `placeText`, `audioDeviceText`, `updateHint`, and `formatMemory` as a file-static.

The header (about 260 lines) keeps the members in their original order, because the constructor's initialiser list and the `addAndMakeVisible` z-order depend on it, and holds the layout constants, `enum class LowerPane` and the inline one-liners. `PushController push` is deliberately the last member, so it is destroyed first, while the panels it talks to are still there. `Application` calls `startHardwareControllers()` at start-up stage 2, never under `--startup-test` ([Ableton Push 1](ableton-push.md), 2026-10-13). Around it: `MainWindow.h/.cpp` is the window (`TitleMenuButton` stays nested in its header); `ControlBarButtons.h` holds `StopButton`, `RecordButton`, `ViewToggle` and `SplitterBar`, header-only as before; `AppPlatform.h/.cpp` holds `rhinoLogFile`, `avoidLegacyDirectSound`, `registerRhinoProjectFileAssociation` and `keepWindowAbove` (below), the one unit that includes `windows.h` and `shlobj.h`; `writeSnapshot` is an inline function in `ui/shared/ComponentSnapshot.h`, so tests can share it later (they still carry their own copies). The test-flag dispatch is `std::optional<int> runTestFlag(const juce::String&)` in `tests/TestRunners.h/.cpp`, with `prepareCommandLineAudio` as its file-static, so `Main.cpp` no longer includes `tests/Pattern/DeviceRackTest.h`; the runner declarations stay in `model/Session.h`. Every body moved verbatim, every unit is under 400 lines ([Keeping files small](keeping-files-small.md)), and all six CTest cases and the snapshots matched before and after. No runner builds a `MainComponent` yet, which is why `RHINO_SHELL_SNAPSHOT=<png>` under `--startup-test` is the way to see it ([Seeing the UI without taking the screen](headless-ui-snapshots.md)).

The dead code went the same day (chore, *remove the shell's dead code*): a hint `juce::Label` that was never shown, and the four `TextButton`s `fileMenu`, `editMenu`, `viewMenu` and `helpMenu` kept only as fallback menu targets, never added to the view (the title bar's own buttons in `MainWindow` are the real ones). `showFileMenu`, `showEditMenu`, `showViewMenu` and `showHelpMenu` take a `juce::Component&` target now, not a nullable pointer.

## The window

`MainWindow` (`app/MainWindow.*`; `Application::Window` nested in `Main.cpp` until `db88b5c`) draws its own title bar but sets JUCE's drop-shadow flag, because on Windows that flag is what gives a borderless window native frame styles: without it there is no Aero Snap, no snap layouts, and maximise covers the taskbar. Full screen (F12) is kiosk mode, separate from maximise, with three overrides keeping the title bar. `StartupScreen.h` marks real completed startup stages, not a simulated percentage.

## Rhino's own windows float above it

Since 2026-10-13, at the user's request, the plugin window (`FloatingDeviceWindow`: Forge's editor, the one MAP opens, the fallback) and the Drum Rack window, while restored, stay above the main window, as Live's plugin windows do, but never above other apps. The aim: a sound dragged from the browser lands on the Drum Rack window instead of the window sinking at the first click in the main window. Built and tested only as far as the request (below); not yet tried by the user.

- **How.** `keepWindowAbove(window, owner)` (`AppPlatform.cpp`) makes `window` an owned window of `owner`'s top-level window with `SetWindowLongPtrW(GWLP_HWNDPARENT)`, written only when the owner differs, so calling it on every resize is cheap; a null owner releases it. Windows keeps an owned window above its owner, sinks it with the owner when another app comes forward and hides it while the owner is minimised. The UI cannot see `app/`, so it asks through `DeviceRack::keepAboveShell(window, floating)`, which `MainComponent.cpp` wires to `keepWindowAbove(window, floating ? this : nullptr)` ([Device rack and device editors](device-rack.md)). Off Windows it does nothing yet; macOS would need a floating window level that hides when the app deactivates.
- **Rejected: polling `Process::isForegroundProcess` and toggling `setAlwaysOnTop`.** JUCE has no "above my own app only" window. Always-on-top floats above every app, and on Windows switching it off (`HWND_NOTOPMOST`) puts the window above every non-topmost window, i.e. above the app the user just switched to.
- **Gotcha: never recreate the main window's native window while one is open.** Windows destroys owned windows with their owner's HWND, out from under JUCE. Nothing does today (as of 2026-10-13): full screen (`MainWindow::setAppFullScreen`, JUCE's Windows kiosk mode) changes style flags in place, `MainWindow` sets its title bar only at construction, and at shutdown `ResizableWindow` deletes its content (`MainComponent`, the rack, its windows) before the main HWND goes. Changing the title bar after construction, or anything else that recreates the peer, must close these windows first.
- Owned windows are not always-on-top. JUCE's popup menus and tooltips are, and its `DialogWindow` and `AlertWindow` go on top when any always-on-top window exists, so nothing of Rhino's is expected to hide behind them (reasoned from JUCE, not tested).
- `runDrumRackWindowTest` checks only that a restored window asks to float; no test owns a real HWND.

## The control bar

Under the 72 px bar sit the docked browser, whose 3 px divider is its resize handle, and the arrangement, with no margins or frames. `layoutControlBar` (`MainComponentControlBar.cpp`) places from a running x: browser toggle, tempo and signature, then rewind, stop, play and record; from the right edge inwards, the view switch (Arrange, then DJ at the very end), then the metronome and its menu, then redo and undo, each group in its own section (the order the user asked for on 2026-10-10). The readout is centred in what remains, capped at 480 px, and hidden rather than squeezed when under 260 px remain.

- `ControlBarFields.*`: `ValueDragBox`, the tempo and signature fields you drag; one drag is one undo step (`Session::beginTempoGesture`). Ctrl makes the step fine, and pressing or releasing it mid-drag carries on from the value on screen, counting the new step from the last pointer position; it used to re-read the whole drag at the new step (commit `bac4059`).
- `ControlBarIcons.*`: glyphs are paths worn by the borderless `IconButton`, because Unicode transport symbols fell through to a different system face on every machine.
- `TransportDisplay.*`: three columns in one recess; when narrow it drops whole readings in a fixed order and never moves the position or clock. `WallClock.h` spells every time, here and on the timeline ruler. The position and the loop read bar, count and sixteenth from `musicalPosition` (`ArrangementGrid.h`), counting in the signature's own note as the ruler does, so 6/8 has six counts of two sixteenths (commit `4416222`).

## The second view

The Arrange/DJ switch at the right end of the bar - two icons, `ViewToggle` on `IconButton`, drawn from `assets/arrangement_view.svg` and `assets/dj_view.svg` compiled in beside the fonts (JUCE's SVG reader has no `currentColor`, so black stands in and the painter tints a copy of the drawable: `palette::viewActive` green for the view showing, `palette::viewIdle` light grey for the other; `IconButton::setIdleColour` exists for it) - and Tab, swap the arrangement for the [DJ view](dj-view.md) in the same rectangle (`setDjViewOpen`, which the View menu's DJ View item calls too). The switch first opened the session view, a clip launcher removed on 2026-10-11 ([Clip slots, the DJ consoles' cells](clip-slots.md)); its gate went with it, and the names that still said session were renamed for the DJ view (`djViewOpen`, `djToggle`). The lower pane is the same under either view, which is how a deck's Edit key opens the cell it plays in the note editor, or in the audio editor for an audio clip. Switching views hands the Device View the track the view being shown has selected: the arrangement's card, or the deck last clicked when it plays a track of the song. The two toggles are drop targets as well (`ViewToggle`): a clip from a lane, a console's cell or a browser item hovering one shows that view, so a drag can cross from a lane to a console's clips grid and back; a drop on the toggle itself does nothing ([DJ view and the booth](dj-view.md)).

Since 2026-10-10 the song's transport is out of reach while the DJ view shows: opening the view stops the song if it is rolling, dims rewind, stop, play and record (`RecordButton` paints grey when disabled whatever the tracks say), and makes Space the focused deck's play key (`DjView::togglePlayFocused`; F9 answers with a hint). The lower pane follows the focused console as it follows a track card (`followDeck`): a press on a console puts its track's chain in the Device View, opening the pane if it was closed, and moves an open clip editor to the clip the deck plays - the note editor for a MIDI clip, the audio editor for an audio one (`Session::djDeckEditClip` gives the cell the deck plays and, since 2026-10-12, nothing while it plays none, never an arrangement clip; an open clip editor closes then). The view switch follows the focused deck without opening a closed pane. A file deck has no track of the song and changes nothing. `followDeck` reads whether a clip editor was open *before* it moves the arrangement's track selection, because that move re-reads the arrangement's own clip selection through `refreshEditorPanes` and may close the pane on the way.

## The lower pane

Note editor, audio clip editor and Device View are three faces of one pane (`LowerPane`). Double-clicking a clip opens its editor, which then follows the clip selection; a card click opens the Device View only when the pane is free. The Clip toggle opens the editor for `clipForPane()`: the arrangement's selected clip, or in the DJ view the focused deck's clip (the cell it plays; the arrangement's selection when it plays none). It used to ask the arrangement in both views, and the arrangement selects no clip while the consoles show, so a clip editor closed for the Device View could not be opened again from the DJ view (reported 2026-10-10). It floats over the arrangement's foot (`Arrangement::setBottomInset`): pushing the lanes up refitted them under a dragged split, so every clip grew and shrank under the pointer. A layout change asked for by a click waits for the timer and for no mouse button down, because that press may be starting a drag. The three faces became one pane in commit `a33503f` (2026-09-30). `MainComponent::resized` (`MainComponentLayout.cpp`) floors the pane at 112 px (`minimumPaneHeight`) for the clip editors and at `DeviceRack::minimumHeight` (219 px) for the Device View; one 112 px floor for all three let a drag cut devices off (commit `67031ba`). The clamp writes back into `lowerPaneHeight`, so a clip editor opened after the Device View keeps the taller height until dragged, deliberately. No runner builds a `MainComponent`, so nothing checks this clamp. The faces not showing, like the DJ view while the arrangement shows, mark themselves stale rather than rebuilding on every change, and catch up when shown ([What a change costs the interface](ui-cost-of-a-change.md)).

## The 30 Hz timer

`MainComponent`'s timer (`MainComponentTimer.cpp`) polls what the engine does not broadcast (`Session::recordingStopped`, for the clips a recording wrote, and `Session::djPoll`, for the DJ booth's finished reads, its decks' bindings and the cells to make again), applies pending pane layout, updates the Info View hint (`InfoHints.h`), and rewrites the readout. It no longer plays track automation: since commit `fe163c8` the engine does ([Track automation](automation.md)).

## Related

- [Arrangement view](arrangement-view.md)
- [Colours and typography](colours-and-typography.md)
- [Computer MIDI keyboard](computer-keyboard.md)
- [Ableton Push 1](ableton-push.md)
- [Device rack and device editors](device-rack.md)
- [Drum Rack](drum-rack.md)
- [Playhead rendering](playhead.md)
- [juce::File::createOutputStream appends](juce-output-stream-appends.md)
- [Transport, tempo and loop](transport.md)
- [What a change costs the interface](ui-cost-of-a-change.md)
