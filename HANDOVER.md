# Handover: the September 2026 file split

If you worked on Theta before commit `105cd78`, your map of this codebase is out of date. The code itself is not. This document tells you where things moved.

## What happened, in one paragraph

Five files held 68% of the application. `Session.cpp` alone was 2,661 lines covering synth patches, note editing, presets, devices, tracks, automation, clips and project loading. Each of those responsibilities was already used by a *different* caller — `StepGrid` only ever touched the note API, `DeviceRack` only the device API — so the implementation was split along the seams the callers had already drawn. `Session.cpp` is now 251 lines, `StepGrid.cpp` 402, `Arrangement.cpp` 277.

## What did NOT change

This matters more than the list of moves:

- **No header changed.** `Session.h`, `StepGrid.h` and `Arrangement.h` declare exactly what they declared before.
- **No signature changed. No call site changed.** If you call `session.setNote(...)`, that line is untouched and still correct.
- **No behaviour changed by the moves.** Total line count across `native/src` is the same before and after. Anything that looks like a behaviour change is a bug I introduced — see Known issues for the two deliberate exceptions.
- **`friend int runArrangementTest();`** still works. Friendship is declared in the headers, so tests still reach private members.

The mechanism is the one `SessionTransport.cpp` already used: **one class defined across several translation units**. `Session` is still a single class. It is just no longer a single file.

## Where things went

### `Session.cpp` (2,661 → 251)

| File | Contains |
| --- | --- |
| `Session.cpp` | `Session()`, `restoreProject`, `projectSnapshot`, `projectSaved`, `markModified`, `undo`, `redo`, `refreshAfterUndoRedo`, `ensureEditablePatternClip`, `panicReset`, `setCommandLineTestMode` |
| `SessionNotes.cpp` | All note editing **and pattern geometry**: `hasNote`, `editorNotes`, `addNote`, `removeNotes`, `adjustNoteVelocities`, `setNote`, `noteLengthSteps`, `resizeNote`, `resizeNoteFromLeft`, `fillNoteToClipEnd`, `moveNote`, `moveNotes`, `redistributeNotes`, `begin`/`endNoteGesture`, `editorStepCount`, `editorStepResolution`, `patternLengthBeats`, `setEditorStepCount`, `ensurePatternLengthSteps` |
| `SessionPresets.cpp` | `clearPattern`, `applyPatternPreset`, `insertPatternPreset`, `insertInstrumentClip`, `selectPatternClip`, `setPatternInstrument`, `isPatternDrums` |
| `SessionPatches.cpp` | The 14 preset note tables, `presetPattern`, `fillMidiClip`, and the 4OSC/ThetaWave patch application |
| `SessionDevices.cpp` | `addAudioEffect`, `addClipAudioEffect`, `addInstrument`, `addMidiEffect`, `deviceSlots`, `deviceParameters`, `begin`/`set`/`endDeviceParameterGesture`, `toggleDeviceEnabled`, `deleteDevice` |
| `SessionTracks.cpp` | `trackCount`, `trackName`, `addAudioTrack`, `removeAudioTrack` |
| `SessionClips.cpp` | `findClip`, `findAudioClip`, `shouldShowClipInArrangement`, `editClip`, `splitClip`, `duplicateClip`, `deleteClip`, `cycleClipColour`, `clipPluginCount`, `toggleTrackMute`, `toggleTrackSolo` |
| `SessionAutomation.cpp` | `clipAutomation`, `clipAutomations`, `setClipAutomationRamp`, `deleteClipAutomation`, `automationRuntimeFor`, `findAutomationRuntime`, `toggleParameterAutomationOverride`, `applyClipAutomationAt` |
| `DeviceMacros.cpp` | `activeParameterAt`, `fourOscMacroParameterAt`, `thetaWaveMacroParameterAt`, the macro name/format helpers, `exposedParameterAt`, `exposedParameterMaximum` |
| `SessionTransport.cpp` | Unchanged — transport, tempo, loop, audio import |

Pattern geometry lives with note editing rather than in `Session.cpp`, because the two were interleaved in the original and serve the same consumer.

### `StepGrid.cpp` (1,283 → 402)

| File | Contains |
| --- | --- |
| `StepGrid.cpp` | Lifecycle, layout and geometry (`cell`, `boundsFor`, `cellWidth`, `gridWidth`, `visibleStepSpan`, `footerBounds`, `rowAreaHeight`), selection state, zoom, `keyPressed`, `timerCallback`, focus, `changeListenerCallback`, `rebuildVisibleNotes`, `updatePlayhead`, scrolling |
| `StepGridPainter.cpp` | `paint` |
| `StepGridGestures.cpp` | `cellHit`, `hit`, `resizeHit`, `updatePointer`, `mouseMove`/`Down`/`Drag`/`Up`/`WheelMove`, `apply`, `pitchForIndex`, `indexForCell`, `moveCurrentNotesBy`, `moveDraggedNotesAt`, `scrollDraggedNotes`, `resizeCurrentNoteTo`, `updateMarqueeSelection` |
| `StepGridEditing.cpp` | Selection commands, clipboard (`copySelection`, `canPasteAt`, `pasteSelection`), `deleteSelection`, `fillSelectionToClipEnd`, and the subdivision and velocity modal tools |

### `Arrangement.cpp` (1,197 → 277)

| File | Contains |
| --- | --- |
| `Arrangement.cpp` | Constructor, `resized`, `fit`, `zoom`, `scrollBarMoved`, `keyPressed`, `cancelDrag`, `selectTrack`, `splitSelectedAtPlayhead`, `duplicateSelected`, `nudgeSelected`, `changeListenerCallback`, `editWillChange`/`editDidChange`, `updatePlayhead` |
| `ArrangementPainter.cpp` | `paint` |
| `ArrangementSync.cpp` | `sync`, `syncTrackControls`, `updateScroll` |
| `ArrangementGestures.cpp` | `mouseDown`/`Drag`/`Up`/`Move`/`WheelMove` |
| `ArrangementDrops.cpp` | `isInterestedInFileDrag`, `filesDropped`, `isInterestedInDragSource`, `itemDropped`, `applyBrowserDrop` |
| `ArrangementGeometry.cpp` | Unchanged |

## New conventions you need to follow

- **Every source file is listed explicitly in `native/CMakeLists.txt`.** Nothing is globbed. A new `.cpp` that is not added there will not compile, and you will get a confusing link error rather than a clear one.
- **Split a `.cpp` when it passes ~600 lines or gains a second responsibility.** Prefer a second translation unit for the same class over inventing a new type. No header change, no call-site change.
- **Shared helpers between a class's own translation units go in `*Internal.h`** — `SessionInternal.h`, `StepGridInternal.h`, `ArrangementInternal.h`. These are private to that class's files; do not include them elsewhere. Helpers shared between *different* classes get a normal header, as `BrowserIds.h` does.
- **Do not search `native/.deps/` or `research/sources/`.** They hold roughly 430,000 lines that are not Theta's code — vendored JUCE/Tracktion, and read-only study snapshots of other DAWs. Neither is ever the answer to "where is this implemented".

## Known issues you are inheriting

**1. Intermittent segfault in `native_arrangement_workflow` (pre-existing, not fixed).**
*Corrected September 2026:* on the current Windows workspace this fails far more often than the rate below — 6 of 12 runs at HEAD before the session view existed, and 5 of 12 with it, so the two are indistinguishable and the session view did not cause it. The crash is also **not** at teardown: every failing run stops between `scenario("gestures: clip drag and trim")` and `scenario("persistence: track state")` in `scenarios/GesturesAndPersistence.inc`, which is the clip copy/paste and selection block. Start there rather than in destruction order. The original note follows.

Roughly 1 run in 5. It is *not* an assertion failure — every check passes, and the crash happens during teardown, after the last scenario's `session.releaseAudioDevice()`, while the session and engine are being destroyed. A background-thread race at shutdown. It behaves identically before and after the split. If you see this, re-run before assuming you broke something; if you want to fix it, look at destruction order and what is still touching state after `releaseAudioDevice`.

**2. ThetaWave drops on the device rack now work (deliberate behaviour change, untested).**
The browser id tables were duplicated between `Arrangement.cpp` and `DeviceRack.cpp`, and had drifted: the arrangement accepted a `ThetaWave` instrument drop and the rack silently ignored it. Both now share `BrowserIds.h`, so the rack accepts it too. No test covers this path — worth exercising by hand.

**3. The 4OSC attack tests in `a9d418c` had never passed.** Fixed in `105cd78`. They asserted a rack minimum of 0 where 4OSC's own range starts at 0.001, and the new 6-second cap left attack pinned at the ceiling so the existing "can be edited" assertion on the next line could not observe an increase.

## What is deliberately still undone

Two classes are defined entirely inline inside one `.cpp` and are the next things worth separating:

- `ControlWindow` — 473 lines inside `Main.cpp` (684 total)
- `FloatingDeviceWindow` — 423 lines inside `DeviceRack.cpp` (747 total), which also has a ThetaWave-specific editor hardcoded inside a generic device window, and a `rebuildParameterControls` that near-duplicates the window's own `refresh`

Splitting either means converting inline method bodies to declaration plus definition. That is real restructuring, not a file move, so it carries different risk from everything described above. Do it deliberately.

Beyond that, two changes would most reduce the cost of adding features:

- **Device parameter helper.** Every device parameter is currently mentioned about 8 times (`referTo`, `addParam`, `attachToCurrentValue`, `valueToStringFunction`, `detachFromCurrentValue`, `updateFromAttachedValue`, the `copyPropertiesToCachedValues` list, and two header members). `ThetaWaveDevice` spends 90 of its 298 lines on this. A helper would remove 200–250 lines across the six devices.
- **`DeviceCatalog.h`.** Adding a device today needs edits in at least six places: `createBuiltInType`, the `AudioEffect`/`Instrument`/`MidiEffect` mappings, ad-hoc type-string comparisons, `BrowserIds.h`, and the editor selection in `DeviceRack.cpp`. One table of `{xmlTypeName, displayName, browserId, category, factory}` would collapse all of it.

## Verifying your work

```powershell
cmake --build native/build --config Release --parallel 2
ctest --test-dir native/build -C Release --output-on-failure
```

Five tests, all the same binary with different flags. The workflow scenarios in `native/src/tests/*/scenarios/*.inc` are bare statement blocks included inside a runner function — they share one `Session`, run in order, and may depend on state an earlier scenario left behind. The first failure aborts the whole runner, so you get one message rather than a list. Re-run after each change rather than batching, so a regression is attributable.

Expect the flaky segfault above. Everything else should be green.
