---
title: "juce::File::createOutputStream appends"
type: gotcha
summary: JUCE opens an existing file for output at its end, so rewriting a PNG or a render in place leaves the old content in front.
tags: [both, juce, testing]
sources: []
updated: 2026-10-05
---

# juce::File::createOutputStream appends

## Symptom

You write a snapshot PNG to the same path as last time, and every viewer, PIL included, still shows the old image whatever you change in the code. In the case that taught this lesson, filling the image with magenta produced no magenta, a `fillRect` produced no rectangle, and `paint()` was provably running. An hour went into chasing a text run that had been drawn correctly all along.

## Cause

`juce::File::createOutputStream()` opens an existing file with its write position at the **end**. The second PNG is appended after the first, and every decoder reads the first image at the front of the file. The same applies to any format that starts with a header, such as a WAV.

## What to do

Use one of the three patterns the code already uses:

- **Delete first.** `writeSnapshot` in `native/src/Main.cpp` calls `deleteFile()` before opening the stream, and its comment explains why. It writes the `RHINO_SHELL_SNAPSHOT` and `RHINO_STARTUP_SNAPSHOT` images. The env-gated PNGs in `AutomationLanes.inc` (`RHINO_AUTOMATION_SNAPSHOT`) and `ClipWarp.inc` (`RHINO_CLIP_PANEL_SNAPSHOT`) also delete first, and so does Forge's `--render` (`tests/ForgeTestRender.cpp`).
- **Rewind and truncate.** Forge's `--snapshot` (`tests/ForgeTestTools.cpp`) calls `setPosition(0)` and then `truncate()` on the stream.
- **Write to a new file.** Test fixtures write to a fresh `juce::TemporaryFile`. A project save writes to a `TemporaryFile` and then replaces the project with it through `overwriteTargetFileWithTemporary()` (`native/src/ProjectFiles.cpp`).

Appending is correct for a log: `rhino.log` is a `juce::FileLogger` and is meant to grow. The last write in the app that used none of the three patterns was `createSample` in `native/src/SessionSamples.cpp`, which rewrites a built-in sample when the file is missing or no more than 44 bytes long: a stub left by an interrupted write stayed in front of the new WAV and the import failed. Since commit `23de20c` it writes to a `juce::TemporaryFile` and moves it into place with `overwriteTargetFileWithTemporary()`. The arrangement's "clip edits are whole" scenario imports the whistle over such a stub.

## Confirm

Delete the image before each run, then check that it exists afterwards with a fresh modification time, which proves this run wrote it. If a file has grown by about the size of one image, the write appended to it.

## Related

- [Seeing the UI without taking the screen](headless-ui-snapshots.md)
- [Proving a Forge change changed nothing](proving-a-forge-change-changed-nothing.md)
- [Writing Rhino tests](writing-rhino-tests.md)
- [App shell and control bar](app-shell.md)
- [JUCE's isBold() is false for SemiBold](juce-isbold-misses-semibold.md)
