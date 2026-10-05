---
title: Project files (.rhinoedit)
type: component
summary: A .rhinoedit is the Tracktion edit's XML plus Rhino's own properties, saved from a snapshot on a worker and opened off-thread.
tags: [rhino, persistence, files, debugging]
sources: []
updated: 2026-10-05
---

# Project files (.rhinoedit)

A `.rhinoedit` is the Tracktion edit's `ValueTree` written as XML. Rhino's own state rides on the same tree as `rhino*` properties and children of the edit, its tracks and its clips (track kind, arming, inputs, groups, automation, clip colour, warp, count-in), so none of it has a save path of its own. `native/src/ProjectFiles.cpp` does the file work; `Session::projectSnapshot`, `restoreProject` and `projectSaved` in `Session.cpp` are the model side.

## Saving

`projectSnapshot` flushes the edit and copies its state on the message thread, stamping `rhinoSnapshotRevision`. A worker writes the XML to a `juce::TemporaryFile` beside the target and swaps it in only on success, so a failed save keeps the previous file. `projectSaved` marks the document clean only if no command ran since the snapshot: edits made during a save stay dirty.

## Opening

The XML is parsed on the worker; `restoreProject` runs on the message thread with the window disabled. It refuses anything that is not an `EDIT` with `rhinoFormatVersion` = 1, and a document with no tracks, before touching the open document. It runs `collapseStackedInstruments` on the candidate, then swaps the edit. It drops what belonged to the outgoing document: the automation mirror's curves (before the old edit goes, since they hold its plugins), the automation overrides, which are keyed by track index, and the dragged loop span. Then it re-finds the note editor's clip (`repairPatternClip`), runs `ensureSceneSlots`, `ensureTrackMixers`, `migrateLegacyTrackGroups` and `reconcileTrackGroups`, and clears undo. It decides nothing about which track is first or what any track runs; the checks and the `rhinoPatternInstrument` re-application that did were removed in commit `a04b407` ([No track is special for being first](pattern-track.md)). The migrations are what [No backward compatibility for .rhinoedit](no-rhinoedit-back-compat.md) targets.

## Media

Imported audio is referenced at its original path and never collected. Takes are written to `<project> Recordings` beside a saved project and merged clips to `<project> Audio`; an untitled project uses `%APPDATA%\Rhino\Recordings` and `%APPDATA%\Rhino\Merged audio`.

## Debugging hooks

- `RhinoDAW.exe <file>.rhinoedit` opens that project at startup (`Application::initialise`): the ten-second headless repro of [Debugging a crash only one project triggers](debugging-a-crashing-project.md). Normal launches also register the `.rhinoedit` association under `HKEY_CURRENT_USER\Software\Classes`.
- `%APPDATA%\Rhino\rhino.log` brackets each open with `Rhino: Opening <name>...` and `Rhino: Opened <name>` (or the refusal).

## WAV export

*Export WAV* renders the whole edit on a worker at the audio device's own rate and block size (falling back to 48 kHz and 512), 24-bit, dithered, through the master plugins and with no normalising, so the file matches what the main fader set. `te::Edit::ScopedRenderStatus` detaches the edit from the device meanwhile. The render reads automation from the parameters' engine curves, exactly as playback does, so the export only calls `Session::mirrorAutomationToEngine` first to make them current ([Track automation](automation.md)). Commit `d3ead2a` (2026-09-15) changed export from fixed 48 kHz and peak normalisation to this behaviour.

## Related

- [Session, the model](session-model.md)
- [No backward compatibility for .rhinoedit](no-rhinoedit-back-compat.md)
- [Debugging a crash only one project triggers](debugging-a-crashing-project.md)
- [Recording and the count-in](recording.md)
