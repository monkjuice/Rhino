---
title: Forge's Processor includes nothing under ui/
type: decision
summary: Since 2026-10-25 src/ForgeProcessor.h includes no header under ui/; the theme choice is free functions in src/ForgeThemeSettings.h that the Editor runs as its first member, and the panel colour constants the state reads live in core/ForgePanelColours.h, so the Processor neither draws nor knows a theme exists.
tags: [forge, architecture, theme, dependencies]
sources: []
updated: 2026-10-25
---

# Forge's Processor includes nothing under ui/

Decided on 2026-10-25 by session rhino-62, in the same change that made the preset browser fast ([Forge preset browser and bank](forge-preset-browser.md)). Uncommitted at the time of writing. Paths are under `instruments/rhino-forge/`.

## Context

`src/ForgeProcessor.h` included headers under `ui/`: the Processor carried `loadThemeOnce` and the theme's read and save, and `src/ForgeProcessorState.cpp` took the panel colour count and default from `ui/ForgeStyle.h`'s `PanelColour` list. So the audio processor's header depended on the panel's drawing vocabulary for two things it never draws.

## The choice

- **The theme is the panel's own setting.** `src/ForgeThemeSettings.h` declares free functions in `rhino::forge::themeSettings`: `read()`, `save(ThemeId)`, `loadOnce()` and `forgetLoadedForTests()`. They are defined in `src/ForgeProcessorSettings.cpp` beside the favourites, because both read and write the file `Processor::settingsFile()` names. `Processor::createEditor` no longer loads the theme; `Editor` (`src/ForgeEditor.h`) has a first data member, `themeInForce = themeSettings::loadOnce()`, declared right after `processor` and before `lookAndFeel` and every control, so the tokens are in force before any control copies one ([Forge themes and Merli](forge-themes.md)). The header's own comment gives the reason: the Processor neither draws nor needs to know a theme exists, which is why these are free functions rather than Processor methods.
- **A constant the state and the panel both read lives in `core/`.** `core/ForgePanelColours.h` holds `panelColourCount`, `defaultPanelColourIndex` and `clampPanelColour`. `ui/ForgeStyle.h` takes them from there, `static_assert`s the count against its `PanelColour` list and re-exports it with a using-declaration, so `ui::panelColourCount` still names it; `src/ForgeProcessorState.cpp` includes the core header instead of `ui/ForgeStyle.h`.

Both new headers are listed in `CMakeLists.txt` ([A new source file needs an explicit CMake configure](cmake-does-not-reconfigure.md)).

## Consequences

- A setting the panel needs goes in a free-function header like `ForgeThemeSettings.h`, defined where the settings file is read, not on the Processor.
- A constant both sides read goes in `core/`, which the Processor already includes; `ui/` may re-export it, the Processor may not reach into `ui/` for it.
- This is Forge's form of the rule Rhino holds with a lint, that knowledge flows one way ([Dependency direction](dependency-direction.md)); Forge has no lint for it yet, so an include of `ui/` in `src/ForgeProcessor*.h` is caught only by reading.

## Related

- [Rhino Forge](forge.md)
- [Forge editor (panel)](forge-editor.md)
- [Forge themes and Merli](forge-themes.md)
- [Forge presets and state](forge-presets-and-state.md)
- [Dependency direction](dependency-direction.md)
