---
title: Seeing the UI without taking the screen
type: guide
summary: Render a Rhino panel, the whole Rhino shell or Forge's editor to a PNG without a window, then measure it instead of eyeballing it.
tags: [both, ui, testing, snapshots]
sources: []
updated: 2026-10-07
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
(`RHINO_CLIP_PANEL_SNAPSHOT`) and `AutomationLanes.inc` (`RHINO_AUTOMATION_SNAPSHOT`) do.

The Drum Rack face does the same in `runDrumRackFaceTest` (`tests/Pattern/DeviceRackTest.cpp`). Set
`RHINO_DRUMS_SNAPSHOT` to an absolute path and run `ctest --test-dir native/build -C Release -R native_pattern_workflow`.
It writes the face at 852 px (`DeviceEditorPanel::drumFaceWidth`) by `standardHeight`, painted with a `Theme`, as the
test leaves it at its end, showing a sliced roll, and deletes any old file first. A snapshot is what caught the sample
editor's envelope line drawing nothing ([A juce::Path holding only a start point is empty](juce-path-isempty-ignores-a-lone-point.md)).

A `juce::TreeView` cannot be seen this way opened: it lays out its rows asynchronously, so a snapshot shows an opened
folder with no children, even after `setOpen`, `resized()` or a rebuild at a new width. Test the browser's rows and drag
payloads instead ([Browser and library preview](browser.md)).

A snapshot, like a test calling a panel's handlers, never passes through JUCE's event dispatch. A check that needs a
real desktop peer stays off the screen instead: it puts the component on the desktop at (-10000, -10000) and runs
only on request, `RHINO_NATIVE_INPUT_TEST=1` for real clicks on the Drum Rack face, `RHINO_NATIVE_RENDER_TEST=1` for
the playhead's Direct2D handoff ([Writing Rhino tests](writing-rhino-tests.md)).

## The whole Rhino shell

`ControlWindow` is defined inside `native/src/Main.cpp`, so no test can reach it. Instead, the startup test builds the
window hidden, writes its content to a PNG and quits:

```powershell
$env:RHINO_SHELL_SNAPSHOT = "<scratch>\shell.png"; $env:RHINO_SHELL_SNAPSHOT_SIZE = "960x680"
& ".\native\build\RhinoNative_artefacts\Release\RhinoDAW.exe" --startup-test
```

Give an absolute path. The size variable is optional; 960x680 is the smallest window the shell allows.
`RHINO_STARTUP_SNAPSHOT` does the same for the loading screen. No variable sets the shell's state, so to see it with the
browser closed, flip the `browserOpen` default in `ControlWindow`, build, snapshot and flip it back. That is how the
Info View heading was caught painting into the control bar.

## Forge's editor

The Forge test binary renders the real editor, controls and cached layers included, in about a second:

```
RhinoForgeTests --snapshot out.png [width height [OSC|TABLE|MATRIX|MIX|FX|ARP [scale [preset.forgepreset]]]]
```

Paths resolve against the working directory. `ARP` presses the plate beside the keyboard, the only way in. The preset
is loaded before the editor is built, which is the only way to review what the panel draws from the patch, such as
modulation rings. A hand-written one needs only `<RhinoForgePreset formatVersion="3" name="x">` around a
`<RhinoForgeState>` holding the `<PARAM id="..." value="..."/>` entries you care about; `Processor::migrated` defaults
the rest. The version must equal `presetFormatVersion` in `src/ForgeProcessorState.cpp` (3 on 2026-10-03), or the
preset is refused, the tool exits 1 and no PNG is written. A spectral sample cannot be hand-written: decode the
standalone's saved `filterState`, or call `importSample` in `runSnapshot` temporarily
([Forge presets and state](forge-presets-and-state.md)).

## Reading the result

- Check the PNG's modification time before believing it. A failed run leaves the previous image, and a writer that does
  not delete or truncate first appends to it ([juce::File::createOutputStream appends](juce-output-stream-appends.md)).
- Sample pixels with PIL instead of trusting a magnified view, which once turned neutral grey boxes purple. PyMuPDF
  pixmaps default to 72 dpi, so pass `dpi=96` or every coordinate is off.
- Against a reference image, render at its pixel width so crops line up, find text rows by row profile before
  windowing, compare glyph widths (cap heights carry half a pixel of antialiasing) and peak stroke colours, and check
  both crops show the same feature: Forge engraves knob ticks into the collar where a reference rings them outside.

## Related

- [Writing Rhino tests](writing-rhino-tests.md)
- [App shell and control bar](app-shell.md)
- [Forge editor (panel)](forge-editor.md)
- [Proving a Forge change changed nothing](proving-a-forge-change-changed-nothing.md)
- [Driving Forge's standalone on Windows](driving-the-forge-standalone.md)
- [juce::File::createOutputStream appends](juce-output-stream-appends.md)
- [A juce::Path holding only a start point is empty](juce-path-isempty-ignores-a-lone-point.md)
