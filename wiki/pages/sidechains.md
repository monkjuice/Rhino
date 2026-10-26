---
title: Sidechains
type: component
summary: Any device the engine says can take a sidechain may name one source track, which Tracktion taps after that track's fader and before its mute, with no gain and no other tap point; Rhino refuses a source that already carries the device's own track, and the rack's Sidechain strip chooses it on every plain-grid or generated face.
tags: [rhino, devices, sidechain, routing, tracktion]
sources: []
updated: 2026-10-26
---

# Sidechains

One track's audio routed into a device on another track: the [Rhino Vocoder](rhino-vocoder.md)'s carrier, [Rhino Duck](rhino-duck.md)'s Audio trigger, a compressor ducking to a kick. The model is `native/src/model/SessionSidechain.cpp`, written for the Vocoder and device-agnostic. The rack's chooser is `native/src/ui/rack/DeviceEditorPanelSidechain.cpp` (2026-10-26).

## What takes one

`Session::deviceTakesSidechain` asks `te::Plugin::canSidechain()`. It is true when the device has more than two inputs, or more inputs than outputs, and false inside a rack. Three ways in:

- **A Rhino device** declares extra input channel names (the Vocoder's Carrier L and R). The device SDK (`devices/sdk/`) cannot declare one yet.
- **A VST3** declares a sidechain bus. `te::ExternalPlugin` enables every bus on creation, so Duck reports four inputs.
- **`te::CompressorPlugin`** (catalog id `Compressor`) declares a third input, "Sidechain Trigger".

## What Tracktion does

Checked against pinned Tracktion `4536d8a`, `createNodeForAudioTrack` in `playback/graph/tracktion_EditNodeBuilder.cpp`. The plugin stores the source's `EditItemID`. The graph builder puts a send on the source after its whole plugin list, so after Rhino's fader, and before its `TrackMutingNode`. The plugin's wires 0-1 carry its own track and 2 up the source.

- **One tap point.** It is Live's Post Mixer, except that it ignores mute: a muted source still drives the device. No API offers Pre FX or Post FX.
- **No gain or mix.** The return is a channel remap into a summing node, with no gain on a wire.
- **No feedback guard.** `AudioTrack::getInputTracks` looks only at output routing. Nobody has checked whether a loop hangs, crashes or reads silence, so Rhino prevents loops instead.
- **No source means silence on the extra inputs.** `ExternalPlugin::applyToBuffer` clears channels 2-3 of its scratch buffer. Until 2026-10-26 only the Vocoder's face could choose a source, so Duck's Audio trigger never fired.
- **The compressor has to be told.** It detects on channel 2 only while its `useSidechainTrigger` CachedValue is set. `followSidechain` sets that flag to match whether a source is routed, in the same undo step. Choosing None, deleting the source and clearing a loop all reset it. With three inputs, `guessSidechainRouting` wires source channels 2 and 3 both into channel 2. The compressor's own "Sidechain gain" stays among its knobs.

## Rhino's rules

- `setDeviceSidechainSource` rewires and calls `restartPlayback` in one undo step ("Set audio source"). The id alone would take effect only at the next transport start.
- **A source can't already carry the device's track.** `tracksCarrying` walks the feeds: output routing into a group bus, aux sends into a return, and other devices' sidechains. A send stays in the graph even at -inf, so every return carries every track. `deviceSidechainSources` leaves these tracks out and the setter refuses them. So a member's device cannot take its group's bus, and no ordinary track's device can take a return.
- **Grouping or a move can create a loop.** `reconcileTrackGroups` ends with `clearSidechainLoops` inside the caller's transaction, so undo restores the source ([Track groups](track-groups.md)). Deleting a track clears its id from every plugin naming it. A device on the main output is never cleared as a loop.
- While the song is stopped, a track held by a DJ deck feeds silence: its hidden DJ Output clears the block before the send ([DJ view and the booth](dj-view.md)).

The strip appears on plain-grid (`Face::Generic`) and generated faces only. The Vocoder keeps its own chooser, and other faces of their own have none ([Device rack and device editors](device-rack.md)).

## Live, for comparison

From the Live 12 manual ([Development environment](development-environment.md)):

- A plug-in with sidechain inputs gets a Sidechain section at its left: Audio From, a tap point, Gain, Mix and Mute (pp. 463-464).
- Compressor and its kin add Pre FX, Post FX and Post Mixer taps, Dry/Wet, Listen and an EQ (p. 558; taps pp. 376-377).
- The Vocoder's Carrier chooser offers Noise, External, Modulator and Pitch Tracking. External opens the same Audio From choosers, and a MIDI track chosen there sends its instrument's audio (pp. 658-660).
- Note-driven sidechains belong to Auto Shift, Corpus, Roar, Shifter and Spectral Resonator.

## Missing

- Gain, Mix, Mute, Listen and a tap chooser on the strip. The engine offers one tap and no gain.
- MIDI sidechains: Duck's MIDI trigger from another track's notes, and Live's note-driven devices.
- The Vocoder's other carriers.
- A sidechain input declared on the SDK.

## Tests

All six CTest cases passed with these tests on 2026-10-26 (commit `d9838b6`).

- `tests/Pattern/DeviceRackTest.cpp` covers the panel:
  - only the compressor's panel has the strip, left of its knobs;
  - `chooseSidechainSource`, the test seam, routes and flips `useSidechainTrigger`;
  - it refuses the device's own track, and None clears the source.
  - Set `RHINO_SIDECHAIN_SNAPSHOT` to a PNG path and the test writes the compressor's face with a source chosen ([Seeing the UI without taking the screen](headless-ui-snapshots.md)).
- `tests/Arrangement/scenarios/DeviceSidechain.inc` runs after `ReturnTracks.inc` because it renders:
  - the source rules, and grouping clearing a loop with undo restoring it;
  - a 60 Hz burst on a muted track ducks a compressor's 1 kHz tone by more than 12 dB under the burst, and stays within 1 dB either side of it. So a muted source does feed a sidechain;
  - with no source, the compressor squeezes the tone evenly, within 1 dB between windows;
  - Duck in Audio mode ducks that tone by more than 20 dB 20-28 ms after the burst's onset with the burst routed, and stays within 1 dB without it. The block requires Duck whenever `effects/rhino-duck/build/RhinoDuck_artefacts/Release/VST3/Rhino Duck.vst3` exists, as `DeviceParameters.inc` does, so a machine that has the bundle never skips it silently.

## Related

- [Rhino Vocoder](rhino-vocoder.md)
- [Rhino Duck](rhino-duck.md)
- [Device rack and device editors](device-rack.md)
- [Built-in devices](built-in-devices.md)
- [Track groups (bus tracks)](track-groups.md)
- [Return tracks and sends](return-tracks.md)
- [Mixer](mixer.md)
