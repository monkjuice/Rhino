---
title: Computer MIDI keyboard
type: component
summary: Plays notes from the typing keyboard into the MIDI input with Forge's exact key mapping, so arming and recording need no special case.
tags: [rhino, midi, keyboard]
sources: []
updated: 2026-10-03
---

# Computer MIDI keyboard

`native/src/ComputerKeyboard.*`, owned and wired by `ControlWindow` in `Main.cpp`. **M** switches it on and off; while on, **A** to **;** play seventeen semitones (home row white keys, the row above black), **Z**/**X** move the octave (3 to 7, default 5) and **C**/**V** the velocity (steps of 10, 1-127, default 100).

## It plays the input, not a track

Notes go through `Session::sendMidiInputNote` into the `keyboardState` of the engine's MIDI input devices, the entry a controller's notes take. Nothing downstream can tell where a note came from, so arming, monitoring and recording need no second path. It writes into both virtual devices, "All MIDI Ins" and "Computer Keyboard", because a track on either has to hear it ([Track inputs and monitoring](inputs-and-monitoring.md)). `Session::midiInputDevice` is only a fallback.

## One mapping, not two

The keys are `juce::MidiKeyboardComponent`'s default, `"awsedftgyhujkolp;"`, with Forge's numbering (`12 * octave + offset`, so octave 5 starts on MIDI 60). Forge's on-screen keyboard plays from the same default and base octave, so a part played into Forge's window and one recorded through Rhino land in the same key: the surest way to keep two mappings identical was not to write a second.

## Key handling, and why

- **`juce::KeyPress::operator==`, not a character.** `F9Key` is `VK_F9 | extendedKeyModifier` = 0x10078; squeezed into a character it is `'x'`, which would have turned the record key into an octave shift. Equality also requires exact modifiers, so Ctrl+C still copies.
- **A `juce::KeyListener`, not `keyPressed`.** JUCE offers a key to listeners before the component's own handler, walking up from the focused component, so the keyboard listens on the shell, browser, arrangement, note editor, audio clip editor, device rack and, as a backstop, the window. A focused `TextEditor` consumes its keys first, which is why typing a track name plays no chord.
- **Notes start in `keyStateChanged`**, which reads the keys physically down, so auto-repeat never retriggers a held key; `keyPressed` only swallows the mapped letters so they do not also fire their shortcuts. While it is on, those letters' plain shortcuts (the arrangement's C for colour, the note editor's F) are unavailable; R and B are not mapped and still work.
- Moving the octave or switching off releases every sounding note first, so nothing hangs.

## Focus from the first frame

A window with nothing focused keeps its keys for itself, above the content, and nothing here took focus on its own, so Space, F9 and the keyboard did nothing until the first click. `ControlWindow` now wants focus and is given it at startup (commit `a1d4ef0`).

## Related

- [Track inputs and monitoring](inputs-and-monitoring.md)
- [Recording and the count-in](recording.md)
- [Hosting Forge in Rhino](forge-hosting.md)
- [App shell and control bar](app-shell.md)
