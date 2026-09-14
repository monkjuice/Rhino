# Grid, Time Signature, and Control Bar Implementation Plan

Status: implementation handoff

Baseline: `main` at or after `9388a12` (`Simplify arrangement toolbar`)

Primary platform: Windows, while preserving macOS portability

## Purpose

Implement two connected UI feature groups in Theta:

1. An Ableton-inspired arrangement grid with adaptive and fixed spacing, visible musical subdivisions, triplet support, a lower-right resolution control, and a true grid-off mode.
2. A Logic-inspired customizable top control bar containing view controls, transport, actions, a read-only LED-style display, simple tempo and time-signature controls, and a split metronome control.

This document is intended to be sufficient for a coding agent to implement and test the work without having to reinterpret the product scope.

## Attached screenshot reference index

The labels below refer to the four screenshots attached to the original user request in this conversation. They are product-direction references and take precedence over incidental differences in the linked Ableton and Logic documentation.

<a id="user-screenshot-1"></a>
### User Screenshot 1 — Ableton grid menu

The first attached image shows Ableton's arrangement context menu with the grid section circled in red.

Use it as the visual and interaction reference for:

- Separate **Adaptive Grid** and **Fixed Grid** sections.
- Five adaptive widths: Widest, Wide, Medium, Narrow, and Narrowest.
- A checkmark on the active choice.
- Fixed choices ranging from multi-bar values down to note subdivisions and Off.
- Narrow Grid, Widen Grid, and Triplet Grid commands beneath the main choices.
- A dense native popup rather than a large settings window.

Do not copy unrelated menu commands visible above the circled grid section.

<a id="user-screenshot-2"></a>
### User Screenshot 2 — Ableton tempo, meter, and metronome controls

The second attached image shows a compact Ableton control-bar section containing tempo, small status controls, a `4 / 4` meter field, a metronome icon with a separate arrow, and a `1 Bar` selector.

Use it as the visual and interaction reference for:

- Compact horizontal spacing between BPM, time signature, and metronome.
- A time-signature control separate from the LED display.
- A split metronome control with distinct icon and arrow hit regions.
- Immediate metronome toggle from the icon.
- Configuration popup from the arrow.
- Visually grouped controls that still read as individual components.

The `1 Bar` selector in the screenshot is an Ableton launch-quantization control and is not part of this Theta scope.

<a id="user-screenshot-3"></a>
### User Screenshot 3 — Logic LED display close-up

The third attached image is the visual reference for Theta's LED display.

Use it for:

- Dark inset display surface.
- Bright, compact, tabular numeric typography.
- Clear value hierarchy across position, tempo, meter, and secondary status.
- Multiple aligned readouts inside one display component.
- A small arrow on the right edge.

Logic's real LCD is interactive, but Theta's display must remain visualization-only. The screenshot is a styling and information-density reference, not an interaction contract.

<a id="user-screenshot-4"></a>
### User Screenshot 4 — Logic control bar and customization panel

The fourth attached image shows the full Logic control bar and its four-column customization panel, with red user annotations identifying the whole top control-bar span and its major regions.

Use it as the primary visual reference for:

- Treating the entire top strip as one **ControlBar** component.
- Grouping content into Views, Transport, Display, and Modes and Functions.
- Centering the display as the strongest visual anchor.
- Keeping view controls toward the left, transport near the center, and modes/functions toward the right.
- A customization panel organized by the same four categories.
- Checkbox-driven show/hide customization.
- Restore/default, save/apply, and revert actions.

The red annotations express Theta's desired component boundaries. They are not requests to reproduce every Logic button shown in the screenshot.

### Repository image paths when the attachments are added as files

The conversation attachments are not currently present as repository files. If the image files are supplied later, store them without editing or recompressing them at:

```text
docs/assets/ui-grid-control-bar/01-ableton-grid-menu.png
docs/assets/ui-grid-control-bar/02-ableton-metronome-controls.png
docs/assets/ui-grid-control-bar/03-logic-led-display.png
docs/assets/ui-grid-control-bar/04-logic-control-bar-customization.png
```

Then add the corresponding Markdown image directly below each screenshot heading. Until that happens, the numbered references above unambiguously map to the original four attachments.

## Repository rules

- Read `HANDOVER.md`, `README.md`, `ARCHITECTURE.md`, and `native/README.md` before editing.
- Application code lives in `native/src`.
- Do not search or read `native/.deps/` or `research/sources/`.
- Add every new `.cpp` explicitly to `native/CMakeLists.txt`.
- Preserve unrelated work, including the untracked `instruments/theta-forge/presets/aLiLBitDark.forgepreset` if it is still present.
- Keep the dependency direction `UI -> Session`; `Session` must not depend on UI classes.
- Split files near 600 lines or when they acquire a second responsibility.
- Commit and push focused, verified milestones to the current branch and upstream. Never force-push.

## Agreed terminology

### Control bar

The **control bar** is the complete top strip. It includes all of these groups:

- Views
- Transport
- BPM control
- Time-signature control
- LED display
- Metronome
- Modes, functions, and actions

### Display

The **display** is one component inside the control bar. It is a read-only LED-style visualization of project and transport information.

The arrow on the display's right edge opens the control-bar customization UI. It does not edit display values. See [User Screenshot 3](#user-screenshot-3) for the display treatment and [User Screenshot 4](#user-screenshot-4) for the customization structure.

### Metronome control

The metronome is a separate split control:

- Clicking the metronome icon toggles the click track.
- Clicking its arrow opens an Ableton-style metronome configuration popup.
- Clicking the arrow must not toggle the click track.

See [User Screenshot 2](#user-screenshot-2) for the compact split-control treatment.

"Play the click" means enabling the metronome so that it is audible while the transport runs, not playing a one-shot preview sample.

## Reference behavior and intentional differences

Ableton's arrangement grid can be adaptive or fixed, provides straight/triplet commands, and displays the current spacing near the lower-right of the arrangement. Its metronome icon toggles the click while an adjacent pull-down opens its settings.

Logic Pro's LCD can actually be edited: Logic allows users to change position, tempo, key, and meter through the LCD. Theta intentionally differs:

- Theta's LED display is visualization-only.
- Tempo is edited with a separate simple BPM control.
- Meter is edited with a separate simple time-signature control.
- Logic normally uses the LCD arrow for LCD modes and Control-click for whole-bar customization. Theta intentionally uses the display arrow to configure the whole control bar.

## Scope

### Included in version 1

- Adaptive arrangement grid.
- Fixed arrangement grid.
- Grid visibility/snapping off mode.
- Straight and triplet grid modes.
- Hierarchical bar, beat, and subdivision lines.
- Current effective grid resolution shown in the arrangement's lower-right corner.
- Grid context menu and keyboard shortcuts.
- One global project time signature.
- Read-only LED display.
- Separate editable BPM and time-signature controls.
- Split metronome toggle/configuration control.
- Control-bar show/hide customization.
- User-level persistence of control-bar layout and grid preferences.
- Project persistence of tempo, meter, and metronome state.
- Responsive control-bar layout with an overflow path at narrow widths.

### Explicitly deferred

- Mid-song time-signature markers and fragmentary bars.
- Tempo-map editing.
- Key-signature editing.
- Drag-reordering control-bar controls. Version 1 customization is show/hide only.
- Recording, count-in, punch, capture recording, or recording-only metronome controls.
- Alternate metronome sound files unless they are implemented as a separate later milestone.
- Fully custom metronome rhythm subdivisions unless the underlying click generator is extended.
- CPU/HD meters, MIDI activity, master output meters, and locator fields.
- Giant or floating displays.
- Replacing the note editor's existing note-entry resolution selector. Arrangement grid resolution and note-entry resolution remain separate concepts.

## Target control-bar layout

The default layout should be visually grouped rather than appearing as one undifferentiated row. [User Screenshot 4](#user-screenshot-4) defines the intended whole-bar grouping and hierarchy:

```text
+--------------------------------------------------------------------------------------+
| Views | Undo/Redo | Transport | BPM | Meter |       LED Display       | Metro | More |
+--------------------------------------------------------------------------------------+
```

Recommended initial controls:

### Views

- Browser
- Clip editor
- Devices

These invoke the existing `browserOpen`, `clipEditorOpen`, and `rackOpen` behavior owned by `ControlWindow`.

### Transport

- Go to beginning
- Play/Pause
- Stop

Do not add Record until Theta has a complete recording workflow.

### Project controls

- Compact BPM editor
- Compact time-signature editor

### Display

- Read-only musical position
- Optional read-only absolute time
- Optional read-only tempo
- Optional read-only time signature
- Optional read-only loop range
- Configuration arrow

### Modes, functions, and actions

- Metronome split control
- Automation mode
- Undo
- Redo
- Panic
- Add audio
- Audio settings

Only expose actions that currently work. At narrow widths, preserve transport and the display first and move lower-priority actions into an overflow menu.

## LED display specification

### Default content

```text
001  1  1       120       4/4
BAR  BEAT  DIV   TEMPO     METER
```

The visual style should use [User Screenshot 3](#user-screenshot-3) as its primary reference:

- A dark inset panel distinct from the surrounding control bar.
- High-contrast, tabular or monospaced numerals.
- Compact labels beneath or beside important values.
- Theta's existing restrained charcoal palette rather than copying Logic's colors exactly.
- Clear playback-state feedback without decorative animation.

### Interaction contract

- The displayed values are not editable.
- Clicking or dragging values does nothing.
- The display must not seek the playhead.
- The display must not take keyboard focus away from the current editor merely because its background was clicked.
- Only the configuration arrow is interactive.
- The configuration arrow opens the control-bar customization UI.
- The arrow must have a tooltip and keyboard-accessible action.

### Update behavior

- Reuse the existing lightweight transport text update path.
- Update only when the formatted value changes.
- Repaint only the affected display bounds, not the full window or arrangement.
- Do not introduce a continuous full-control-bar repaint timer.
- Position should use the same latency-adjusted display position policy already used by Theta's visible playheads where practical.

### Display configuration

Allow the user to choose which implemented readouts appear:

- Musical position: bar, beat, division.
- Absolute elapsed time.
- Tempo.
- Time signature.
- Loop start and loop length.
- Playback state.

Do not show fake or placeholder data for unsupported features.

The display shell and its configuration arrow should remain available even if all optional readouts are hidden. As a recovery path, right-clicking empty control-bar space may open the same customization UI.

## BPM control specification

Keep the existing Theta interaction simple:

- Use a compact increment/decrement slider with direct numeric entry.
- Preserve the current `40-240 BPM` range and one-BPM step unless separately changed by product direction.
- Continue calling `Session::setTempo`.
- Move the current BPM slider from `ControlWindow` into the control bar instead of creating competing tempo state.
- The LED display may mirror tempo, but the separate BPM control is the only tempo editor in this feature.

## Time-signature specification

### UI

Place a compact editable time-signature control beside BPM and the metronome, following the relationship shown in [User Screenshot 2](#user-screenshot-2):

```text
120 BPM    4 / 4    [metronome v]
```

Use either two compact selectors or one `N / D` editor. The LED display only mirrors the active signature.

Validation:

- Numerator: `1-99`.
- Denominator: `1`, `2`, `4`, `8`, or `16`.
- Invalid input restores the previous value and reports a status message.

### Session API

Add a small musical-meter value type and Session methods:

```cpp
struct TimeSignature
{
    int numerator = 4;
    int denominator = 4;
};

TimeSignature timeSignature() const;
juce::Result setTimeSignature(int numerator, int denominator);
double beatsPerBar() const;
```

Implementation requirements:

- Default new projects to `4/4`.
- Use Tracktion's first `TimeSigSetting` as the authoritative persisted state.
- Do not duplicate the same musical state in custom Theta project properties.
- Changing meter is one undo transaction.
- Mark the project modified and notify Session listeners.
- Refresh Tracktion tempo/meter data after a change.
- Restart playback only if required for the engine to adopt the meter safely.
- Save/reopen and undo/redo must restore the signature.

### Clip behavior

- Changing the project signature must not move, resize, or delete existing user clips.
- An untouched empty starter placeholder may be resized to one bar.
- Newly inserted pattern and instrument clips default to one bar under the current signature.
- Existing MIDI note positions remain in beats.

### Musical definition

Tracktion beat positions use quarter-note beats. Compute a bar length as:

```cpp
numerator * 4.0 / denominator
```

Examples:

| Signature | Quarter-note beats per bar |
| --- | ---: |
| 4/4 | 4.0 |
| 3/4 | 3.0 |
| 5/4 | 5.0 |
| 6/8 | 3.0 |
| 7/8 | 3.5 |

The time-signature identity still matters even where two signatures have the same duration. `3/4` and `6/8` must display differently and use their own beat-label semantics.

## Metronome specification

### Primary split-control behavior

[User Screenshot 2](#user-screenshot-2) is authoritative for the icon-plus-arrow structure.

- The left/icon region toggles `clickTrackEnabled`.
- The right/arrow region opens the popup.
- Arrow clicks never toggle the icon state.
- Icon clicks never open the popup.
- The enabled state must be communicated by shape/icon treatment as well as color.
- The control needs separate accessible names for the toggle and menu button.

### Version 1 popup

```text
METRONOME

[x] Emphasize first beat

Sound
(*) Classic

Rhythm
(*) Auto

Level
[----------o--]  -6 dB
```

Implement only functional state:

- Metronome enabled.
- Emphasize first beat.
- Click gain/level.

`Classic` and `Auto` may appear as the sole current choices if useful for the intended Ableton-style grouping, but do not present multiple choices that are not implemented.

Defer count-in, recording-only behavior, alternate sounds, and custom rhythm divisions.

### Engine state

Wrap the existing Tracktion edit properties behind Session methods rather than accessing them throughout the UI:

- `clickTrackEnabled`
- `clickTrackEmphasiseBars`
- `clickTrackGain`

Default the metronome off so current renders and playback tests remain unchanged. Metronome state belongs to the project and should survive `.thetaedit` save/reopen.

## Arrangement grid specification

### Modes

Provide:

- Off
- Adaptive
- Fixed
- Straight subdivisions
- Triplet subdivisions

Default to:

```text
Adaptive / Medium / Straight / On
```

### Grid menu

Follow the information hierarchy in [User Screenshot 1](#user-screenshot-1), limited to the grid-related commands in scope:

```text
Grid
  Adaptive
    Widest
    Wide
    Medium
    Narrow
    Narrowest
  Fixed
    8 Bars
    4 Bars
    2 Bars
    1 Bar
    1/2
    1/4
    1/8
    1/16
    1/32
    1/64
  Triplet Grid
  Show/Snap Grid
```

The menu should indicate the active mode and selected adaptive width or fixed division.

### Lower-right control

- Place the current effective resolution above or beside the arrangement's lower-right scrollbar area.
- Do not cover the horizontal or vertical scrollbar.
- The label reflects the actual resolved division, such as `1/16` or `1/8T`.
- Right-click opens the full grid menu.
- A small arrow or normal click may also open the same menu, but right-click is required.
- Remove the current arrangement-top `snap` and `snapSize` controls after the replacement works.

### Adaptive resolution

Suggested minimum logical-pixel spacing:

| Width mode | Target spacing |
| --- | ---: |
| Widest | 72 px |
| Wide | 48 px |
| Medium | 32 px |
| Narrow | 20 px |
| Narrowest | 12 px |

Choose the finest supported candidate division whose adjacent grid lines remain at or above the target spacing. Clamp safely at both ends.

- Zooming in should reveal finer subdivisions.
- Zooming out should coarsen the grid.
- Fixed mode must never change its division while zooming.
- The lower-right label must update immediately when adaptive resolution changes.

### Painting

- Draw bar lines strongest.
- Draw beat lines with medium contrast.
- Draw subdivisions faintly.
- Retain essential ruler labels and major bar boundaries when the grid is off.
- Paint lines through the visible arrangement lane area.
- Iterate only the visible musical range plus a small margin.
- Cap pathological tick counts.
- Do not create one JUCE component per line.
- Do not add a continuous repaint loop.

### Snapping

One authoritative resolved grid must drive all of these paths:

- Clip movement.
- Clip left/right trim.
- Loop creation, move, and trim.
- Browser and file drops.
- Paste placement.
- Keyboard nudging.
- Future edit-cursor operations.

Preserve existing clip-edge magnetic snapping.

Convert through musical beats rather than rounding seconds:

```text
timeline seconds
    -> TempoSequence::toBeats
    -> quantize in beats
    -> TempoSequence::toTime
    -> timeline seconds
```

This keeps the design valid when tempo changes are added later.

### Off and modifier behavior

- Grid off hides subdivision lines and disables musical snapping.
- Preserve essential major bar ruler structure for navigation.
- `Alt` temporarily bypasses an enabled grid.
- When the grid is off, `Alt` temporarily enables the current resolved grid, following Ableton's behavior.
- Clip-edge magnetic snapping should remain independent unless testing shows that users cannot reliably bypass all snapping. Document the final choice.

### Keyboard commands

- `Ctrl/Cmd+1`: narrow grid.
- `Ctrl/Cmd+2`: widen grid.
- `Ctrl/Cmd+3`: toggle triplet grid.
- `Ctrl/Cmd+4`: toggle grid on/off.
- `Ctrl/Cmd+5`: toggle adaptive/fixed.

Do not steal these commands while a text editor is actively accepting numeric or project-name input.

## Musical-time audit

Audit hard-coded four-beat assumptions in at least these files:

- `native/src/Session.cpp`
- `native/src/SessionTransport.cpp`
- `native/src/SessionNotes.cpp`
- `native/src/SessionPresets.cpp`
- `native/src/SessionClips.cpp`
- `native/src/Arrangement.cpp`
- `native/src/ArrangementGeometry.cpp`
- `native/src/ArrangementPainter.cpp`
- `native/src/ArrangementSync.cpp`
- `native/src/StepGrid.cpp`
- `native/src/StepGridGestures.cpp`
- `native/src/StepGridPainter.cpp`

Not every `4.0` represents a 4/4 bar. Conversions such as:

```cpp
steps * 4.0 / resolution
```

represent four quarter notes per whole note and remain musically valid. Rename those constants or route them through a helper so they cannot be confused with `beatsPerBar()`.

Specific corrections likely include:

- Arrangement bar labels currently assume `beat / 4` and `beat % 4`.
- Arrangement bar nudging currently uses `tempo * 4` seconds.
- Arrangement scroll padding assumes a four-beat bar.
- Default/new MIDI clip lengths assume four beats.
- `Session::editorStepCount` currently rounds clip length in four-beat bars.
- Step-grid beat and bar accents assume four steps per beat without considering the selected resolution.

For editor step count, derive the visible count from clip beat length and steps per whole note rather than forcing every bar to 16 cells. A one-bar 3/4 clip at 1/16 resolution should expose 12 steps; 5/4 should expose 20.

## Proposed class and file structure

### App shell and control bar

The feature is an appropriate reason to deliberately extract the currently inline `ControlWindow` from `Main.cpp`.

Recommended files:

```text
native/src/ControlWindow.h
native/src/ControlWindow.cpp
native/src/ControlBar.h
native/src/ControlBar.cpp
native/src/TransportDisplay.h
native/src/TransportDisplay.cpp
native/src/MetronomeControl.h
native/src/MetronomeControl.cpp
native/src/ControlBarLayout.h
```

Use `TransportDisplay` as the class name if desired, but its product name and behavior are the read-only **Display** described above.

Suggested ownership:

```text
ControlWindow
  |- owns BrowserPanel
  |- owns Arrangement
  |- owns StepGrid
  |- owns DeviceRack
  `- owns ControlBar
       |- owns Display
       |- owns BPM and time-signature controls
       |- owns MetronomeControl
       `- invokes view/action callbacks supplied by ControlWindow
```

`ControlBar` must not own the browser, arrangement, step grid, or device rack. View buttons call explicit callbacks into `ControlWindow`.

### Grid model

Add a dependency-light model such as:

```text
native/src/ArrangementGrid.h
```

It should contain testable value types and calculations:

```cpp
enum class GridMode { off, adaptive, fixed };
enum class AdaptiveGridWidth { widest, wide, medium, narrow, narrowest };
enum class GridDivision { eightBars, fourBars, twoBars, bar, half, quarter,
                          eighth, sixteenth, thirtySecond, sixtyFourth };

struct GridSettings
{
    GridMode mode;
    AdaptiveGridWidth adaptiveWidth;
    GridDivision fixedDivision;
    bool triplet;
};
```

Include pure helpers for:

- Division-to-beats conversion.
- Adaptive resolution selection.
- Narrow/widen transitions.
- Straight/triplet conversion.
- Bar/beat/subdivision line classification.
- Stable serialization IDs.

Keep JUCE painting and Session ownership out of this model.

## Control-bar customization

### Entry points

- Primary: display arrow.
- Optional recovery path: right-click empty control-bar space.

Both routes open the same component or dialog.

### Categories

Mirror the conceptual grouping in [User Screenshot 4](#user-screenshot-4) without copying unsupported items:

```text
Views
Transport
Display
Modes and Functions
```

### Stable item IDs

Persist stable IDs rather than visible labels:

```text
view.browser
view.clipEditor
view.devices
transport.return
transport.play
transport.stop
project.tempo
project.timeSignature
display.position
display.absoluteTime
display.tempo
display.timeSignature
display.loop
mode.metronome
mode.automation
function.undo
function.redo
function.panic
function.addAudio
function.audioSettings
```

### Dialog behavior

- Checkboxes provide a live preview.
- Apply/save persists the layout.
- Revert restores the state present when the dialog opened.
- Restore Defaults loads the factory layout.
- Closing without applying restores the prior layout.
- Unknown IDs from a newer version are ignored safely.
- Missing or corrupt settings fall back to defaults.

### Persistence boundary

Control-bar layout and arrangement grid UI preferences are user/workspace state. Store them through JUCE `ApplicationProperties`/`PropertiesFile`, not in the `.thetaedit` project.

Changing UI layout or grid preferences must not mark the project dirty.

Tempo, time signature, and metronome state are musical project state and must persist through Tracktion's edit state.

## Implementation milestones

### Milestone 0: baseline and extraction

1. Confirm `main` contains `9388a12` or its successor.
2. Record baseline build/test results.
3. Extract `ControlWindow` from `Main.cpp` without behavior changes.
4. Add the new source file to CMake.
5. Build and run all tests.
6. Commit and push the extraction independently.

Acceptance: the application layout and behavior remain unchanged.

### Milestone 1: meter and pure grid model

1. Add `TimeSignature` and Session meter APIs.
2. Use Tracktion's time-signature state.
3. Add `beatsPerBar()`.
4. Add the dependency-light arrangement grid model.
5. Add pure tests for meter and grid calculations.
6. Build, test, commit, and push.

Acceptance: meter and grid calculations are correct without any new UI.

### Milestone 2: musical-time correctness

1. Audit the hard-coded 4/4 assumptions listed above.
2. Correct default new-clip lengths and editor step counts.
3. Preserve existing populated clip lengths during meter changes.
4. Update ruler/bar formatting helpers.
5. Add 3/4, 5/4, 6/8, and 7/8 tests.
6. Build, test, commit, and push.

Acceptance: alternate meters do not corrupt clips or display 4/4-only bar math.

### Milestone 3: arrangement grid

1. Integrate `GridSettings` into `Arrangement`.
2. Convert snapping to beat-domain quantization.
3. Paint hierarchical visible ticks.
4. Add adaptive zoom resolution.
5. Add fixed, triplet, off, modifier, and shortcut behavior.
6. Add the lower-right resolution/menu control.
7. Remove the old top-left snap controls.
8. Build, test, perform manual zoom checks, commit, and push.

Acceptance: painting, snapping, label, and keyboard behavior share one resolved grid.

### Milestone 4: control-bar shell and read-only display

1. Add `ControlBar` and child group layout.
2. Move existing view, transport, and action controls into it without changing their behavior.
3. Move the current BPM slider into the control bar.
4. Add the separate time-signature editor.
5. Add the read-only display and its update path.
6. Add the display configuration arrow callback.
7. Verify responsive layout at multiple widths.
8. Build, test, commit, and push.

Acceptance: all existing actions still work and the display cannot edit or seek anything.

### Milestone 5: metronome

1. Add Session wrappers for click-track state.
2. Add the split icon/arrow component.
3. Implement toggle, accent, and gain.
4. Add persistence and state tests.
5. Verify audible click and accent manually in multiple meters.
6. Build, test, commit, and push.

Acceptance: icon and arrow have independent hit regions and all exposed settings work.

### Milestone 6: customization and preferences

1. Add stable layout IDs and defaults.
2. Add the customization component launched by the display arrow.
3. Implement live preview, apply, revert, and restore defaults.
4. Persist user layout and grid settings.
5. Add narrow-width overflow behavior.
6. Add serialization and bounds tests.
7. Restart the app and verify persistence manually.
8. Build, test, commit, and push.

Acceptance: UI customization survives restart without dirtying a project.

### Milestone 7: documentation and final validation

1. Update the root README's user-facing controls and shortcuts.
2. Update `native/README.md` implementation notes where appropriate.
3. Review all diffs for unintended files.
4. Run the complete Release build and CTest suite.
5. Complete the manual checklist below.
6. Commit and push documentation/final fixes.

## Automated test plan

### Pure unit-style tests

Add tests for:

- Adaptive resolution becomes finer when pixels per beat increase.
- Adaptive resolution becomes coarser when pixels per beat decrease.
- Every adaptive width produces a stable expected threshold.
- Fixed resolution is unaffected by zoom.
- Narrow/widen clamps at both ends.
- Triplet duration equals two-thirds of the straight division.
- Bar divisions use `beatsPerBar()`.
- Grid-off bypasses musical quantization.
- Control-bar layout serialization round-trips.
- Unknown/corrupt layout data falls back safely.
- Position formatting across bar boundaries.

Prefer calling a focused grid/layout test from the existing arrangement-geometry test runner rather than adding an expensive Session test for pure calculations.

### Session and persistence tests

Extend workflow coverage for:

- Default meter is 4/4.
- Set 3/4, 5/4, 6/8, and 7/8.
- Invalid meters are rejected.
- Meter undo/redo.
- Meter save/reopen.
- Existing populated clips do not change length when meter changes.
- New clips use the active one-bar length.
- Metronome enabled/accent/gain save/reopen.
- UI preferences do not alter the project snapshot or dirty revision.

### Arrangement workflow tests

Cover:

- Fixed 1/8 snapping.
- Triplet snapping.
- Adaptive resolution at two or more zoom levels.
- Grid-off free movement.
- `Alt` bypass and temporary enable behavior.
- Loop and drop snapping use the same division as clip movement.
- Existing clip-edge magnetic snap still works.
- Bar two begins at beat 3 in 3/4 and 6/8, beat 5 in 5/4, and beat 3.5 in 7/8.
- Grid shortcuts change the expected state.
- The lower-right grid control does not overlap scrollbars.

### Component tests

Exercise the control bar at widths such as:

- 1024 logical pixels.
- 1280 logical pixels.
- 1600 logical pixels.

Verify:

- Transport and display do not overlap.
- Lower-priority controls enter overflow when required.
- Display arrow remains available.
- Display values are not editable.
- Metronome icon and arrow have separate bounds/actions.
- Hidden items do not leave unusable gaps.
- Restore defaults produces the documented default set.

## Manual validation checklist

### Grid

- Keep [User Screenshot 1](#user-screenshot-1) open while comparing menu hierarchy, active checkmarks, and density.
- Zoom continuously while playback runs.
- Confirm adaptive lines and the lower-right indicator change together.
- Confirm fixed mode stays fixed.
- Confirm straight/triplet placement audibly and visually.
- Confirm grid off removes subdivisions and permits free movement.
- Confirm `Alt` behavior in both enabled and disabled states.
- Confirm clip-edge snapping remains predictable.
- Check 100%, 125%, 150%, and 200% Windows scaling.

### Time signature

- Set 3/4, 5/4, 6/8, and 7/8.
- Confirm ruler labels, bar boundaries, LED position, grid accents, and click accents agree.
- Confirm existing clips do not move or resize.
- Insert a new MIDI clip and confirm its one-bar length.
- Save, close, reopen, undo, and redo.

### Control bar and display

- Compare the overall grouping against [User Screenshot 4](#user-screenshot-4) and the display treatment against [User Screenshot 3](#user-screenshot-3).
- Confirm view toggles still open and close their existing panels.
- Confirm play, pause, stop, return-to-start, undo, redo, panic, add audio, and audio settings still work.
- Confirm BPM editing feels the same as current Theta.
- Confirm no display value can be edited or dragged.
- Confirm the display arrow opens whole-control-bar customization.
- Hide/show controls, revert, restore defaults, apply, and restart.
- Confirm UI-only changes do not add the project dirty marker.
- Resize through narrow and wide window sizes.

### Metronome

- Compare the split hit regions and compact spacing against [User Screenshot 2](#user-screenshot-2).
- Click the icon and confirm it toggles without opening the popup.
- Click the arrow and confirm it opens the popup without toggling.
- Confirm click is audible only when enabled and transport runs.
- Confirm accent follows the first beat of 4/4, 3/4, and 6/8 bars.
- Confirm gain changes are audible and persist.
- Confirm default-off metronome does not contaminate existing renders.

### Performance

- Scroll and zoom while audio plays and waveform generation is active.
- Confirm no new full-window repaint loop exists.
- Confirm playhead updates remain smooth.
- Confirm pointer drag previews remain immediate.
- Confirm control-bar display updates do not cause arrangement repainting.

## Build and test commands

```powershell
cmake --build native/build --config Release --parallel 2
ctest --test-dir native/build -C Release --output-on-failure
```

The inherited intermittent teardown crash in `native_arrangement_workflow` occurs after assertions complete. Re-run it once before treating that specific teardown crash as a new regression. All assertion failures and other test failures must be investigated.

## Suggested commit sequence

1. `Extract ControlWindow from Main`
2. `Add musical meter and grid model`
3. `Make timeline math meter aware`
4. `Add adaptive arrangement grid controls`
5. `Add control bar and read-only display`
6. `Add split metronome control`
7. `Persist control bar and grid preferences`
8. `Document grid and control bar workflows`

Before each commit:

- Inspect `git status` and the complete diff.
- Include only files belonging to that milestone.
- Run validation proportionate to the change.
- Push to the current upstream after the commit passes.

## Definition of done

The feature is complete only when all of the following are true:

- Arrangement grid painting and snapping share one authoritative resolved grid.
- Adaptive spacing changes sensibly with zoom.
- Fixed, triplet, and off modes work visually and behaviorally.
- The lower-right grid indicator always reports the effective resolution.
- Time signature controls drive ruler, bar, grid, display, new-clip, and metronome-accent math.
- Existing populated clips survive meter changes unchanged.
- The control bar is a separate component representing the entire top strip.
- The display is read-only and its arrow opens whole-control-bar customization.
- BPM remains a separate simple editable control.
- Time signature remains a separate editable control.
- The metronome icon and arrow perform separate, correct actions.
- Control-bar layout and grid preferences survive app restart without dirtying a project.
- Tempo, meter, and metronome state survive project reopen.
- Layout remains usable at supported window widths and Windows scaling levels.
- Existing arrangement, pattern, device, render, persistence, and startup tests pass.
- Focused commits have been pushed to the current upstream branch.

## Primary references

- Ableton Arrangement View, including editing-grid behavior: <https://www.ableton.com/en/manual/arrangement-view/>
- Ableton metronome settings: <https://www.ableton.com/en/manual/recording-new-clips/#metronome-settings>
- Apple Logic Pro control-bar customization: <https://support.apple.com/guide/logicpro/customize-the-control-bar-lgcp5bdd6d9d/mac>
- Apple Logic Pro project properties and editable LCD behavior: <https://support.apple.com/guide/logicpro/tempo-key-and-time-signature-lgcp8f5d126d/10.7/mac/11.0>
- Tracktion Engine time-signature model: <https://tracktion.github.io/tracktion_engine/classtracktion_1_1engine_1_1TimeSigSetting.html>
- Tracktion Engine feature summary: <https://github.com/Tracktion/tracktion_engine/blob/develop/FEATURES.md>
