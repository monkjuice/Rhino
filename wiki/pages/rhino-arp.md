---
title: Rhino Arp
summary: Architecture, parameter model, beat-domain scheduling, and dedicated rack UI for Rhino's built-in arpeggiator.
tags: [devices, midi, arpeggiator, timing, ui]
updated: 2026-10-03
---

# Rhino Arp

Rhino Arp is a standalone built-in `te::Plugin` in `RhinoDevices`, registered under the stable type `rhino.arp.v1`. It remains a rack MIDI effect before the instrument: it owns no synth voices or audio path, and adding or editing it must not involve `Session` or an instrument implementation.

Its 13 automatable controls cover style (Up, Down, UpDown, DownUp, Chord), rate, gate/overlap, Hold, pattern Offset, groove (Straight, Swing 8, Swing 16), retrigger (Off, Note, Beat plus interval), finite/infinite repeats, and scale-aware Distance/Steps with Chromatic, Major, or Minor plus Root. The intentional preset-like defaults are UpDown, 1/16, 84% gate, Distance -7 scale degrees, Steps 2, Swing 16, Hold on, Beat retrigger every 1/2 note, infinite repeats, and G# minor.

## Timing contract

Schedule in edit beats, not block-relative seconds. Apply incoming note changes at their actual in-block MIDI timestamp, then convert scheduled events to seconds only when writing output. This prevents generated notes from appearing before the input event that triggered them. Reuse pre-sized MIDI output scratch arrays and fixed-size pending note-off storage on the realtime path; do not introduce callback allocation.

Gate may extend to 200%, so note-offs can overlap following steps. Scale Distance is measured in scale degrees, not semitones; for example, -7 degrees in C minor moves down an octave.

## Editor and tests

The dedicated 820 px rack face lives in `DeviceEditorPanelArp.cpp`, exposes every automatable control, and paints the cyan moving-note diagram. Keep it in its own translation unit and list that file explicitly in `native/CMakeLists.txt`; MIDI-effect chrome uses the named theme colour rather than a literal.

Coverage belongs at three levels: direct processor timing/pitch behavior, parameter/default registration, and dedicated-face bounds plus off-screen snapshot smoke coverage. A timing regression test should assert that no generated event precedes an in-block note-on.
