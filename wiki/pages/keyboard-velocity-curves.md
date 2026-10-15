---
title: Keyboard velocity curves
type: component
summary: A velocity curve per MIDI keyboard port, kept per machine; a port set to anything but Linear is read by Rhino itself, bent on the MIDI thread and delivered to All Ins and a "<port> (curved)" stand-in, because Tracktion cannot rewrite a note on its way in.
tags: [rhino, midi, recording, hardware, settings]
sources: []
updated: 2026-10-15
---

# Keyboard velocity curves

Added 2026-10-15 for the developer's original Novation Launchkey Mini (USB `VID_1235 PID_0035`), which reaches high velocities only when struck hard; per Novation's support documents its curve is fixed in firmware. Rhino applies a curve per MIDI input port, chosen in the MIDI tab of Preferences ([App shell and control bar](app-shell.md#preferences)).

## The curves

`native/src/model/VelocityCurve.h` is a pure header: `VelocityCurve` (softest, softer, soft, linear, hard, harder, fixed) and `velocityCurves`, in chooser order, each with the word the settings file stores (a word, not a position, so a curve added later changes no old setting), a name and an exponent: 0.35, 0.5, 0.7, 1, 1.5, 2.2. `applyVelocityCurve` returns 127 x (in/127)^exponent clamped to 1..127; Fixed is always 127; 0 stays 0, since a note-on at 0 is a note-off. `tests/VelocityCurveTest.cpp` (`checkVelocityCurves`) runs in the arrangement geometry runner beside `checkPushProtocol`.

## How a curve is applied

`native/src/model/SessionKeyboards.cpp`: `Session` across units, its private state in `Session::Keyboards` behind a custom deleter, as `SessionMeters` and `SessionBounce` do.

- **Why Rhino reads the port.** Tracktion has no hook that rewrites a note on its way in, the gap the [Push](ableton-push.md) met; `MidiInputDevice::setOverridingNoteVelocities` (full velocity) is its only velocity option. So a port with a curve is opened by Rhino through a `juce::MidiInput` and kept from the engine as a controller's ports are: `disableReservedMidiPorts` disables curved inputs too, in the engine and in JUCE's `AudioDeviceManager`, but leaves their outputs alone, and `enablePhysicalMidiInputs` (All Ins) skips them.
- **Delivery.** Each note-on is bent on the MIDI thread and delivered, as `sendControllerMidi` delivers the Push's pads, to All Ins and to a per-port virtual stand-in named `<port> (curved)`. A track whose MIDI From is `device:<port>` resolves to the stand-in (`midiInputDeviceForTrack`) and its card still shows the port's name ([Track inputs and monitoring](inputs-and-monitoring.md)). Arming, monitoring and recording see an ordinary input, so a take records what was heard.
- **Linear is the engine's untouched path**, so a machine that never sets a curve behaves exactly as before.
- **The cost.** While a curve is set, Rhino holds the port for as long as it runs, as it holds a Push: a Windows MIDI port has one owner between processes, so no other program can open that keyboard meanwhile.
- A port that goes (unplugged or handed back) sends note-offs to both targets for every note its keys held.
- A controller's reserved port is refused a curve: what it plays is already shaped.

## Settings and start-up

Per machine: `keyboard.velocityCurve.<port name>` = the curve's word, in the settings file (`rhinoSettingsOptions`), never read or written under the test runners. `initialiseKeyboards`, from the `Session` constructor, loads them and makes the port entries at once, so a project opened from the command line arms its tracks onto the stand-in rather than onto a port the engine would then have to give up. Ports are opened only after `Session::startKeyboardPorts()`, which `MainComponent::startHardwareControllers` calls beside `push.start()`; the runners never call it.

## syncKeyboardPorts

The one reconcile step, on the message thread. It runs from `startKeyboardPorts`, `setVelocityCurve`, every MIDI device-list rebuild (`midiDevicesChanged`), `finishRecording`, and a once-a-second timer while a curved port is not open (busy or unplugged). In order it:

1. updates curves in place (atomic; the next note uses it, even mid-take);
2. takes and hands back ports; deletes stale `(curved)` stand-ins the engine kept for a port with no curve and creates missing ones, setting `awaitingMidiDeviceScan`;
3. disables curved ports in the engine on every pass, because Tracktion remembers each device's enabled flag between runs;
4. opens and closes ports by name against `juce::MidiInput::getAvailableDevices` (a changed identifier means unplugged);
5. repoints each port's All Ins and stand-in targets under a `SpinLock`;
6. re-registers `PhysicalMidiInputDevice::Listener`s on engine-read ports, so the MIDI tab shows the last note on Linear ports too.

It returns true when a port changed hands, and the caller re-arms (`rearmListeningMidiTracks`, factored out of `midiDevicesChanged`). Under a take nothing changes hands, because moving tracks onto another device would cut it; `finishRecording` syncs again and sets `armingDeferred` if anything moved ([Recording and the count-in](recording.md)). Taking a port rescans the device list, whose handler syncs again; that re-entry is noted (`syncing`/`syncAgain`) and run once the pass in hand ends.

## Tracktion facts it relies on

Read on GitHub at the pinned commit `4536d8a` ([Directories not to read](off-limits-directories.md)):

- `createVirtualMidiDevice` and `deleteVirtualMidiDevice` only edit the stored virtual-device list and call `rescanMidiDeviceList`, which starts a 5 ms timer. The list is rebuilt asynchronously, so a device deleted during a sync pass stays valid for the rest of it, and the test runners, which dispatch no message loop, never see a stand-in appear.
- `PhysicalMidiInputDevice::Listener::handleIncomingMidiMessage` is called under the device's `listenerLock`, which `addListener` and `removeListener` also take, so removing a listener from the message thread is safe.

## States and the reading

`keyboardPortState`: `engine` (Linear), `open`, `busy` (open refused twice in a row: on Windows another program has it; one refusal can be the engine still letting go) and `absent` (curve set, not plugged in or not started). The last note is one packed atomic word (count, struck, played), so a panel never mixes two notes; the MIDI tab polls it at 20 Hz while visible. A Linear port the engine fails to open still cannot report busy ([Reading MIDI hardware on Windows](reading-midi-hardware-on-windows.md#ableton-live-holding-the-ports-2026-10-15)).

## Tests

`tests/Arrangement/scenarios/KeyboardCurves.inc`, included after `PushController.inc` in `WorkflowTest.cpp`, uses a made-up port name, never opens a port and drives the port's callback through `receiveKeyboardMidi`. It covers a curve taking the port, a track resolving to the stand-in, a note bent and counted, Linear handing the port back, the Push scenario's reserved port refused a curve, and the Preferences panel's tabs; `RHINO_PREFERENCES_SNAPSHOT=<png>` writes the panel ([Seeing the UI without taking the screen](headless-ui-snapshots.md)).

## Related

- [Track inputs and monitoring](inputs-and-monitoring.md)
- [Ableton Push 1](ableton-push.md)
- [Recording and the count-in](recording.md)
- [App shell and control bar](app-shell.md)
- [Reading MIDI hardware on Windows](reading-midi-hardware-on-windows.md)
- [Session, the model](session-model.md)
- [Real-time audio rules](real-time-audio-rules.md)
