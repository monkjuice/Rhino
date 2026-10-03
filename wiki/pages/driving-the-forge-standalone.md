---
title: Driving Forge's standalone on Windows
type: guide
summary: Run, click and capture the Forge standalone without costing the developer their patch, pointer or screen, or use a headless test.
tags: [forge, ui, windows, debugging]
sources: []
updated: 2026-10-03
---

# Driving Forge's standalone on Windows

The standalone is `instruments/rhino-forge/build/RhinoForge_artefacts/Release/Standalone/Rhino Forge.exe`: JUCE's stock
standalone wrapper around the same `Processor` as the VST3, and the quickest way to look at a change without a host. A
rendered snapshot usually answers the question while taking nothing from the developer
([Seeing the UI without taking the screen](headless-ui-snapshots.md)); drive the real window only when real input is
the point.

## Before launching

- **Save the developer's patch.** The standalone keeps its state between runs in
  `%APPDATA%\Rhino Forge\Rhino Forge.settings` (the `filterState` entry), so driving it changes what they open next.
  Copy the file aside and put it back afterwards. Deleting it opens Forge on its defaults, where the sub and all three
  oscillators are on, which is the way to capture those modules enabled.
- **Tick the MIDI input once** in Options → Audio/MIDI Settings → *Active MIDI inputs*. JUCE leaves inputs off on
  Windows; the choice is then remembered in the same file ([Forge MIDI learn](forge-midi-learn.md)).

## Clicking

Synthetic window messages do not work. JUCE takes Windows pointer input (`WM_POINTERDOWN`) here, so a posted
`WM_LBUTTONDOWN`/`WM_LBUTTONUP` is silently ignored. Only real `SendInput` or `mouse_event` clicks land, and those take
over the developer's pointer for as long as they run: say so first, check what they are doing (`GetForegroundWindow`
and its title), and keep it short.

To check that a new control is wired up, a test is better. `createEditor()` runs headless under
`ScopedJuceInitialiser_GUI`, and the control is reachable through `getChildren()` and a `dynamic_cast`, the way
`runSnapshot` in `tests/ForgeTestTools.cpp` finds the page tabs. That exercises everything except JUCE's own event
delivery.

## Coordinates and capture

- **Measure the window after it settles.** Read straight after `Start-Process`, the origin is where the window is about
  to leave, and every click lands somewhere else.
- **Offset for the Options strip.** It sits inside the client area above the panel, pushing panel coordinates down
  (by 28 px when measured). Derive the offset as the client height minus the panel height (900 by default,
  `defaultPanelHeight` in `ui/ForgePlacement.h`) rather than hard-coding it.
- **Capture without foregrounding.** `CopyFromScreen` grabs whatever is on top, and a covered window looks like a
  rendering bug. Check `GetForegroundWindow` before every grab, or use `PrintWindow(hwnd, hdc, 2)`
  (`PW_RENDERFULLCONTENT`), which renders Forge's own content whatever covers it.

## Afterwards

Restore the settings file. A leftover `Rhino Forge.exe` locks its own executable, and the next build fails with
`LNK1104`. Unlike RhinoDAW or a DAW, which may hold unsaved work and are never killed without asking, it is safe to
close: find it with `tasklist | findstr "Rhino Forge"` and end it with `taskkill /PID <id> /F`
([LNK1104 means a running binary holds the file](locked-executable-lnk1104.md)).

## Related

- [Build and test Forge](build-and-test-forge.md)
- [Seeing the UI without taking the screen](headless-ui-snapshots.md)
- [Forge editor (panel)](forge-editor.md)
- [Forge presets and state](forge-presets-and-state.md)
- [Reading MIDI hardware on Windows](reading-midi-hardware-on-windows.md)
