---
title: Seeing the UI without taking the screen
type: guide
summary: Render a Rhino panel, the whole Rhino shell or Forge's editor to a PNG without a window, then measure it instead of eyeballing it.
tags: [both, ui, testing, snapshots]
sources: []
updated: 2026-10-25
---

# Seeing the UI without taking the screen

Do not inspect the running app with `SetForegroundWindow` and `CopyFromScreen`. The developer works on the same
machine; bringing the app forward takes their focus, and the grab came back holding their editor rather than Rhino.
Every piece of UI here renders off-screen instead, through `createComponentSnapshot`.

## A Rhino panel

Build it inside a test ([Writing Rhino tests](writing-rhino-tests.md)) and snapshot it; no desktop peer is needed.
`checkEditorFace` in `native/src/tests/EqTest.cpp` is the pattern: a `DeviceEditorPanel` given `setTarget`, sized to
`preferredWidth()` by `DeviceEditorPanel::standardHeight`, its children asserted inside the panel and off each other,
and the snapshot kept as a paint smoke test. Write a PNG only when an env var names a path, as `ClipWarp.inc`
(`RHINO_CLIP_PANEL_SNAPSHOT`), `AutomationLanes.inc` (`RHINO_AUTOMATION_SNAPSHOT`) and the Arp face in
`DeviceRackTest.cpp` (`RHINO_ARP_SNAPSHOT`) do. Give the panel a `Theme` with `setLookAndFeel` before the snapshot and
clear it after: a test component otherwise paints in stock `LookAndFeel_V4`, and the Arp's reference PNG showed JUCE's
knobs and number-less bars until commit `d28a687`.

The Drum Rack face does the same in `runDrumRackFaceTest` (`tests/Pattern/DeviceRackTest.cpp`). Set
`RHINO_DRUMS_SNAPSHOT` to an absolute path and run `ctest --test-dir native/build -C Release -R native_pattern_workflow`.
It writes the face at 852 px (`DeviceEditorPanel::drumFaceWidth`) by `standardHeight`, painted with a `Theme`, as the
test leaves it at its end, showing a sliced roll, and deletes any old file first. A snapshot is what caught the sample
editor's envelope line drawing nothing ([A juce::Path holding only a start point is empty](juce-path-isempty-ignores-a-lone-point.md)).
`RHINO_DRUM_WINDOW_SNAPSHOT` writes the Drum Rack's own window, stacked, at 1280 x 800 from `runDrumRackWindowTest`, in
the same run. `RHINO_DJ_SNAPSHOT` writes the DJ view at 1240 x 720, a track deck and a file deck either side of the
mixer, from `DjBooth.inc` in `native_arrangement_workflow` ([DJ view and the booth](dj-view.md)).
`RHINO_TRACK_METERS_SNAPSHOT` writes the left 420 px of the arrangement at 2x scale from `TrackMeters.inc` in the same
case, every card's meter and the main row's fed a different level (a held peak above a falling bar) and one row folded,
deleting any old
file first; it is the way to see the meters without opening the app ([Track level meters](level-meters.md)).
`RHINO_PREFERENCES_SNAPSHOT` writes the Preferences panel on its MIDI tab at 560 x 480, with a `Theme`, from
`KeyboardCurves.inc` in the same case, deleting any old file first ([Keyboard velocity curves](keyboard-velocity-curves.md)).
`RHINO_TIME_ROW_SNAPSHOT` writes the arrangement's time row at four zooms, stacked - two minutes, a 1/32 grid, a 1/512
grid and the deepest zoom - from `BrowserDrops.inc` in the same case, deleting any old file first
([Arrangement view](arrangement-view.md#the-time-row)).
`RHINO_NOTE_KEYS_SNAPSHOT` writes the note editor at 1000 x 300, zoomed until one step is wider than the keys,
scrolled half a step in, its rows under 8 px and one key named on its tag, from `NoteKeys.inc` in the same case,
deleting any old file first
([Note editor](note-editor.md#pitfalls)).
`RHINO_NOTE_GRID_SNAPSHOT` writes the note editor at 1000 x 300 partway through `NoteGrid.inc`, in the same case,
with the preview switched on: the bars-and-beats ruler, black-key rows and notes shaded by velocity, deleting any old
file first ([Note editor](note-editor.md#drawing)).

A `juce::TreeView` cannot be seen this way opened: it lays out its rows asynchronously, so a snapshot shows an opened
folder with no children, even after `setOpen`, `resized()` or a rebuild at a new width. Test the browser's rows and drag
payloads instead ([Browser and library preview](browser.md)).

A snapshot, like a test calling a panel's handlers, never passes through JUCE's event dispatch. A check that needs a
real desktop peer stays off the screen instead: it puts the component on the desktop at (-10000, -10000) and runs
only on request, `RHINO_NATIVE_INPUT_TEST=1` for real clicks on the Drum Rack face, `RHINO_NATIVE_RENDER_TEST=1` for
the playhead's Direct2D handoff ([Writing Rhino tests](writing-rhino-tests.md)).

## The whole Rhino shell

No runner builds the shell's `MainComponent` (`native/src/app/MainComponent.h`; until commit `db88b5c` on 2026-10-13
it was `ControlWindow`, defined inline in `Main.cpp`, so no test *could* build it - now none does yet). So the way to see
it is the startup test, which builds the window hidden, writes its content to a PNG through `writeSnapshot`
(`ui/shared/ComponentSnapshot.h`) and quits:

```powershell
$env:RHINO_SHELL_SNAPSHOT = "<scratch>\shell.png"; $env:RHINO_SHELL_SNAPSHOT_SIZE = "960x680"
& ".\native\build\RhinoNative_artefacts\Release\RhinoDAW.exe" --startup-test
```

Give an absolute path. The size variable is optional; 960x680 is the smallest window the shell allows.
`RHINO_STARTUP_SNAPSHOT` writes the start-up splash (`StartupScreen`, not the `LoadingScreen` of long commands): since
2026-10-15 the whole window, title bar or none, rather than the splash component alone, so the PNG is what the user
sees at start-up ([App shell and control bar](app-shell.md#the-window)). No variable sets the shell's state, so to see it with the
browser closed, flip the `browserOpen` default in `MainComponent`, build, snapshot and flip it back. That is how the
Info View heading was caught painting into the control bar.

A hash of the shell PNG is not a "nothing changed" check. `--startup-test` is not command-line test mode, so the four
starter tracks take random colours, and two runs of one binary differ in four 98 x 94 blocks at columns 375-472. The
DJ snapshot carries live readouts as well (three small bands near rows 41-62 and 475-484). The Drum Rack face snapshot
is byte-identical run to run. So compare per pixel, and check each differing region against a rerun of the *same*
binary before blaming the change; that is how the source move of 2026-10-13 was shown to change nothing
([The source tree mirrors the layers](source-tree-mirrors-the-layers.md)).

## Forge's editor

The Forge test binary renders the real editor, controls and cached layers included, in about a second:

```
RhinoForgeTests --snapshot out.png [width height [OSC|TABLE|MATRIX|MIX|FX|ARP|ARPHELD|PRESETS|SAVE|PRESETS:<LIBRARY> [scale [preset.forgepreset|- [forge|merli]]]]]
```

`PRESETS` and `SAVE` press LOAD or SAVE and so open the preset browser ([Forge preset browser and bank](forge-preset-browser.md)). `PRESETS:<LIBRARY>`, for example `"PRESETS:SERUM 2"` or `PRESETS:VITAL`, opens that library's folder and prints how long the listing took. The bank these pages show is a scratch one, `snapshot-bank`, made from the factory set in the **working directory**, so run them from a scratch folder rather than the repository root. The Serum 2 and Vital libraries are the machine's own and are only read. Since commit `ad945e1` (2026-10-25) the browser lists on a thread, so these pages wait for the listing to land before they paint: `settleListing` in `tests/ForgeTestTools.cpp` sleeps and calls `PresetBrowser::takeListings()` until `listing` clears, with no message loop (this JUCE build has `JUCE_MODAL_LOOPS_PERMITTED` off, so there is none to pump), as `--profile-browser` does; a test that opens the browser itself must wait the same way, through `waitForListing` in `tests/ForgeTestsBrowser.cpp`, or it reads and paints an empty list ([Forge preset browser and bank](forge-preset-browser.md#listing-on-a-thread)).

Paths resolve against the working directory. The last argument is the theme, chosen with `ui::selectTheme` before the editor is built; give `-` for the preset to name a theme with none. An unknown theme exits 2 rather than defaulting, so a typo cannot silently render Forge against a Merli baseline. With no theme argument the snapshot is Forge whatever the machine's user picked, because the test binary points the settings at a scratch file it deletes first ([Forge themes and Merli](forge-themes.md)). `--profile [width height [theme [page [preset]]]]` takes the same names, and `--profile-browser [width height]` times the preset path ([Build and test Forge](build-and-test-forge.md#test)). `ARP` presses the name column of the ARP plate on the keyboard shelf
(`arpPlateNameBounds`; the lamp and the slot buttons beside it do other things), the only way in, then plays an
eight-note chord through the voices, runs the due timers by hand with `juce::Timer::callPendingTimersSynchronously()`
(nothing runs a message loop, and the editor's timer is what carries a note from the Processor to the plate) and waits
450 ms, so the PNG shows notes falling through the plate's well ([Forge ARP plate, slots and waterfall](forge-arp-plate.md)).
`ARPHELD` (commit `f95557b`, 2026-10-13) releases four of the chord's notes, waits `sparkFallMs` + `sparkChargeMs`
so the other four's traces are in ultra (since the trace redesign, commit `90246e1`, their lights have left through the floor), then plays two short notes that fall the ordinary way. The `MidiBuffer` handed to
`processBlock` keeps the events `keyboardState` injected into it, so clear it before every later block or the chord
replays.

**Pump the timers through a wait, do not call them once.** `callPendingTimersSynchronously()` runs only a timer the
timer thread has already counted down. One call after an 80 ms sleep caught the first editor tick and missed the next,
so `ARPHELD`'s four releases were not seen until the 3.3 s wait ended and the lights showed mid-fall; matching the
lights' x positions to the notes is what exposed it. `runSnapshot` (`tests/ForgeTestTools.cpp`) now loops sleep 15 ms,
call, until the wait ends, as a message loop would. Any snapshot that needs more than one editor tick must pump this way.

The preset
is loaded before the editor is built, which is the only way to review what the panel draws from the patch, such as
modulation rings. It goes through `Processor::loadPreset`, so since 2026-10-19 a `.SerumPreset` or `.vital` also works,
converted on the way and named on the title plate ([Forge preset import](forge-preset-import.md)). A hand-written one needs only `<RhinoForgePreset formatVersion="3" name="x">` around a
`<RhinoForgeState>` holding the `<PARAM id="..." value="..."/>` entries you care about; `Processor::migrated` defaults
the rest. The version must equal `presetFormatVersion` in `src/ForgeProcessorState.cpp` (3 on 2026-10-03), or the
preset is refused, the tool exits 1 and no PNG is written. A spectral sample cannot be hand-written: decode the
standalone's saved `filterState`, or call `importSample` in `runSnapshot` temporarily
([Forge presets and state](forge-presets-and-state.md)).

## Reading the result

- Check the PNG's modification time before believing it. A failed run leaves the previous image, and a writer that does
  not delete or truncate first appends to it ([juce::File::createOutputStream appends](juce-output-stream-appends.md)).
- A snapshot paints once, so it never shows a kept layer. Forge draws an FX picture straight through on the first frame at a size. A paste that faded every later frame therefore showed only in the running app ([A JUCE image is drawn at the last colour's opacity](juce-image-draws-at-current-opacity.md)). To check a cache, paint twice and compare.
- Sample pixels with PIL instead of trusting a magnified view, which once turned neutral grey boxes purple. PyMuPDF
  pixmaps default to 72 dpi, so pass `dpi=96` or every coordinate is off.
- Against a reference image, render at its pixel width so crops line up, find text rows by row profile before
  windowing, compare glyph widths (cap heights carry half a pixel of antialiasing) and peak stroke colours, and check
  both crops show the same feature: Forge engraves knob ticks into the collar where a reference rings them outside.

## Related

- [Writing Rhino tests](writing-rhino-tests.md)
- [App shell and control bar](app-shell.md)
- [Forge editor (panel)](forge-editor.md)
- [Forge themes and Merli](forge-themes.md)
- [Proving a Forge change changed nothing](proving-a-forge-change-changed-nothing.md)
- [Driving Forge's standalone on Windows](driving-the-forge-standalone.md)
- [juce::File::createOutputStream appends](juce-output-stream-appends.md)
- [A juce::Path holding only a start point is empty](juce-path-isempty-ignores-a-lone-point.md)
- [A JUCE image is drawn at the last colour's opacity](juce-image-draws-at-current-opacity.md)
