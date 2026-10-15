---
title: Track level meters
type: component
summary: A stereo peak meter on every track card and on the main row, fed once a frame by Session::takeOutputLevels from one engine level meter behind each track's fader and the main output's own measurer, drawn on Ardour's IEC 60268-18 scale and gated on the engine's mute and solo.
tags: [rhino, mixer, ui, metering]
sources: []
updated: 2026-10-15
---

# Track level meters

Since 2026-10-15 (commit `7a92d14`) every track card and the main row carry a level meter: what the track sends to the mix, after its fader and pan. On 2026-10-15 all six CTest cases passed with it and its snapshot was checked by eye; it had not yet been seen in the running app.

## Where a reading comes from

- `ensureTrackMixers` (`native/src/model/SessionMixer.cpp`) gives every track, group buses included, exactly one engine `te::LevelMeterPlugin` (`level`) straight behind its Volume & Pan. Older documents get theirs on open, before the undo history begins, so nothing is dirtied. The rack hides it: `isTrackInfrastructure` already counted `level` ([Device chain order](device-chain-order.md)).
- The main output is the playback context's `masterLevels`, after the master fader.
- `Session::takeOutputLevels()` (`native/src/model/SessionMeters.cpp`, message thread only) returns one `OutputLevel {leftDb, rightDb}` per track and the main output's at index `trackCount()` ([The main track](main-track.md)): the loudest sample per channel since the previous call, -100 dB for silence.

## How the session reads, and why so

- **One reader.** A `te::LevelMeasurer` measures only while it has a client, and a `Client` keeps the maximum since its own last `getAndClear`. Each meter therefore has exactly one client, read by exactly one caller: the arrangement's frame clock. A second caller of `takeOutputLevels` would steal the first one's peaks.
- **A tap per meter.** The private `MeterTap` registers a client on the plugin's `measurer` and holds a `te::Plugin::Ptr`, so the measurer cannot die before the client comes off. Taps are keyed by the meter's `itemID`, so a moved track keeps its tap, and checked against the live plugin, because an undo brings a track back as new plugins under the old ids. Taps for vanished tracks are swept each call by a pass counter. The main tap only watches its measurer (`juce::WeakReference`), because the playback context is rebuilt as the audio device comes and goes.
- **Released before the edit.** `releaseMeterTaps()` runs in `~Session`, `newProject` and `restoreProject` before the old edit goes, since a plugin must not outlive its edit ([Hold ids, not pointers](ids-not-pointers.md)).

## Gotchas

- **A muted track's meter keeps measuring.** Tracktion mutes a track after its whole chain (`createNodeForAudioTrack` at `4536d8a` puts the `TrackMutingNode` behind the plugins) and Rhino keeps muted chains running ([Mixer](mixer.md)), so the meter hears what the track would play. `takeOutputLevels` reports silence unless the engine's `Track::shouldBePlayed()` is true, a cached flag `Edit::updateMuteSoloStatuses` updates synchronously on every mute or solo change; solo-silenced tracks read silent too.
- **A render drives the same meters.** During a bounce or a WAV export the taps are still read, so the render's peaks are gone afterwards, but they report silence ([A long command runs in slices behind a modal screen](long-commands-run-in-slices.md)).
- **A starter track's Utility is not metered.** It sits after the meter there; on a track added later it is ahead of the fader and shows.

## Drawing

`native/src/ui/shared/LevelMeter.*` never reads the engine; its owner feeds it.

- `meterFraction(dB)` is Ardour's IEC 60268-18 scale (`log_meter` in `libs/ardour/ardour/logmeter.h`): floor -70 dB, 0 dBFS at 100/115 (87%) of the height, +6 dB at the top. NaN reads as the floor.
- `MeterBallistics`: instant attack, a fall of 24 dB/s, the peak held 1.5 s and then falling, never below the bar.
- `LevelMeter`: two 3 px bars and a 1 px gap (7 px) on `palette::displayInset`, lit through one gradient pinned to the meter rather than to the lit part, so a colour always means one level: `meterSafe` to -12 dB, `meterHot` by -3 dB, `meterClip` from 0 dBFS; a 1 px held-peak line. It is opaque and click-through, and repaints only when a bar or a hold moves a whole pixel: a silent song repaints nothing, and as JUCE clips opaque children out of the parent's paint, a moving meter never repaints the arrangement behind it.

## In the arrangement

- One meter per card, a child of `laneHeaders` made and dropped with the card's controls in `syncTrackControls`, plus `masterMeter`. Since commit `14c9c77` the meter opens the card, at `cardMeterInset` (9 since `83135f7`) plus the group indent, 8 px ahead of the buttons and faders, so a member's meter moves in with its card; the main row's stands at x 9, ahead of MAIN and its faders ([Arrangement view](arrangement-view.md)). In `7a92d14` the meters stood in one column at the header's right edge (`cardMeterLeft`, gone) and the name stopped short of them; the user asked for them at the start of the card.
- `Arrangement::updateMeters()` runs on the existing `VBlankAttachment` after autoscroll and before the playhead, so the playhead's `UpdateWindow` handoff also collects the meters' damage ([Playhead rendering](playhead.md)). It skips while not showing, and back from the DJ view it throws its first read away, peaks that piled up while hidden, and drops the bars to empty.

## Tests (passing on 2026-10-15)

`tests/Arrangement/ClipGeometryTest.cpp` (`native_arrangement_geometry`) checks the scale against `log_meter`'s points, its monotonicity and the ballistics. `tests/Arrangement/scenarios/TrackMeters.inc` (three scenarios in `native_arrangement_workflow`), after `SharedMixer.inc` in a document of its own, checks one hidden meter behind every fader, one reading per track plus main, post-fader readings (the chain played by hand with `applyToBuffer`, as `DjLiveChain.inc` does), a read that clears, silence for muted and solo-silenced tracks, each meter's place at the start of its card and the gradient's colours. The main output's reading has no automated test and was still unverified on 2026-10-15: it needs a live playback context.

To look at the meters without opening the app, set `RHINO_TRACK_METERS_SNAPSHOT` to an absolute `.png` path and run `ctest -R native_arrangement_workflow`: the scenario feeds every meter a different level, a held peak above a falling bar, folds one row so both directions of the fold arrow show, and writes the left 420 px of the arrangement at 2x ([Seeing the UI without taking the screen](headless-ui-snapshots.md)).

## Related

- [Mixer](mixer.md)
- [Arrangement view](arrangement-view.md)
- [Device chain order](device-chain-order.md)
- [Playhead rendering](playhead.md)
- [What a change costs the interface](ui-cost-of-a-change.md)
- [Seeing the UI without taking the screen](headless-ui-snapshots.md)
