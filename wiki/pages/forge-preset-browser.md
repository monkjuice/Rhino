---
title: Forge preset browser and bank
type: component
summary: LOAD opens a browser over Forge's modules that lists the player's own bank in Documents/Rhino Forge/Presets, filled with the factory set on first use, beside the machine's Serum 2 and Vital libraries, read only, all listed from file heads alone on a lister thread that shows a folder's last listing while it re-reads only the files that changed.
tags: [forge, presets, ui, browser, performance]
sources: []
updated: 2026-10-25
---

# Forge preset browser and bank

Built on 2026-10-18/19. Paths are under `instruments/rhino-forge/`.

## The bank

`src/ForgePresetBank.*` holds the bank. It knows nothing about the panel and never runs on the audio thread.

- The bank is one folder tree, `Documents/Rhino Forge/Presets` (`presetBank::root()`, redirected by `setRootForTests`). The player arranges it freely with `createFolder`, `rename`, `moveInto` and `remove`. `remove` sends to the recycle bin. Each operation refuses a taken name rather than overwrite.
- `presetBank::ensure()` makes the bank the first time and copies the factory set from `presets/Factory` into `Factory/`, subfolders included: `restoreFactory` finds the files recursively and keeps each one's path relative to the source, so `presets/Factory/Drum n Bass/` lists as a folder under Factory in the tree with no browser change (checked 2026-10-23 with `--snapshot ... PRESETS`; [Writing Forge factory presets](writing-forge-factory-presets.md)). *Restore factory presets* (`restoreFactory`) copies back only the missing ones. The source is found upward from the binary by `sourceFolderAbove` (`core/ForgeTableFiles.h`), as `tables/` is, so a build with no source tree has no factory set. The set is copied, not read where it ships, so the player can rename, move and delete it like their own presets ([Forge presets and state](forge-presets-and-state.md)).
- `libraries()` lists the bank first. Then, where they exist, it lists `Documents/Xfer/Serum 2 Presets/Presets` and `Documents/Vital`, both read only. A preset from those libraries is converted when it is opened and never written back ([Forge preset import](forge-preset-import.md)).
- The styles are Vital's nine (`presetStyles()`). `canonicalPresetStyle` maps other synths' words onto them ("Arp" becomes Sequence, "Pluck" becomes Keys) and drops words it does not know. A Serum preset's style comes from its name prefix (BA, LD, PD, PL...) or from its folder's name.
- Favourites belong to the machine. They are stored as `<Favourites><Preset path/>` in `%APPDATA%\Rhino Forge\Settings.xml`, beside the theme (`Processor::readFavouritePresets`, `saveFavouritePresets`), so a Serum or Vital preset can be a favourite without Forge writing into another synth's folder. The browser rewrites the paths when a file moves.

## Listing reads only file heads

`readPresetEntry` never opens a whole preset:

- For a `.forgepreset` it reads the wrapper's attributes with `XmlDocument::getDocumentElement(true)`. A table or a sample is about 100 KB of base64 under the wrapper.
- For a `.SerumPreset` it reads the JSON header (`readSerumPresetHeader`) and decompresses nothing.
- For a `.vital` it reads the top-level keys up to `"settings"` with `JsonHeadScanner`, a small JSON skipper that builds no strings for the values it skips.

The skipper cut the listing of 75 Vital presets from 718 ms to 13 ms. 626 Serum 2 presets list in about 100 ms. Keep any new format's listing to the file's head.

Those are warm figures. With a cold file cache the same walk read 5.1 s for the 626 Serum presets, 660 ms for Vital's 75 and 114 ms for the bank's 30 (measured 2026-10-25 by `--profile-browser`, below), which is why the browser now lists on a thread. Since commit `ad945e1` (2026-10-25) a listing also reuses what the last one read: `PresetEntry` carries the file's `size`, and `presetsIn(folder, recursive, previous = nullptr, shouldStop = {})` takes the previous entries. The walk always happens, so a file added, changed or deleted outside Forge is noticed; a file whose modification time and size match its previous entry is reused without being opened; and `shouldStop` is asked between files so a lister can give up. `listingSuite` (`tests/ForgeTestsBrowser.cpp`) doctors a previous entry to prove an unchanged file is not reopened, pushes a rewritten file's date forward to prove it is re-read, adds and deletes a file, and checks that `shouldStop` lists nothing.

## The browser

`src/ForgePresetBrowser.cpp` holds what the browser lists and does, and `ForgePresetBrowserViews.cpp` holds its drawing and gestures. They share the private `ForgePresetBrowserInternal.h`. The panel's side is `src/ForgeEditorPresets.cpp`.

- **Opening it.** LOAD toggles the browser and stays lit while it is open. SAVE opens it on the save form in the info card (name, author, style, notes). A save goes into the chosen bank folder, or failing that the loaded preset's folder if it is in the bank, or failing that the bank's root (`saveFolder`). A name already taken needs a second press, and the button reads REPLACE.
- **Where it sits.** It covers `ui::contentBounds`, the modules, so the keyboard still plays while browsing. Tabs close it. It is opaque and buffered to an image, because the panel under it ticks at 24 Hz ([Forge's tick repaints its live regions at 24 Hz](forge-panel-repaint-cost.md)). Since commit `ad945e1` (2026-10-25) its own chrome is kept too: `paintChrome` (`ForgePresetBrowserViews.cpp`) draws the plate, the top bar's well and title, the FOLDERS title and the save form's well and heading once into `chrome`, keyed on size, raster scale and whether the save form is up, and `PresetBrowser::paint` blits it and draws live only the loaded preset's name and the save form's INTO line. Before, the browser repainted whole at 19.6 ms a frame and 23.4 ms after a hover in the list.
- **Left column.** An info card shows the name, byline, origin and notes. A converted preset's info card also lists, in violet, what was not converted. Below it come a search field (Ctrl+F), the nine style chips and the folder list: ALL PRESETS, FAVOURITES, the bank's tree, then SERUM 2 and VITAL, folded.
- **Right side.** A table that sorts by star, name (tagged SERUM or VITAL), style, author or date.
- **Moving through it.** A click loads a preset. A double-click loads it and closes the browser. The arrow keys and `<`/`>` walk the filtered list and wrap at its ends (`step`). Esc closes the browser.
- **Menus and drags.** Presets and folders have right-click menus, and a rename happens in place. A preset dragged onto a bank folder moves there. A Serum or Vital preset dragged there is converted in by `Processor::convertPreset`, which leaves the playing patch alone.
- **File drops.** The editor is a `FileDragAndDropTarget`, so a dropped `.forgepreset`, `.SerumPreset` or `.vital` file loads.
- The favourite star is a drawn path, because Forge's fonts are Latin subsets with no star glyph.

## Listing on a thread

Since commit `ad945e1` (2026-10-25) the browser never reads a folder on the message thread.

- `scanned` maps a folder's path to a `Scan` of `entries` and `fresh`. `openToBrowse` marks every scan stale instead of clearing it; only a file operation the browser makes itself clears them.
- `relist` shows a stale folder's last entries at once and hands the folder, with those entries as `previous`, to `PresetBrowser::Lister`, a nested `juce::Thread` with one pending job, a newer request replacing one not yet started. It is declared as the browser's last member, so it is destroyed first, and its destructor calls `stopThread(5000)`.
- A result comes back through a mailbox, not `MessageManager::callAsync`: `PresetBrowser` privately inherits `juce::AsyncUpdater`, the lister pushes an `Arrival` of `request`, `key` and `entries` onto `PresetBrowser::arrivals` under `arrivalsLock` (a `juce::CriticalSection`) and calls `triggerAsyncUpdate()`, and `handleAsyncUpdate` swaps the vector out under the lock and calls `listArrived(request, key, entries)` for each. `listArrived` always caches the entries but shows them only while `listRequest` still matches: a listing that lands after the folder was changed again is kept, not shown. Nothing on the lister's thread touches the browser except the mailbox and `triggerAsyncUpdate`.
- Why a mailbox: this JUCE build has `JUCE_MODAL_LOOPS_PERMITTED` off, so there is no `runDispatchLoopUntil` for a test to pump. The public `PresetBrowser::takeListings()` calls `handleUpdateNowIfNeeded()`, which delivers whatever has arrived with no message loop at all; in the running app the message loop delivers it the same way, through the `AsyncUpdater`.
- Teardown: `lister` is the browser's last member, so its `stopThread(5000)` runs before the mailbox or the `AsyncUpdater` base is destroyed, and `presetsIn`'s `shouldStop` ends a walk within milliseconds of `stopThread`.
- The public `bool listing` says a read is in flight. The table prints "READING THE FOLDER" only when it also has nothing to show.
- A hover repaints two rows, not the list: `FolderList::setHover` and `PresetTable::setHover` repaint the row left and the row entered, and both paints skip rows outside `g.getClipBounds()`.

Anything that reads `listed` or `shown` in a test must first call `waitForListing(browser)` (`tests/ForgeTestsBrowser.cpp`), which sleeps 2 ms and calls `takeListings()` until `listing` clears; `--snapshot ... PRESETS[:LIBRARY]` and `--profile-browser` do the same through `settleListing` (`tests/ForgeTestTools.cpp`).

## What it costs

`RhinoForgeTests --profile-browser [width height]` (`tests/ForgeTestTools.cpp`, dispatched in `tests/ForgeTestMain.cpp`, since 2026-10-25) reads the real bank in Documents and the machine's Serum 2 and Vital folders, writes nothing, and prints: each library walked and listed cold, then warm; every factory preset loaded through the call a click in the list makes, median and max, with the editor open so its attachments and tick are paid for; five Serum 2 and five Vital presets loaded, converted on the way in; the browser opened the first time and again, and each library chosen; then the browser's frames, nothing moved and after a hover in the list ([Build and test Forge](build-and-test-forge.md#test)).

Before any change, 2026-10-25, software-rendered at 1440x900: a factory preset loaded in 98 ms median, 114 ms max; a Serum 2 preset in 138 ms; a Vital preset in 380 ms; the browser repainted whole in 19.6 ms and a frame after a hover in the list took 23.4 ms; the 626 Serum presets listed in 104 ms warm and 5.1 s cold. The load's 98 ms was three nearly equal parts, each fixed on the Processor's side: the oscillators' built-in tables rebuilt, `migrated` walking the tree once per parameter, and `replaceState` laying the rack out once per moved FX slot ([Forge presets and state](forge-presets-and-state.md#one-way-a-patch-arrives)). After the change, same tool, same machine, same day: a factory preset loads in 7.5 ms median, 17.3 ms max over the 29 presets; a Serum 2 preset in 71 ms, the conversion itself being what remains; a Vital preset in 294 ms, where the Vital converter and its wavetables dominate and were not touched. The browser opens the first time in 20.5 ms and again in 15.9 ms (from 23.4 and 18.0); opening on SERUM 2's 626 presets takes 31 ms warm (from 115) and is off the message thread when cold; on VITAL 20 ms (from 32). A frame with the browser open and nothing moved is 11.0 ms (from 15.2), a frame after a hover in the list 14.0 ms (from 23.4; in the running panel a hover now repaints two rows, not the browser), and the browser painted whole 8.9 ms (from 19.6). The plain `presetsIn` with no previous listing is unchanged by design, 626 Serum heads still taking about 115 ms warm: the saving is in the browser's path, which hands the previous listing along.

## Tests

Area `browser` (`tests/ForgeTestsBrowser.cpp`, CTest `forge_browser`) covers the bank operations, the kept scan (`listingSuite`), the metadata round trip, host state and favourites. On a headless editor it checks LOAD, SAVE, the plate, stepping and REPLACE, each after `waitForListing`. `--snapshot` renders the browser with the pages `PRESETS`, `SAVE` and `PRESETS:<LIBRARY>`, waiting for the listing first ([Seeing the UI without taking the screen](headless-ui-snapshots.md)).

## Related

- [Forge presets and state](forge-presets-and-state.md)
- [Writing Forge factory presets](writing-forge-factory-presets.md)
- [Forge preset import (Serum 2 and Vital)](forge-preset-import.md)
- [Forge editor (panel)](forge-editor.md)
- [Forge themes and Merli](forge-themes.md)
- [Rhino Forge](forge.md)
