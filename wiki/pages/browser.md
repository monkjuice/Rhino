---
title: Browser and library preview
type: component
summary: A six-section library rail, Drums first, whose rows leave only by drag, with samples and drum presets auditioned through a second audio callback.
tags: [rhino, ui, browser, library]
sources: []
updated: 2026-10-06
---

# Browser and library preview

`native/src/BrowserPanel.*` is a rail of six sections (Drums, Instruments, Patterns, Samples, Audio FX, MIDI FX) over a folder tree, the shape Live uses. Its rows come from four places:
- pattern presets and the two generated samples (Whistle, Siren), listed in `BrowserPanel.cpp`;
- every file `ContentLibrary::samples()` finds under `library/` ([Content library](content-library.md));
- the kits and drum presets on disk;
- every browsable entry of `DeviceCatalog::all()`, so a new device appears with no browser edit ([Device catalog](device-catalog.md)).

`--self-test` checks that each browsable device has a row, and that the Samples section's rows match the library scan. It counts Samples rows only, because Drums lists the same files again.

Drums comes first, as in Live, because it is the one section that holds a device, kits, presets and samples for one job ([Drum Rack](drum-rack.md)). `addDrumItems` puts the blank Drum Rack at the top, then a Kits folder, then a folder per drum kind (`ContentLibrary::drumTypes()`). Each kind's folder lists its drum presets before its samples, the same files Samples lists by pack. A search shows each file once, under Samples. The rack is also listed under Instruments / Drum Rack as a catalog row. `refreshPresets()` rebuilds the Drums rows as well, so a kit or drum preset just saved appears. `--self-test` checks this order and every kit's and preset's drag payload.

Search looks across every section, groups the hits as *Section / Folder* and opens those folders.

A device's row holds its presets, read from disk by `ContentLibrary::presets()` when the panel is built and again on `refreshPresets()`, which the shell calls after a preset is saved. Dragging the device adds it at its defaults; dragging a preset adds it already set, and a search that finds a preset brings out its device's row, open ([Device presets (.rnd)](device-presets.md)). The rows' order and drag payloads are tested; how the open tree looks is not, since a test cannot run the message loop the tree lays its rows out on.

## Rows leave only by drag

There is no click-to-add and no double-click action, so browsing and auditioning can never land a device on whatever track happens to be selected. A row's drag description is `rhino-browser:<kind>:<id>` (kinds `preset`, `instrument`, `midi-effect`, `effect`, `device-preset`, `file`, `sample`, `drumkit`, `drum-preset`), parsed by `BrowserIds.h` for every drop target: the [arrangement](arrangement-view.md), the [device rack](device-rack.md) and the paused session view. It exists because the arrangement and rack kept their own id tables, which drifted until the rack silently ignored a Rhino Wave drop the arrangement accepted. The id is read from the front, because a `file` payload is a Windows path with its own colon. `drumkit` carries the `.rdk` file's path (until 2026-10-06, a kit enum's id) and `drum-preset` the `.rdp` file's, read by `browserDropKitFile` and `browserDropDrumPresetFile`.

A drop below the last lane makes a lane of the kind the item needs, since a track cannot change kind later ([Track kinds: audio and MIDI](track-kinds.md)).

## Preview

Clicking a sample plays it once. The audition is a second `juce::AudioIODeviceCallback` on the engine's own `AudioDeviceManager` (`SessionPreview.cpp`), not anything inside the edit: JUCE sums its callbacks, so it mixes with the transport while choosing no track, writing no clip and opening no undo transaction.

- It is built on first use, so a session that never previews starts no thread and registers no callback, which leaves such test runs and offline renders untouched.
- The file is read ahead on a `TimeSliceThread`: the audio callback must never wait on a disk seek.
- It follows the click (`itemClicked`), not the selection, because the tree is rebuilt on every search and resize and restoring the selection would replay the sound.
- `releasePreview` runs from both `releaseAudioDevice` and `~Session`, since either can come first and the callback must leave the device manager while it and its transport are alive.
- **Edit > Preview library sounds** is stored as `browserPreview` in Rhino's settings file under `%APPDATA%\Rhino`; under `isCommandLineTestMode` it is neither read nor written, so a developer's setting cannot decide whether the suite passes.

Clicking a drum preset plays it as a pad would, with its Tune, Decay and Tone, rather than playing the bare file. `Session::previewDrumSound` renders one strike into a `juce::MemoryAudioSource`, and `startPreview` plays any `PositionableAudioSource` through the same transport.

## Related

- [Content library](content-library.md)
- [Device catalog](device-catalog.md)
- [Arrangement view](arrangement-view.md)
- [Real-time audio rules](real-time-audio-rules.md)
- [Recording and the count-in](recording.md)
- [Pattern presets](pattern-presets.md)
- [Device presets (.rnd)](device-presets.md)
- [Drum Rack](drum-rack.md)
