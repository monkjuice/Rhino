---
title: Forge preset browser and bank
type: component
summary: LOAD opens a browser over Forge's modules that lists the player's own bank in Documents/Rhino Forge/Presets, filled with the factory set on first use, beside the machine's Serum 2 and Vital libraries, read only, all listed from file heads alone.
tags: [forge, presets, ui, browser]
sources: []
updated: 2026-10-19
---

# Forge preset browser and bank

Built on 2026-10-18/19. Paths are under `instruments/rhino-forge/`.

## The bank

`src/ForgePresetBank.*` holds the bank. It knows nothing about the panel and never runs on the audio thread.

- The bank is one folder tree, `Documents/Rhino Forge/Presets` (`presetBank::root()`, redirected by `setRootForTests`). The player arranges it freely with `createFolder`, `rename`, `moveInto` and `remove`. `remove` sends to the recycle bin. Each operation refuses a taken name rather than overwrite.
- `presetBank::ensure()` makes the bank the first time and copies the factory set from `presets/Factory` into `Factory/`. *Restore factory presets* (`restoreFactory`) copies back only the missing ones. The source is found upward from the binary by `sourceFolderAbove` (`core/ForgeTableFiles.h`), as `tables/` is, so a build with no source tree has no factory set. The set is copied, not read where it ships, so the player can rename, move and delete it like their own presets ([Forge presets and state](forge-presets-and-state.md)).
- `libraries()` lists the bank first. Then, where they exist, it lists `Documents/Xfer/Serum 2 Presets/Presets` and `Documents/Vital`, both read only. A preset from those libraries is converted when it is opened and never written back ([Forge preset import](forge-preset-import.md)).
- The styles are Vital's nine (`presetStyles()`). `canonicalPresetStyle` maps other synths' words onto them ("Arp" becomes Sequence, "Pluck" becomes Keys) and drops words it does not know. A Serum preset's style comes from its name prefix (BA, LD, PD, PL...) or from its folder's name.
- Favourites belong to the machine. They are stored as `<Favourites><Preset path/>` in `%APPDATA%\Rhino Forge\Settings.xml`, beside the theme (`Processor::readFavouritePresets`, `saveFavouritePresets`), so a Serum or Vital preset can be a favourite without Forge writing into another synth's folder. The browser rewrites the paths when a file moves.

## Listing reads only file heads

`readPresetEntry` never opens a whole preset:

- For a `.forgepreset` it reads the wrapper's attributes with `XmlDocument::getDocumentElement(true)`. A table or a sample is about 100 KB of base64 under the wrapper.
- For a `.SerumPreset` it reads the JSON header (`readSerumPresetHeader`) and decompresses nothing.
- For a `.vital` it reads the top-level keys up to `"settings"` with `JsonHeadScanner`, a small JSON skipper that builds no strings for the values it skips.

The skipper cut the listing of 75 Vital presets from 718 ms to 13 ms. 626 Serum 2 presets list in about 100 ms. Keep any new format's listing to the file's head.

## The browser

`src/ForgePresetBrowser.cpp` holds what the browser lists and does, and `ForgePresetBrowserViews.cpp` holds its drawing and gestures. They share the private `ForgePresetBrowserInternal.h`. The panel's side is `src/ForgeEditorPresets.cpp`.

- **Opening it.** LOAD toggles the browser and stays lit while it is open. SAVE opens it on the save form in the info card (name, author, style, notes). A save goes into the chosen bank folder, or failing that the loaded preset's folder if it is in the bank, or failing that the bank's root (`saveFolder`). A name already taken needs a second press, and the button reads REPLACE.
- **Where it sits.** It covers `ui::contentBounds`, the modules, so the keyboard still plays while browsing. Tabs close it. It is opaque and buffered to an image, because the panel under it repaints at 24 Hz ([Forge's panel repaints whole at 24 Hz](forge-panel-repaint-cost.md)).
- **Left column.** An info card shows the name, byline, origin and notes. A converted preset's info card also lists, in violet, what was not converted. Below it come a search field (Ctrl+F), the nine style chips and the folder list: ALL PRESETS, FAVOURITES, the bank's tree, then SERUM 2 and VITAL, folded.
- **Right side.** A table that sorts by star, name (tagged SERUM or VITAL), style, author or date.
- **Moving through it.** A click loads a preset. A double-click loads it and closes the browser. The arrow keys and `<`/`>` walk the filtered list and wrap at its ends (`step`). Esc closes the browser.
- **Menus and drags.** Presets and folders have right-click menus, and a rename happens in place. A preset dragged onto a bank folder moves there. A Serum or Vital preset dragged there is converted in by `Processor::convertPreset`, which leaves the playing patch alone.
- **File drops.** The editor is a `FileDragAndDropTarget`, so a dropped `.forgepreset`, `.SerumPreset` or `.vital` file loads.
- The favourite star is a drawn path, because Forge's fonts are Latin subsets with no star glyph.

## Tests

Area `browser` (`tests/ForgeTestsBrowser.cpp`, CTest `forge_browser`) covers the bank operations, the metadata round trip, host state and favourites. On a headless editor it checks LOAD, SAVE, the plate, stepping and REPLACE. `--snapshot` renders the browser with the pages `PRESETS`, `SAVE` and `PRESETS:<LIBRARY>` ([Seeing the UI without taking the screen](headless-ui-snapshots.md)).

## Related

- [Forge presets and state](forge-presets-and-state.md)
- [Forge preset import (Serum 2 and Vital)](forge-preset-import.md)
- [Forge editor (panel)](forge-editor.md)
- [Forge themes and Merli](forge-themes.md)
- [Rhino Forge](forge.md)
