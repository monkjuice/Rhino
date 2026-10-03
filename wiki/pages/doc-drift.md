---
title: Where the written docs disagree with the code
type: analysis
summary: Statements in Rhino's own docs that the code has overtaken, checked on 2026-10-03, with what the code does instead.
tags: [rhino, docs, maintenance]
sources: []
updated: 2026-10-03
---

# Where the written docs disagree with the code

Rhino statements found stale while seeding this wiki on 2026-10-03; Forge's are in
[Where Forge's docs disagree with its code](forge-doc-drift.md). `AGENTS.md` was the most current document, but not
infallible. Fix the doc, then delete its row here.

## User-facing docs

| Doc | Says | The code |
| --- | --- | --- |
| `README.md` | An instrument drop makes a track MIDI for good | The kind is fixed at creation; the drop is refused on an audio track ([A track's kind is fixed when it is made](track-kind-fixed-at-creation.md)) |
| `README.md` | A track records MIDI if it runs an instrument | It records by kind, `trackRecordInput` ([Recording and the count-in](recording.md)) |
| `README.md` | One monitor setting for the audio input; **Edit > Monitor the audio input** | Monitoring and input are per track, set on the card ([Track inputs and monitoring](inputs-and-monitoring.md)) |
| `README.md` | Takes are written beside the project | Into a `<project> Recordings` folder beside it |
| `README.md` | **Add audio** button, **Synth Gain**, **Snap 1/16**, a span dragged with **S** held | None exists; snapping has off, adaptive and fixed grids (`ArrangementGrid.h`) |
| `README.md` | Export at 48 kHz, peak-normalised to -1 dBFS | The device's rate and block size, dithered, at unity ([Project files (.rhinoedit)](project-files.md)) |
| `README.md` | Portable CMake in `native/.tools/` | Gone; CMake is on `PATH` |
| `README.md`, `native/README.md` | Two independent panels below the arrangement; a click opens a clip | One lower pane with three faces; a clip opens on double-click ([App shell and control bar](app-shell.md)) |
| `README.md`, `native/README.md` | A 10 Hz timer updates the transport text | One 30 Hz timer, which also drives live automation |

## Implementation docs

| Doc | Says | The code |
| --- | --- | --- |
| `native/README.md` | `trackType` answers `midi` for any track with an instrument; the fifth track is `MIDI 5` | `trackType` reads only `rhinoTrackType`; names carry no number ([Track kinds: audio and MIDI](track-kinds.md)) |
| `native/README.md` | `pasteClipSnapshots`; `groupNameBounds`; Tune is the only face needing `devicePlugin` | `pasteClipRegion`; gone; the EQ and Vocoder faces use it too |
| `native/README.md` | The readout is elastic; `RhinoCore` is the library plus pitch DSP | Centred and capped at 480 px; `RhinoCore` also holds the EQ, spectrum, vocoder and system-usage code |
| `native/README.md`, `VocoderTest.cpp` | The test's four tones are five octaves apart | 250 Hz to 4 kHz, four octaves |
| `AGENTS.md` | Every track is made by `appendTrack` | The starter stack and group buses are not |
| `AGENTS.md` | No chrome colour is a hex literal | Older UI files (the rack, the note editor, the device faces, the clip editor, the startup screen) still use literals ([Colours and typography](colours-and-typography.md)) |
| `AGENTS.md` | Neither off-limits tree is compiled; `tuningSuite()` is in `ForgeTests.cpp` | `native/.deps` is compiled into the app; the suite is in `tests/ForgeTestsOscillator.cpp` |
| `HANDOVER.md` | A one-track starter; six edits per device; `Main.cpp` is 684 lines | Four tracks; the catalog exists; about 2,200 lines |
| `SESSION-VIEW.md` | `native_arrangement_workflow` fails intermittently; no view can rename a track; `showTrackMixer` | Fixed; the arrangement renames and colours; gone |
| `native/src/tests/README.md` | Device checks are Tune and EQ | Also `VocoderTest.cpp` |
| `research/DEVICE-SYSTEM.md` | The GUI rate never sets automation precision | Live automation runs at the shell's 30 Hz ([Track automation](automation.md)) |
| `ARCHITECTURE.md` | The frontend is undecided; two tracks | A research-era snapshot of 2026-09-08 |

## Stale comments and names in code

- `native/CMakeLists.txt` says explicit lists make `ZERO_CHECK` unnecessary; in fact a new file then needs a manual
  configure ([A new source file needs an explicit CMake configure](cmake-does-not-reconfigure.md)).
- `SessionRecording.cpp`'s header and `Session.h`'s `RecordInput` comment still tie recording to an instrument.
- `Session.cpp` still names the engine `"Theda Native"`, the project's first name.

## Related

- [Where Forge's docs disagree with its code](forge-doc-drift.md)
- [Hazards found while seeding the wiki](known-hazards.md)
- [Overview](../overview.md)
- [Session, the model](session-model.md)
