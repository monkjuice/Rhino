---
title: Reading MIDI hardware on Windows
type: guide
summary: Enumerate and monitor a MIDI controller from PowerShell through winmm, and why a port that will not open usually means something else.
tags: [both, midi, windows, debugging]
sources: []
updated: 2026-10-03
---

# Reading MIDI hardware on Windows

When a question turns on what a controller actually sends, such as which CC a knob emits or which port the pads use,
read the hardware rather than a manual. Nothing in the repository does this, and by the developer's rule nothing should:
write the script in the scratchpad or a user-level skill, never in the repo
([Development environment and reference material](development-environment.md)).

## Enumerate

From PowerShell, `Add-Type` a small C# class that P/Invokes `winmm.dll`: `midiInGetNumDevs` for the count and
`midiInGetDevCapsW` for each input's name. A controller can expose more than one input. The developer's Launchkey Mini
shows two, `Launchkey Mini` (the keys) and `MIDIIN2 (Launchkey Mini)` (its InControl/DAW port).

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

A MIDI input is exclusive. A running Forge standalone, RhinoDAW or another DAW holding the device makes `midiInOpen`
fail, so check `Get-Process` before theorising. Do not trust the error code to say why, though. A port whose device was
being unplugged returned `7` (`MMSYSERR_NOMEM`) and later `2` (`MMSYSERR_BADDEVICEID`), not the `4`
(`MMSYSERR_ALLOCATED`) that "someone else has it" should produce, and a `7` was once misread as Forge holding the port
when the keyboard had simply been unplugged. Re-enumerate before concluding anything: `midiInGetNumDevs` returning 0
and no such device in `Get-PnpDevice -PresentOnly` is what unplugged looks like.

## Open question

Forge's MIDI learn ships CC 21-28 to the eight macros as its default map, written only when no bindings file exists
([Forge MIDI learn](forge-midi-learn.md)). That default has never been checked against the developer's Launchkey Mini:
as of 2026-09-27 the capture attempts had caught no message from it. Verify it before relying on it, and in the Forge
standalone remember that a MIDI input must be ticked once in Options → Audio/MIDI Settings before anything arrives
([Driving Forge's standalone on Windows](driving-the-forge-standalone.md)).

## Related

- [Forge MIDI learn](forge-midi-learn.md)
- [Track inputs and monitoring](inputs-and-monitoring.md)
- [Computer MIDI keyboard](computer-keyboard.md)
- [Driving Forge's standalone on Windows](driving-the-forge-standalone.md)
- [Development environment and reference material](development-environment.md)
