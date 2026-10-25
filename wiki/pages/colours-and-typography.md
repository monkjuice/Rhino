---
title: Colours and typography
type: convention
summary: Rhino names chrome colours in Theme.h's palette and draws text in embedded Inter; Forge names its own as theme tokens in ForgeTheme.h and draws in four faces.
tags: [both, ui, theme, fonts]
sources: []
updated: 2026-10-25
---

# Colours and typography

## Rhino: colours are named, never spelt

`palette` in `native/src/ui/shared/Theme.h` (included as `"ui/shared/Theme.h"`) is where a chrome colour gets its name, and code uses the token rather than a hex literal (`AGENTS.md`). The tokens, as of 2026-10-05:

- **Surfaces and text:** `appBackground`, `globalBar`, `sideSurface`, `trackCard`, `control`, `hover`, `border`, `text`, `textDim`, `disabled`, `activeNeutral`, `displayInset`, `recordAccent`.
- **The timeline:** `arrangement`, `automationLane`, `minorGrid`, `beatGrid`, `barGrid`. The lanes are light and ruled dark, which keeps the grid's contrast under room light.
- **Meaning:** `displayText`, `displayTextDim` and `displayTextFaint` for the readout, plus `volume`, `pan` and `selection`; `meterSafe`, `meterHot` and `meterClip`, the green, yellow and red of the [track level meters](level-meters.md) (2026-10-15). `selectedRowWash` (`0x12ffffff`, about 7% white) lights a selected track's card and lane: a wash rather than a colour, so it lifts whatever ground it lies on by the same step (commit `83135f7`, 2026-10-15; [Arrangement view](arrangement-view.md)). `midiEffect` (cyan) marks MIDI-effect controls; `arpSequence` (orange) is only for [Rhino Arp](rhino-arp.md)'s sequence display, a colour no control wears so the output stands apart from the controls.
- **The note editor** (2026-10-16): `noteBody`, `noteSelected`, `noteEditorButton` and `noteEditorButtonOn`, `keyHoverText` (commit `4783232`, 2026-10-25: the name of the key under the pointer, on a `noteBody` tag; [Note editor](note-editor.md#gestures)), plus the [velocity lane](note-velocity-lane.md)'s `velocityLane`, `velocityLaneAlternate`, `velocityLaneStepRule`, `velocityLaneBarRule` and `velocityGuide`. Commit `f2ae477` added the grid's cells and lines ([Note editor](note-editor.md#drawing)): `noteCell` (`0xff46515a`) and `noteCellAlternate` (`0xff3f4a53`) for a white key's row on alternate beats, `noteCellBlackKey` (`0xff38424b`) and `noteCellBlackKeyAlternate` (`0xff323c45`) for a black key's, and `noteBarLine`, `noteBeatLine` and `noteStepLine` for the vertical lines. `noteCellAlternate` was a literal `0xff3b4650` and was brought closer to `noteCell` so that the darker black-key rows never match the beat alternation. `noteCell` is also what a quiet note's colour fades towards. The ruler's ticks and labels, the note edges, the row lines and the footer in `StepGridPainter.cpp` are still literals.

`Theme` feeds the tokens to JUCE's stock colour ids, so menus, dialogs and scrollbars match the panels unaided.

The chrome is neutral on purpose. Only what carries meaning is saturated: a track's own colour, volume cyan, pan red and the record light. Selection is grey everywhere, so a selected card never competes with the track colour beside it. Content (waveforms, browser row markers, device accents) keeps its own colour ([Track and clip colours](track-and-clip-colours.md)).

**Not applied everywhere yet** (checked 2026-10-03). Files that predate the palette (2026-09-30) or escaped it still spell colours as `juce::Colour(0xff...)` literals: the rename editor in `ArrangementRename.cpp`, the loading screen in `StartupScreen.h`, `AudioClipPanel.cpp`, `AudioClipWarp.cpp`, and the note editor, device rack and device faces (`StepGridPainter.cpp`, `DeviceRack.cpp`, `DeviceEditorPanel*.cpp`). New code names a `palette` token.

## Rhino: type and icons

- Inter Regular and SemiBold are embedded (`native/assets/fonts/`, under the SIL OFL) and served by `Theme::getTypefaceForFont`. A face drawn for the screen is bundled because Direct2D's greyscale antialiasing without grid fitting leaves the Windows shell font soft.
- `uiFont` asks for whole-pixel em sizes. A JUCE font height includes ascent and descent, so a height of 12 rasterises Inter at 9.9 px per em, off the pixel grid.
- Text Rhino paints goes through `drawSnappedText` (`Theme.h`), which rounds the baseline to a pixel row: JUCE centres a line in floats, so text drawn another way can land half a pixel low and render grey. The Preferences window's tabs are two radio `TextButton`s rather than a `juce::TabbedComponent` because a TabbedComponent's tabs draw their own text off that baseline (2026-10-15; [App shell and control bar](app-shell.md#preferences)).
- The cut is chosen from the style string, not from `Font::isBold()`, which answers false for SemiBold ([JUCE's isBold() is false for SemiBold](juce-isbold-misses-semibold.md)).
- Toolbar icons are paths in `ControlBarIcons.*`, not characters. Inter's Latin cuts lack the transport symbols, and a fallback glyph is a different face on every machine. The view switch's two icons are the exception: SVG files compiled in and drawn as `Drawable`s, with `currentColor` replaced by black in the text (JUCE's SVG reader does not know it) and black swapped for the state's colour, `palette::viewActive` or `viewIdle`, on a copy at paint ([App shell and control bar](app-shell.md)).

## Forge: its own look

Forge shares none of this. Its panel is a dark metal chassis (`ui/ForgePanels.h`). Since commit `425b253` (2026-10-16) every chrome colour is a token in `ui/ForgeTheme.h`, with a value in each theme's palette, Forge's and Merli's; a new chrome colour is a token, never a literal ([Forge themes and Merli](forge-themes.md)). The signal path is `electricBlue` and the modulators `signalViolet`, names kept even where Merli paints them orange and violet. Each oscillator wears a plate colour picked from its LED (`oscRed`, `oscOrange`, `oscGreen` by default, or blue; `panelColourOf` in `ui/ForgeStyle.h`), and each FX type has its own colour (`fxTypeColour`), which stays a literal as content colours do. Text uses four subset OFL faces, one per job: Chakra Petch SemiBold for headers and tabs, Rajdhani Medium for labels, Rajdhani SemiBold for buttons and IBM Plex Mono Medium for readouts. Subsetting cut them from 988 KB to 79 KB of embedded data (`ui/assets/fonts/README.md`).

Some of those tokens are short words at namespace scope in `rhino::forge::ui` (`ui/ForgeTheme.h`): `text`, `panel` and `line`. Never name a local or parameter `text`, `panel` or `line` in Forge UI code: under `/W4` it raises C4459, once per translation unit that includes the header ([Build and test Forge](build-and-test-forge.md#warnings)).

## Related

- [App shell and control bar](app-shell.md)
- [Track and clip colours](track-and-clip-colours.md)
- [Forge editor (panel)](forge-editor.md)
- [Forge themes and Merli](forge-themes.md)
