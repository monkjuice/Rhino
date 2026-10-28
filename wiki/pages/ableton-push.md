---
title: Ableton Push 1
type: component
summary: Rhino drives the original Push itself on its User port and keeps all four of its ports from the engine; the pads are translated into a scale or a Drum Rack's bank on the MIDI thread and play like any keyboard, and the buttons and encoders become Rhino's commands, never a track's MIDI.
tags: [rhino, midi, controller, hardware]
sources: []
updated: 2026-10-27
---

# Ableton Push 1

Rhino drives the original Ableton Push itself (added 2026-10-13). `native/src/app/PushProtocol.h` is the protocol, a pure header with no JUCE or `Session`. `PushController` is one class across `PushController.cpp` (the ports, the MIDI thread and the lifecycle), `PushControllerControls.cpp` (the buttons and encoders) and `PushControllerSurface.cpp` (the pads, the lights and the display). The model side is `native/src/model/SessionControllers.cpp`. Ableton never published the Push 1's protocol, so it was taken from Live 9's Push script, DrivenByMoss and push-wrapper. **It first ran on the hardware on 2026-10-13**: the Push lit up and Rhino took over its display. Most of this page is still read from the code ([On the hardware](#on-the-hardware-2026-10-13)). A Push 2 (`Ableton Push 2`) uses a different protocol and Rhino ignores it.

## Why Rhino drives it, not Tracktion

At the pinned commit `4536d8a`, Tracktion has no Push driver and cannot remap notes on an input. Its `ControlSurface` framework needs a `SelectionManager` and a `UIBehaviour`, which Rhino lacks. The raw pads send notes 36-99 in rows 8 semitones apart, and these have to be translated before any track hears them. The buttons and encoders send CCs that must never reach a track: the upper row is CC 20-27, and Forge's MIDI-learn default puts its macros on CC 21-28 ([Forge MIDI learn](forge-midi-learn.md)).

## Ports

- **Port names.** On Windows the Push shows as `Ableton Push` (the Live port) and `MIDIIN2 (Ableton Push)` / `MIDIOUT2 (Ableton Push)` (the User port). On macOS the names are `Ableton Push Live Port` and `Ableton Push User Port`. Windows prefixes a second identical device with `2- `, inside the parentheses too (`push::portRole`). Rhino uses the User port in both directions, as DrivenByMoss and push-wrapper do.
- **The engine is kept off all four ports, on every rescan.** `Session::reserveMidiPorts` records the names and calls `disableReservedMidiPorts`, which disables the engine's devices for them. `midiDevicesChanged` calls it again first, before its early return, on every rebuild of the engine's device list. Only a reserved device still enabled is touched, so the rescan that disabling itself causes finds nothing to do. Reserving also unticks them in JUCE's `AudioDeviceManager`, because Audio settings opens ticked inputs behind the engine's back. Every input path skips a reserved name ([Track inputs and monitoring](inputs-and-monitoring.md)). The outputs are reserved too, because the engine sends all-notes-off to every enabled output at each stop, which would blank the pads.
- **Why on every rescan.** Tracktion builds its MIDI device list on a timer after start-up, and keeps each device's enabled flag in its `Settings.xml` (`%APPDATA%\Theda Native`). On the first hardware run `rhino.log` showed "Push connected" before "MIDI Input devices scanned in", so reserving met an empty list and disabled nothing. The scan then found all four Push ports enabled, as All Ins had left them in earlier runs, and opened them. **Nothing failed: a Windows MIDI port has one owner between processes, not within one.** JUCE's Win32 backend shares one OS handle per device inside a process, so the engine and the controller's own `juce::MidiInput` both opened the User port. The raw pad notes and button and encoder CCs reached All Ins beside the translated ones, and an armed track would have heard every pad twice.
- **Retries.** The engine closes a disabled port asynchronously. `lookForPush` runs once a second on the controller's own 30 Hz timer, so it retries the open. After three failures it reports once that another program, such as Ableton Live, holds the Push. Plugging in or unplugging is noticed within a second.
- **No handshake.** Writing to the display clears "Please start Live to play". `greet` first sends User mode (`F0 47 7F 15 62 00 01 01 F7`), because a Push that Live crashed out of stays in Live mode. It then sends Live's pad sensitivity (`push::padSensitivitySysex`, see [Pad sensitivity](#pad-sensitivity)), poly aftertouch (`5C 00 01 00`) and the touch strip as a pitch bend that springs back (`63 00 01 05`), and redraws everything. All of it is resent on every connect.
- **On `stop`** the controller ends its gestures, releases held notes, darkens the pads and lit buttons, and writes "Rhino has closed" on line 2.

## Threads

- **Pads are played on the MIDI thread.** The pad map is a 64-entry table guarded by a `SpinLock`. A per-pad `sounding` table means that letting go releases the note actually played, even if the scale or octave changed in between. A per-note count (`padsHolding`) handles a note that sits on two pads in the fourths layout: it starts with the first pad down and stops with the last pad up. Accent plays every pad at velocity 127. Poly aftertouch follows the sounding note, and pitch bend and channel pressure pass straight through.
- **`Session::sendControllerMidi` is the one `Session` call that is safe from any thread.** It calls `handleIncomingMidiMessage(msg, MPESourceID)` on cached `shared_ptr`s to the All Ins virtual device and the Push virtual device, under `controllerTargetLock`. The cache is refreshed on the message thread at the top of `midiDevicesChanged`, before its early returns. The lock is held during delivery, so the last reference to a device the engine has dropped is always released on the message thread.
- **Everything else goes to the message thread**: buttons, encoders, encoder touches and sysex replies. They cross through a fixed ring of 256 `juce::MidiMessage` slots and two atomics (`controlQueue`, `controlWritten`, `controlRead`), one writer (the MIDI thread) and one reader (the `AsyncUpdater`'s callback on the message thread). A message that finds the ring full is dropped: a lost press costs a second press, where a MIDI thread blocked on the heap would cost every pad on the port its timing. `closePorts` empties it, since what the port sent is for a Push that is gone. Until 2026-10-23 the queue was a `std::vector` the MIDI thread grew under a `SpinLock` ([Real-time audio rules](real-time-audio-rules.md)).
- **Output goes through `juce::MidiOutput`'s background thread** (`sendBlockOfMessages`), because a sysex can block the caller for milliseconds.

## The pads are a keyboard

By the user's decision, the pads feed All Ins and an input choice called "Push" (token `device:Push`, `Session::midiInputPushToken`). The Push input is a virtual device made on first use, like the Computer Keyboard's, and is subject to the same asynchronous rescan. Cards list it while a Push is connected or while a track names it. **Nothing is armed implicitly**: to play from the pads, arm a MIDI track or set its monitoring to In.

In key, the bottom-left pad plays the root at octave 3 (MIDI 48 for C). Each row starts a fourth above the one below: in key, that is the fewest scale steps that reach five semitones (three in a major scale); chromatic, it is five semitones. On a track with a [Drum Rack](drum-rack.md), the bottom-left four by four plays the bank the rack shows, lowest note at the bottom left, and the other pads play nothing.

## Pad sensitivity

The Push 1 turns pad pressure into velocity itself, through a threshold and a curve in its firmware: `F0 47 7F 15 5D 00 20`, then 32 nibbles (off threshold and on threshold, 4 each; gain and two curve terms, 8 each; most significant nibble first), then `F7`. Live sends its own every time it starts; Rhino sent nothing until 2026-10-13, so the Push stayed on its power-on default and the user had to hit pads too hard for full velocity. `greet` now sends Live's default: threshold 0 and the curve Live calls Log 1 (off 190, on 210, gain 85000, curves 120000 and 60000). `PushProtocolTest.cpp` checks the bytes against Live 9.1.2's captured start-up payload.

- **No sensitivity page, by decision.** Live has one on Push 1's User button (threshold -20..+20, six curves). It was rejected because it would only ever reach the Push. A per-port software curve now exists for keyboards ([Keyboard velocity curves](keyboard-velocity-curves.md), 2026-10-15), but it refuses a reserved port, so it does not reach the Push's pads.
- **If a page is ever wanted:** gain/curve1/curve2 are Linear 100000/45000/0, Log 1 85000/120000/60000, Log 2 85000/120000/50000, Log 3 100000/120000/50000, Log 4 130000/120000/50000, Log 5 140000/120000/0. Threshold step `t` gives on = 10 + 10(t+20), off = 10 + 9(t+20).
- **Loudness is not only the Push.** The instrument's own velocity response matters too (the Drum Rack's per-pad Velocity control), and Accent plays every pad at 127.

## Controls

- **Play** (while rolling, a pause; Shift: `Session::stop`, the top of the song, since 2026-10-27; [Transport](transport.md#pause-returns-to-the-passage-stop-to-the-top)) and **Record** go through shell callbacks, so the DJ view's rules hold: there Play toggles the focused deck, and Record replies that recording belongs to the arrangement.
- **Other buttons.** Metronome toggles the click. Tap tempo uses the mean of the last four intervals, starts over after a 2 s pause, and makes one run of taps a single tempo gesture that closes 2.1 s after the last tap. Undo (Shift: redo). Accent.
- **Octave up and down** move the layout between octaves -1 and 8. On a Drum Rack they move the rack's own bank by 16 through `showDrumBank`, so the rack's face follows.
- **Scales** puts the root, the scale (`core/dsp/ScaleQuantizer.h`) and in key/chromatic on encoders 1-3, one step every 6 detents; pressing it again returns to the previous mode. The layout is kept per machine through `Session::controllerSetting` (`push.*` keys in the settings file), and never read or written under the test runners.
- **Tracks.** Left, Right and the lower row select tracks, eight per bank, each button lit in its track's colour. A tap on Mute or Solo toggles the selected track; held together with a lower-row button, it toggles that track instead.
- **Encoder modes.** Device, Volume and Pan & Send choose what the encoders do.
  - In Device mode, the upper row picks a device in the chain, and In/Out (CC 62/63, unverified) page through its parameters eight at a time. A continuous control moves through a `NormalisableRange` with the parameter's skew, about 140 steps across its whole travel, ten times finer with Shift. A discrete control, a choice or a toggle moves one step every 6 detents.
  - In Volume and Pan mode, the encoders drive the bank's eight tracks: 0.5 dB a step for volume (Shift 0.1) and 0.01 for pan (Shift 0.002).
  - The master encoder sets the main volume and the tempo encoder moves 1 BPM a step (Shift 0.1).
- **While the shell is disabled** (opening a project) **or the loading screen runs** (a bounce since 2026-10-14, a WAV export since 2026-10-15), presses are dropped but releases are not (`commandsAllowed`).

## Gestures

A gesture opens on the **first turn, not the touch**: `beginDeviceParameterGesture` takes an automated knob off its lane, and a finger merely resting on an encoder must not do that. The gesture closes when the touch lifts, or after 300 ms without a move while nothing touches the encoder. Every open gesture is closed first before undo or redo, mute or solo, a selection, mode or page change, `editWillChange`, and whenever the port closes. A Push unplugged under a finger never sends the touch's release, so `closePorts` also forgets every touch and held modifier. A whole turn is one undo step.

**A bug the Push made reachable.** `endDeviceParameterGesture`, `endTrackVolumeGesture` and `endTrackPanGesture` used to return early when their device or track had gone, before decrementing `parameterGestureDepth` or `mixerGestureDepth`. With the depth stuck, every later parameter change was announced to the rack alone, and the DJ decks' `gestureHeld` stayed true. They now always decrement the depth and close the transaction ([What a change costs the interface](ui-cost-of-a-change.md)). A held encoder can rest while its device is deleted, which is how the Push exposed the bug.

## What it shows

On each 30 Hz tick while connected, `wantedSurface` works out the pad colours, the button lights and the display's four 68-character lines, and only the differences are sent. The redraw runs every tick, not only on change messages, because the model does not announce a knob that automation moves, nor a transport started from the keyboard.

- **Pad colours.** The root takes the track's colour, the nearest hue in the Push's palette (a grey track's root is sky blue instead). In-scale pads are white and out-of-scale pads are dark. Every pad sounding a held note turns green. On a Drum Rack, a pad with a sound takes the track's colour and an empty pad is dark grey.
- **Device mode display.** The lines show the controls' names, then their values, then the track and device with the page, then the chain, with the selected device marked by the display's own arrow (glyph 127).
- **Volume and Pan display.** The lines show the track names, the values, bars, and the main level.
- **Text.** The display is ASCII; each non-ASCII character of a UTF-8 name shows as one `?`.

## Selection

The Push follows the arrangement's selected track; it ignores the main row. It also follows the device the rack reports through `DeviceRack::deviceSelected`, which fires from the end of `sync` only when the track and plugin pair changes. With no report, it takes the track's instrument, or failing that its first device. The lower row and Left/Right select through `Arrangement::selectTrack`. The upper row moves the rack through `DeviceRack::selectPluginIndex`, except while the DJ view is open ([Device rack and device editors](device-rack.md)).

## Lifecycle

`PushController push` is the **last member** of `MainComponent`, so it is destroyed first, while the panels it talks to still exist. `Application` calls `startHardwareControllers()` at start-up stage 2, except under `--startup-test`, so CTest never takes the user's Push. The constructor never touches the hardware ([App shell and control bar](app-shell.md)).

## Tests

- `tests/PushProtocolTest.cpp` (`checkPushProtocol`, run by `--arrangement-geometry-test`) checks the header.
- `tests/Arrangement/scenarios/PushController.inc` drives a controller that is never started, through its `sendToPush` and `playNote` sinks plus `receive()` and `flush()`. It covers reserved ports, the Push input choice, the greeting sending the pad sensitivity, the gesture-depth fix, the display, a turn as one undo step, buttons, mixer mode, pad translation and release, and the Drum Rack bank.
- Reserving a real port name in a test would disable that port in the test engine's device list, so the scenario uses a made-up name.

## On the hardware (2026-10-13)

- **First run.** The Push lit up and Rhino took over its display. The run exposed the double delivery described under Ports; the fix (reservations re-applied on every rescan) is built but not yet re-checked on the hardware.
- **A buzz in the speakers is electrical, not Rhino.** Once Rhino lit the Push, a constant buzz came through the speakers, with the Push on its own power adapter. Moving it to another USB port stopped it. That is USB ground or power noise in the audio path, plausibly worse now that the LEDs draw current. Rhino played nothing: the engine had opened the same MIDI outputs (GS Wavetable Synth and both Push outs, all `sendMidiClock="0"`) in runs before Push support existed. Try another USB port before suspecting the code.
- **Not yet confirmed on the hardware:** the In/Out CCs, the button light values, the display's bar glyphs (3, 5, 6), that the User port carries everything once the Push is in User mode, and that the pad sensitivity sysex makes the pads feel as they do under Live (built and tested on 2026-10-13, not yet tried).

## Related

- [Track inputs and monitoring](inputs-and-monitoring.md)
- [Computer MIDI keyboard](computer-keyboard.md)
- [Keyboard velocity curves](keyboard-velocity-curves.md)
- [Reading MIDI hardware on Windows](reading-midi-hardware-on-windows.md)
- [Device rack and device editors](device-rack.md)
- [Drum Rack](drum-rack.md)
- [Mixer](mixer.md)
- [Session, the model](session-model.md)
- [App shell and control bar](app-shell.md)
- [Forge MIDI learn](forge-midi-learn.md)
