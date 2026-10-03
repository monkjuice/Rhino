---
title: Browser and library preview
type: component
summary: A five-section library rail whose rows leave only by drag, with samples auditioned through a second audio callback.
tags: [rhino, ui, browser, library]
sources: []
updated: 2026-10-03
---

# Browser and library preview

`native/src/BrowserPanel.*` is a rail of five sections (Instruments, Patterns, Samples, Audio FX, MIDI FX) over a folder tree, the shape Live uses. Its rows come from three places: pattern presets, drum kits and the two generated samples (Whistle, Siren) listed in `BrowserPanel.cpp`; every file `ContentLibrary::samples()` finds under `library/` ([Content library](content-library.md)); and every browsable entry of `DeviceCatalog::all()`, so a new device appears with no browser edit ([Device catalog](device-catalog.md)). `--self-test` checks both: each browsable device has a row, and the sample rows match the library scan.

Search looks across every section, groups the hits as *Section / Folder* and opens those folders.

## Rows leave only by drag

There is no click-to-add and no double-click action, so browsing and auditioning can never land a device on whatever track happens to be selected. A row's drag description is `rhino-browser:<kind>:<id>` (kinds `preset`, `instrument`, `midi-effect`, `effect`, `file`, `sample`, `drumkit`), parsed by `BrowserIds.h` for every drop target: the [arrangement](arrangement-view.md), the [device rack](device-rack.md) and the paused session view. It exists because the arrangement and rack kept their own id tables, which drifted until the rack silently ignored a Rhino Wave drop the arrangement accepted. The id is read from the front, because a `file` payload is a Windows path with its own colon.

A drop below the last lane makes a lane of the kind the item needs, since a track cannot change kind later ([Track kinds: audio and MIDI](track-kinds.md)).

`HANDOVER.md` still says browser double-clicks act on the selected track through `BrowserPanel::targetTrack`; no such path exists (checked 2026-10-03).

## Preview

Clicking a sample plays it once. The audition is a second `juce::AudioIODeviceCallback` on the engine's own `AudioDeviceManager` (`SessionPreview.cpp`), not anything inside the edit: JUCE sums its callbacks, so it mixes with the transport while choosing no track, writing no clip and opening no undo transaction.

- It is built on first use, so a session that never previews starts no thread and registers no callback, which leaves such test runs and offline renders untouched.
- The file is read ahead on a `TimeSliceThread`: the audio callback must never wait on a disk seek.
- It follows the click (`itemClicked`), not the selection, because the tree is rebuilt on every search and resize and restoring the selection would replay the sound.
- `releasePreview` runs from both `releaseAudioDevice` and `~Session`, since either can come first and the callback must leave the device manager while it and its transport are alive.
- **Edit > Preview library sounds** is stored as `browserPreview` in Rhino's settings file under `%APPDATA%\Rhino`; under `isCommandLineTestMode` it is neither read nor written, so a developer's setting cannot decide whether the suite passes.

## Related

- [Content library](content-library.md)
- [Device catalog](device-catalog.md)
- [Arrangement view](arrangement-view.md)
- [Real-time audio rules](real-time-audio-rules.md)
- [Recording and the count-in](recording.md)
- [Pattern presets](pattern-presets.md)
