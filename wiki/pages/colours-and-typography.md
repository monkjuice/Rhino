---
title: Colours and typography
type: convention
summary: Rhino names chrome colours in Theme.h's palette and draws text in embedded Inter; Forge has its own metal look, accent colours and four faces.
tags: [both, ui, theme, fonts]
sources: []
updated: 2026-10-03
---

# Colours and typography

## Rhino: colours are named, never spelt

`palette` in `native/src/Theme.h` is where a chrome colour gets its name, and code uses the token rather than a hex literal (`AGENTS.md`; `native/README.md`, *The shell's chrome*). The tokens, as of 2026-10-03:

- **Surfaces and text:** `appBackground`, `globalBar`, `sideSurface`, `trackCard`, `control`, `hover`, `border`, `text`, `textDim`, `disabled`, `activeNeutral`, `displayInset`, `recordAccent`.
- **The timeline:** `arrangement`, `automationLane`, `minorGrid`, `beatGrid`, `barGrid`. The lanes are light and ruled dark, which keeps the grid's contrast under room light.
- **Meaning:** `displayText`, `displayTextDim` and `displayTextFaint` for the readout, plus `volume`, `pan` and `selection`.

`Theme` feeds the tokens to JUCE's stock colour ids, so menus, dialogs and scrollbars match the panels unaided.

The chrome is neutral on purpose. Only what carries meaning is saturated: a track's own colour, volume cyan, pan red and the record light. Selection is grey everywhere, so a selected card never competes with the track colour beside it. Content (waveforms, browser row markers, device accents) keeps its own colour ([Track and clip colours](track-and-clip-colours.md)).

**Not applied everywhere yet** (checked 2026-10-03). Files that predate the palette (2026-09-30) or escaped it still spell colours as `juce::Colour(0xff...)` literals: the rename editor in `ArrangementRename.cpp`, the loading screen in `StartupScreen.h`, `AudioClipPanel.cpp`, `AudioClipWarp.cpp`, and the note editor, device rack and device faces (`StepGridPainter.cpp`, `DeviceRack.cpp`, `DeviceEditorPanel*.cpp`). New code names a `palette` token.

## Rhino: type and icons

- Inter Regular and SemiBold are embedded (`native/assets/fonts/`, under the SIL OFL) and served by `Theme::getTypefaceForFont`. A face drawn for the screen is bundled because Direct2D's greyscale antialiasing without grid fitting leaves the Windows shell font soft.
- `uiFont` asks for whole-pixel em sizes. A JUCE font height includes ascent and descent, so a height of 12 rasterises Inter at 9.9 px per em, off the pixel grid.
- The cut is chosen from the style string, not from `Font::isBold()`, which answers false for SemiBold ([JUCE's isBold() is false for SemiBold](juce-isbold-misses-semibold.md)).
- Toolbar icons are paths in `ControlBarIcons.*`, not characters. Inter's Latin cuts lack the transport symbols, and a fallback glyph is a different face on every machine.

## Forge: its own look

Forge shares none of this. Its panel is a dark metal chassis (`ui/ForgePanels.h`), with colours named in `ui/ForgeStyle.h` and `ui/ForgeChrome.h`. The signal path is electric blue and the modulators are violet. Each oscillator wears a plate colour picked from its LED (red, orange, green by default, or blue), and each FX type has its own colour (`fxTypeColour`). Text uses four subset OFL faces, one per job: Chakra Petch SemiBold for headers and tabs, Rajdhani Medium for labels, Rajdhani SemiBold for buttons and IBM Plex Mono Medium for readouts. Subsetting cut them from 988 KB to 79 KB of embedded data (`ui/assets/fonts/README.md`).

## Related

- [App shell and control bar](app-shell.md)
- [Track and clip colours](track-and-clip-colours.md)
- [Forge editor (panel)](forge-editor.md)
