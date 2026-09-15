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
| `SessionAutomation.cpp` | Automation. *Rewritten September 2026 — see "Automation moved from clips to tracks" below.* `trackAutomations`, `trackAutomationState`, `showTrackAutomation`, `hideTrackAutomation`, `setTrackAutomationPoints`, `clearTrackAutomationPoints`, `automationRuntimeFor`, `findAutomationRuntime`, `toggleParameterAutomationOverride`, `applyTrackAutomationAt` |
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

**1. Intermittent segfault in `native_arrangement_workflow` — FIXED, September 2026.**
It was never a shutdown race or a threading problem. `scenarios/AudioClipEditing.inc` caches a raw `te::Clip*` in `clip`, and `scenarios/TrackManagement.inc` then moves that clip across tracks and undoes the moves. Undo rebuilds clips from the edit state, so the cached pointer was dangling from that point on; `scenarios/GesturesAndPersistence.inc` dereferenced it in its very first assertion. Whether the freed memory still read as a clip depended on the heap, which is what made it look like a 1-in-5 race that tracked no code change. The fix is one line: `GesturesAndPersistence.inc` looks the clip up by id again before using it.

The lesson generalises. **A raw `te::Clip*` does not survive an undo, a cross-track move, or a project restore.** Scenarios share one `Session` and run in order, so a pointer cached several files earlier is almost always stale. Re-fetch with `session.findClip`/`findAudioClip` at the top of any scenario that inherits one.

**2. ThetaWave drops on the device rack now work (deliberate behaviour change, untested).**
The browser id tables were duplicated between `Arrangement.cpp` and `DeviceRack.cpp`, and had drifted: the arrangement accepted a `ThetaWave` instrument drop and the rack silently ignored it. Both now share `BrowserIds.h`, so the rack accepts it too. No test covers this path — worth exercising by hand.

**3. The 4OSC attack tests in `a9d418c` had never passed.** Fixed in `105cd78`. They asserted a rack minimum of 0 where 4OSC's own range starts at 0.001, and the new 6-second cap left attack pinned at the ceiling so the existing "can be edited" assertion on the next line could not observe an increase.

## Automation moved from clips to tracks (September 2026)

Automation used to be a ramp stored inside a clip's `ValueTree` and drawn only across that clip. It is now a **track** property spanning the whole timeline, which is how every other DAW behaves.

- **Storage.** A `thetaTrackAutomation` child on the owning track's state, keyed by `{track, slot, parameter}`, holding `point` children with `time` and `value`. It rides the project snapshot, so there is no save path of its own. Old projects simply come back without their clip ramps; nothing migrates them.
- **Two states per lane.** Fewer than two points means "revealed but never drawn": the arrangement shows a dotted line at the knob's current value and the lane drives nothing. `deviceParameters(...).automated` stays false until a curve exists.
- **Reaching it.** Right-click a knob in `DeviceEditorPanel` for *Show automation* / *Show automation on new lane* / *Hide* / *Delete*. `Session::lastTouchedDeviceParameter()` is no longer how a lane is chosen; it remains only for legacy callers.
- **Gone:** `clipAutomation`, `clipAutomations`, `setClipAutomationRamp`, `deleteClipAutomation`, `applyClipAutomationAt`, `hasClipAutomationTarget`, `ClipView::automations` and the `Arrangement` clip-automation overlay. `hasClipAutomationTarget` became `hasActiveTrackAutomation`.
- **New UI file:** `ArrangementAutomation.cpp` holds the lane row stack, the curve painting and the automation pointer gestures. The arrangement's lanes are no longer uniform-height: `Arrangement::rows` is the authority and `lane(track)` looks a track up in it, because a lane sent to "its own row" stacks a dimmed clone of the track underneath and pushes everything below it down.
- **Not done yet:** the pinned main row has no space to stack a lane, so `showTrackAutomation` refuses a master target rather than silently doing nothing. Lane targets are plugin-list indices, so swapping a track's instrument leaves a lane pointing at whatever now occupies that slot.

## The starter document is one empty track (September 2026)

Theta used to open with `Pattern synth` (a 4OSC plus a hidden placeholder pattern clip) and `Audio 1`. It now opens with **one** track named `Track 1`, running no instrument, plus the pinned main row. The hidden pattern clip is still there, so the note editor works from the first click; it just drives nothing until an instrument is dropped.

If you are updating code or a test that assumed the old layout:

- **No index is "the audio track" any more.** `masterTrackIndex()` is `trackCount()`, so on a fresh document index 1 *is* the main row. Passing 1 to a track-indexed call now silently targets main instead of failing. `Session::addAudioEffect` lost its `= 1` default for exactly this reason — say which track.
- **`importAudio(file)`** with no target appends a new track and puts the clip there, rather than landing on track 1.
- **`audioUtility`** is null until a second track exists; `addAudioTrack` adopts the first one it creates. `utility` still belongs to track 0.
- **`restoreProject`** accepts a one-track project; it used to require two.
- **`removeAudioTrack`** now refuses only when one track is left, not two.
- **Browser double-clicks** act on the selected track through `BrowserPanel::targetTrack`, wired in `Main.cpp`. They used to hardcode track 0 or track 1 per item kind.
- **Test scenarios** that want an audio track must `addAudioTrack()` first. Several already got one for free from `importAudio`.

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

All five should be green, every run. The intermittent arrangement segfault described above is fixed; if it comes back, suspect a stale clip pointer in a scenario rather than a threading race.
