---
title: Project files (.rhinoedit)
type: component
summary: A .rhinoedit is the Tracktion edit's XML plus Rhino's own properties, saved from a snapshot on a worker and opened off-thread.
tags: [rhino, persistence, files, debugging]
sources: []
updated: 2026-10-03
---

# Project files (.rhinoedit)

A `.rhinoedit` is the Tracktion edit's `ValueTree` written as XML. Rhino's own state rides on the same tree as `rhino*` properties and children of the edit, its tracks and its clips (track kind, arming, inputs, groups, automation, clip colour, warp, count-in), so none of it has a save path of its own. `native/src/ProjectFiles.cpp` does the file work; `Session::projectSnapshot`, `restoreProject` and `projectSaved` in `Session.cpp` are the model side.

## Saving

`projectSnapshot` flushes the edit and copies its state on the message thread, stamping `rhinoSnapshotRevision`. A worker writes the XML to a `juce::TemporaryFile` beside the target and swaps it in only on success, so a failed save keeps the previous file. `projectSaved` marks the document clean only if no command ran since the snapshot: edits made during a save stay dirty.

## Opening

The XML is parsed on the worker; `restoreProject` runs on the message thread with the window disabled. It refuses anything that is not an `EDIT` with `rhinoFormatVersion` = 1, and requires the first non-bus track to hold a MIDI clip and a Utility device ("The project is missing its pattern track devices."), all before touching the open document. Then it swaps the edit, re-applies `rhinoPatternInstrument`, runs `collapseStackedInstruments`, `ensureSceneSlots`, `ensureTrackMixers`, `migrateLegacyTrackGroups` and `reconcileTrackGroups`, and clears undo. Those migrations are what [No backward compatibility for .rhinoedit](no-rhinoedit-back-compat.md) targets.

**Trap, traced in the code but not reproduced (2026-10-03):** nothing keeps a MIDI clip on the first track when the stack is reordered, so moving an audio track to the top and saving looks like it writes a file this check refuses. The first track is special in other ways too: [The first track is still the pattern track](pattern-track.md).

## Media

Imported audio is referenced at its original path and never collected. Takes are written to `<project> Recordings` beside a saved project and merged clips to `<project> Audio`; an untitled project uses `%APPDATA%\Rhino\Recordings` and `%APPDATA%\Rhino\Merged audio`.

## Debugging hooks

- `RhinoDAW.exe <file>.rhinoedit` opens that project at startup (`Application::initialise`): the ten-second headless repro of [Debugging a crash only one project triggers](debugging-a-crashing-project.md). Normal launches also register the `.rhinoedit` association under `HKEY_CURRENT_USER\Software\Classes`.
- `%APPDATA%\Rhino\rhino.log` brackets each open with `Rhino: Opening <name>...` and `Rhino: Opened <name>` (or the refusal).

## WAV export

*Export WAV* renders the whole edit on a worker at the audio device's own rate and block size (falling back to 48 kHz and 512), 24-bit, dithered, through the master plugins and with no normalising, so the file matches what the main fader set. `te::Edit::ScopedRenderStatus` detaches the edit from the device meanwhile, and automation lanes are mirrored into engine curves first ([Track automation](automation.md)). `README.md` still says 48 kHz, peak-normalised to -1 dBFS; commit `d3ead2a` (2026-09-15) changed both.

## Related

- [Session, the model](session-model.md)
- [No backward compatibility for .rhinoedit](no-rhinoedit-back-compat.md)
- [Debugging a crash only one project triggers](debugging-a-crashing-project.md)
- [Recording and the count-in](recording.md)
