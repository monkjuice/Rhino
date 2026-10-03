---
title: Track inputs and monitoring
type: component
summary: Each track names its own MIDI or audio input and its own Off, Auto or In monitoring, all resolved to engine devices by applyRecordArming.
tags: [rhino, recording, midi, audio-io]
sources: []
updated: 2026-10-03
---

# Track inputs and monitoring

Which input a track takes, and whether you hear it, are properties of the **track**, as in Live. The model is `native/src/SessionMidiInput.cpp`, `SessionAudioInput.cpp` and the monitoring half of `SessionRecording.cpp`; the card's chooser and On/Auto/Off control (`MonitorSelector.*`) are wired in `ArrangementSync.cpp` and `ArrangementGestures.cpp`. Every choice is saved on the track, follows it when reordered, and opens no undo transaction. `Session::applyRecordArming` turns it all into engine routing ([Recording and the count-in](recording.md)).

## MIDI From

`rhinoMidiInput` holds a token: absent means **All Ins** (so no older document needed migrating), `keyboard` the typing keyboard, `none` nothing, `device:<name>` one port. All Ins resolves to the engine's "All MIDI Ins" virtual device, which merges only the physical inputs that are *open*, so choosing it enables them all. The typing keyboard gets a second virtual device, created the first time a track asks for it ([Computer MIDI keyboard](computer-keyboard.md)).

**The rescan trap.** Creating that device rescans the MIDI device list asynchronously: it does not exist when `createVirtualMidiDevice` returns. Choosing Computer Keyboard used to fail once with "no MIDI input" and work the second time. `awaitingMidiDeviceScan` now suppresses the complaint, and `MidiDeviceWatcher` re-arms when the rebuilt list lands, which is also why a keyboard plugged in mid-session starts playing without a relaunch.

**Armed is not playable.** With no hardware keyboard and the typing keyboard off, an armed MIDI track is correct and silent. `Session::hasHardwareMidiInput` lets the arm message say so.

## Audio From

`rhinoAudioInput` works the same way: empty means **Default In**, whatever Audio settings chose, so a project opened on another machine points at an input that exists there. The chooser lists every input, enabled or not, because choosing one is what switches it on. A device the machine lacks resolves to null and is reported when the track is armed.

## Monitoring

`Session::InputMonitoring` (off, automatic, on) is Live's Off/Auto/In and exactly Tracktion's `InputDevice::MonitorMode`. It is stored as `rhinoMonitor` only when it differs from the kind's default: **Auto on MIDI** (the only setting where playing an armed instrument makes a sound) and **Off on audio**, unlike Live, because a laptop's microphone and speakers feed back.

The engine keeps the mode on the *device*, so two tracks sharing one input get the strongest mode either asked for (On beats Auto beats Off), which never silences a track that asked to hear itself. Different inputs give each track exactly what its card says. A mode change reapplies routing, except during a take or count-in: `setMonitorMode` restarts the transports and would cut the recording in half.

`README.md`'s monitoring paragraph is stale: there is no longer one setting for the audio input, nor an **Edit > Monitor the audio input** item (checked 2026-10-03). Long form: *Audio From* and *MIDI From* in `native/README.md`.

## Related

- [Recording and the count-in](recording.md)
- [Computer MIDI keyboard](computer-keyboard.md)
- [Track kinds: audio and MIDI](track-kinds.md)
- [Reading MIDI hardware on Windows](reading-midi-hardware-on-windows.md)
- [Arrangement view](arrangement-view.md)
