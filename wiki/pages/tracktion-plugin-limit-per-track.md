---
title: Tracktion refuses a track's seventeenth plugin without a word
type: gotcha
summary: Tracktion's default EngineBehaviour::getEditLimits caps a track at 16 plugins, and PluginList::insertPlugin drops a plugin past the cap without a word (the Plugin::Ptr overload Rhino calls returns void); every Rhino track spends about seven on hidden plugins, so a chain filled at about nine devices until RhinoEngineBehaviour raised the cap to 128 on 2026-10-27. The master list (4) and clip lists (5) keep Tracktion's caps, and only addDevice checks, by comparing the list's size.
tags: [rhino, tracktion, devices, gotcha]
sources: []
updated: 2026-10-27
---

# Tracktion refuses a track's seventeenth plugin without a word

## Symptom

`--self-test` adds every catalog device to track 0. When five more devices joined the catalog on 2026-10-27 ([Built-in devices](built-in-devices.md)), the run failed in Rhino Tune's face test: the device was "added", and it was not in the chain.

The same limit had been biting in the app before anyone noticed:
- a chain filled up at about nine devices;
- a new return track's send could not be inserted on a full track.

## Cause

At the pinned Tracktion commit `4536d8a`, `EngineBehaviour::getEditLimits()` (`tracktion_EngineBehaviour.h`) returns these defaults:

| Limit | Default |
| --- | --- |
| `maxNumTracks` | 400 |
| `maxClipsInTrack` | 1500 |
| `maxPluginsOnClip` | 5 |
| `maxPluginsOnTrack` | 16 |
| `maxNumMasterPlugins` | 4 |

`PluginList::insertPlugin` asks `canInsertPlugin` first. Past the cap it inserts nothing and logs nothing.

The overload Rhino calls everywhere, `insertPlugin(Plugin::Ptr, index, SelectionManager*)`, returns `void`, so it cannot report a refusal at all. Only the `ValueTree` overload returns a `Plugin::Ptr`, which is null when it refuses.

Every Rhino track spends about seven of its sixteen before anything is added ([Device chain order](device-chain-order.md)):
- the DJ Input and DJ Output;
- the Utility, the Volume & Pan and the level meter;
- one aux send per return track, two in a new document.

A return track also carries its aux return.

## Fix

The fix landed in commit `1bdd7dc` and has two parts:

- **The cap.** `RhinoEngineBehaviour::getEditLimits` (`native/src/model/SessionInternal.h`) sets `maxPluginsOnTrack` to 128 and leaves every other limit at Tracktion's default. Live has no such limit, and 128 is far past any chain a person builds.
- **The refusal.** `Session::addDevice`'s audio-effect path (`native/src/model/SessionDevices.cpp`) compares the list's size before and after the insert. If the size has not changed, it fails with "<name> could not be added: this chain holds as many devices as it can." instead of reporting success. This covers the main track too, whose master list still holds at most 4 plugins.

`checkDriveDevices` (`native/src/tests/DriveDevicesTest.cpp`) adds ten devices to one fresh audio track, which takes it past the old cap of sixteen plugins, and checks that every one of them is in the chain. All six CTest cases passed on the final tree.

## Still unchecked

No other insert in the model checks whether its plugin went in:
- the instrument, in `switchTrackInstrument` and `ensurePlugin` (`SessionInternal.cpp`), where `ensurePlugin` keeps a pointer to the plugin even when the insert was refused;
- MIDI effects, in `addMidiEffectDevice`;
- the DJ ports;
- the Volume & Pan and the meter (`SessionMixer.cpp`);
- sends and aux returns (`SessionReturns.cpp`);
- clip effects, in `addClipDevice`.

At 128 plugins per track the track paths are out of practical reach. A clip's 5 are not. While a clip has envelopes, its plugin list also holds Clip Envelope and Clip Pitch, so it can take as few as three effects before refusing the next without a word ([Clip envelopes](clip-envelopes.md)).

**The rule:** after `insertPlugin(Plugin::Ptr, ...)`, compare the list's size before and after, as `addDevice` does. The call itself cannot tell you that Tracktion refused the plugin.

## Related

- [Device chain order](device-chain-order.md)
- [Return tracks and sends](return-tracks.md)
- [Session, the model](session-model.md)
- [Built-in devices](built-in-devices.md)
- [The main track](main-track.md)
