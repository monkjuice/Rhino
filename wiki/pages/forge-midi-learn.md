---
title: Forge MIDI learn
type: component
summary: Right-click learn binds a controller's knobs and pads to any control; bindings are kept per machine in MidiMap.xml, not in patches.
tags: [forge, midi, controllers]
sources: []
updated: 2026-10-03
---

# Forge MIDI learn

Right-click any control, choose **MIDI learn**, then move a knob or hit a pad. The menu's header names what is already bound and offers to forget it. The audio-thread half is `core/ForgeMidiMap.h`, the message-thread half `src/ForgeProcessorMidi.cpp`, the menu `src/ForgeEditorModulation.cpp`.

## Behaviour

- **Learning replaces at both ends.** The knob you moved drops what it drove, and the control drops whatever drove it: a control with two masters jumps, and the panel could not say which one moved it.
- **No device knowledge.** A pad and a key are the same message, so there is no table of hardware. A press is read off its target: a discrete parameter, such as a switch, steps to its next value and stays; a knob is held at the pad's velocity while the pad is down and restored on release.
- **Bound messages are consumed** before the wheels, the arp or the voices see them, so a bound pad does not also sound and a bound CC 1 no longer moves the mod wheel. Note releases always pass through, or a pad bound while held would hang its note.
- The map runs before the on-screen keyboard is merged in, so clicking a panel key never fires a pad binding. Only a press arms a learn, and pitch bend cannot be learned, since brushing the strip would bind it.
- **Default:** CC 21-28 drive the eight macros (`firstDefaultMacroCc`), written only on a machine with no bindings file and never over a binding. Unconfirmed on real hardware: two captures from the developer's Launchkey Mini caught nothing ([Reading MIDI hardware on Windows](reading-midi-hardware-on-windows.md)).

## Where bindings live

`%APPDATA%\Rhino Forge\MidiMap.xml`, one file for every instance. Not in a preset, where a patch from another machine would repoint your knobs; not in plugin state, where every project would remember a keyboard that may be unplugged. The file names parameters by id, so it survives parameter reordering, and a binding to a control this build lacks is dropped. Tests redirect it with `Processor::setMidiMapFile` rather than overwrite a real one.

## Two threads

The audio thread asks "is this bound?" of a flat array of atomics indexed by kind, channel and number: no lock, no allocation. It never writes a parameter, because notifying a host takes locks. A bound message goes onto a fixed 256-entry lock-free queue that the **Processor's** own 60 Hz timer drains — not the editor's, or a learned knob would stop working when the window closed.

In the standalone, tick the controller once under Options → Audio/MIDI Settings → *Active MIDI inputs*; JUCE leaves inputs off on Windows and macOS, and the choice is kept in `Rhino Forge.settings`. In a host nothing is needed.

## Related

- [Forge modulation: matrix, envelopes, LFOs and macros](forge-modulation.md)
- [Reading MIDI hardware on Windows](reading-midi-hardware-on-windows.md)
- [Driving Forge's standalone on Windows](driving-the-forge-standalone.md)
- [Real-time audio rules](real-time-audio-rules.md)
- [Forge presets and state](forge-presets-and-state.md)
