---
title: Reading MIDI hardware on Windows
type: guide
summary: Enumerate and monitor a MIDI controller from PowerShell through winmm, and why a port that will not open usually means something else.
tags: [both, midi, windows, debugging]
sources: []
updated: 2026-10-15
---

# Reading MIDI hardware on Windows

When a question turns on what a controller actually sends, such as which CC a knob emits or which port the pads use,
read the hardware rather than a manual. Nothing in the repository does this, and by the developer's rule nothing should:
write the script in the scratchpad or a user-level skill, never in the repo
([Development environment and reference material](development-environment.md)).

## Enumerate

From PowerShell, `Add-Type` a small C# class that P/Invokes `winmm.dll`: `midiInGetNumDevs` for the count and
`midiInGetDevCapsW` for each input's name. A controller can expose more than one input. The developer's original
Launchkey Mini (USB `VID_1235 PID_0035`) shows two, `Launchkey Mini` (the keys) and `MIDIIN2 (Launchkey Mini)` (its InControl/DAW port). An original Ableton
Push shows `Ableton Push` (its Live port) and `MIDIIN2 (Ableton Push)` / `MIDIOUT2 (Ableton Push)` (its User port).
A second identical device gets a `2- ` prefix, inside the parentheses too ([Ableton Push 1](ableton-push.md)).

## Monitor

1. Say what to touch (which knobs, pads, keys) **before** opening the capture. It needs someone moving the controls,
   and two of three attempts came back empty for want of one.
2. Open **every** input with `midiInOpen` and `CALLBACK_FUNCTION` (`0x00030000`), passing a C# delegate, then call
   `midiInStart` on each. Knobs and pads may sit on either port.
3. Keep the delegate in a static field, or it is garbage-collected mid-capture. Have the callback enqueue onto a static
   `ConcurrentQueue` and print from PowerShell.
4. Keep the `MIM_DATA` (`0x3C3`) messages. The short message is packed in `dwParam1` as
   `status | data1 << 8 | data2 << 16`.
5. Close every port when done (`midiInStop`, `midiInClose`), or end the PowerShell process: an input left open blocks
   every other program.

## When a port will not open

A MIDI input is exclusive between processes; within one process JUCE shares the handle, so a second open there
succeeds ([Ableton Push 1](ableton-push.md)). A running Forge standalone, RhinoDAW or another DAW holding the device makes `midiInOpen`
fail, so check `Get-Process` before theorising. Do not trust the error code to say why, though. A port whose device was
being unplugged returned `7` (`MMSYSERR_NOMEM`) and later `2` (`MMSYSERR_BADDEVICEID`), not the `4`
(`MMSYSERR_ALLOCATED`) that "someone else has it" should produce, and a `7` was once misread as Forge holding the port
when the keyboard had simply been unplugged. Re-enumerate before concluding anything: `midiInGetNumDevs` returning 0
and no such device in `Get-PnpDevice -PresentOnly` is what unplugged looks like.

**A running RhinoDAW holds a Push's User port** (since 2026-10-13), so close Rhino before capturing a Push. Rhino also
switches the Push into User mode, which routes the pads, buttons and encoders to the User port, so while Rhino runs,
the Live port can be opened but carries nothing. Rhino keeps its engine off all four Push ports, and Tracktion remembers
them as disabled ([Ableton Push 1](ableton-push.md)). Since 2026-10-15 it likewise holds any keyboard port given a
velocity curve other than Linear ([Keyboard velocity curves](keyboard-velocity-curves.md)).

### A controller Windows cannot enumerate (2026-10-15)

The Launchkey vanished from Windows entirely: "Unknown USB Device (Device Descriptor Request Failed)", problem code 43,
on port 1 of a `VID_1A40 PID_0101` hub chained behind a Genesys `05E3:0608` hub. Moved to a USB port on the PC itself,
it enumerated again. When a controller is missing from `midiInGetNumDevs`, look at its USB device before any MIDI code.

### Ableton Live holding the ports (2026-10-15)

Rhino and the Forge standalone then both "could not select" the Launchkey because Ableton Live 12 Trial was running:
its `Launchkey_Mini` control-surface script and Track input held both Launchkey ports, and the Push's Live port too.
`midiInOpen` returned `7` (`MMSYSERR_NOMEM`) for every port Live held, never `4`, so a `7` does not mean unplugged
either. Live's own `Log.txt` under `%APPDATA%\Ableton\Live 12.x\Preferences` lists the `MidiRemoteScript` and
`MidiInDevice` settings that prove what it holds. Rhino's engine path still reports "plays from <port>" when its open
fails; the tell is a missing `opening MIDI in device:` line in `rhino.log`. A port with a velocity curve shows **busy**
in Preferences' MIDI tab; a Linear port, opened by the engine, cannot.

## Open question

Forge's MIDI learn ships CC 21-28 to the eight macros as its default map, written only when no bindings file exists
([Forge MIDI learn](forge-midi-learn.md)). That default has never been checked against the developer's Launchkey Mini:
as of 2026-09-27 the capture attempts had caught no message from it. Verify it before relying on it, and in the Forge
standalone remember that a MIDI input must be ticked once in Options → Audio/MIDI Settings before anything arrives
([Driving Forge's standalone on Windows](driving-the-forge-standalone.md)).

## Related

- [Forge MIDI learn](forge-midi-learn.md)
- [Ableton Push 1](ableton-push.md)
- [Keyboard velocity curves](keyboard-velocity-curves.md)
- [Track inputs and monitoring](inputs-and-monitoring.md)
- [Computer MIDI keyboard](computer-keyboard.md)
- [Driving Forge's standalone on Windows](driving-the-forge-standalone.md)
- [Development environment and reference material](development-environment.md)
