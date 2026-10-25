---
title: Device chain order
type: concept
summary: A track's plugin list is its signal chain - the hidden DJ input (on a return track followed by its aux return), MIDI FX, instrument, audio FX, the hidden DJ output, a channel strip of Utility, fader and meter whose Utility sits on a different side of the fader on starter and added tracks, then one send per return - so the first visible device is plugin 1, or 2 on a return; how every add and every drag keeps that order.
tags: [rhino, devices, signal-chain, dj, returns, gotcha]
sources: []
updated: 2026-10-25
---

# Device chain order

A track's plugin list *is* its signal chain: each device reads what the one in front of it wrote. So order is meaningful and editable. A vocoder followed by [Rhino Tune](rhino-tune.md) tunes the vocoded signal; swap them and the vocoder is handed an already-tuned voice.

## The order

At plugin index 0 the DJ booth's hidden input (`DjTrackInjectorDevice`, "DJ Input"); then MIDI effects, the instrument and audio effects; then the booth's hidden output (`DjTrackTapDevice`, "DJ Output"); then, hidden from the Device View like the two DJ ends, the channel strip: Rhino's Utility, the engine's Volume & Pan and, straight behind it on every track since 2026-10-15, the engine's level meter the card reads ([Track level meters](level-meters.md)), the Utility's place depending on how the track was made (below). Since commit `b7bd73c` the chain ends with one `te::AuxSendPlugin` per return track, after the meter (post-fader sends), and a return track has a `te::AuxReturnPlugin` straight after the DJ input, so its first visible device is plugin 2 ([Return tracks and sends](return-tracks.md)). `isTrackInfrastructure` in `native/src/model/SessionDevices.cpp` reads the catalog's `infrastructure` flag and also counts the engine's `volume`, `level`, aux send and aux return types, so no send or aux return shows, moves or deletes. The two DJ ends are made with every track by `ensureDjPortDevices` and pass audio through until a deck binds them ([DJ decks play live through the track's chain](dj-decks-play-live-through-the-track.md)); neither can be moved (`canBeMoved`) or deleted. Each add path builds the order:

- `switchTrackInstrument` puts an instrument after the DJ input and any leading MIDI effects, at the old instrument's index ([One instrument per track](one-instrument-per-track.md)).
- `addMidiEffectDevice` puts a MIDI effect immediately in front of the instrument, or, on a track with no instrument yet, after the DJ input and the MIDI effects already at the front, which is where `switchTrackInstrument` will put one.
- An audio effect, a channel-strip facility like Utility included, goes on either kind of track, in front of the first infrastructure plugin after the DJ input (`channelStripInsertIndex`), which is the DJ output: so a device added later always lands ahead of it, in the part of the chain a deck hears.
- Instruments and MIDI effects start counting at `djInputEnd` (`SessionInternal.h`), which is 1 when the DJ input heads the chain and 0 otherwise. Audio effects (`channelStripInsertIndex`) and the DJ output's placement (`ensureDjPortDevices`) start at `chainHeadEnd`, which also skips an aux return right after the DJ input, so an effect added to a return lands after its way in.
- On the main track an effect is appended to the edit's master list, and the engine's `canBeAddedToMaster` refuses the rest ([The main track](main-track.md)). A group bus and a return track take audio effects only.

**Raw plugin indices count the hidden input** (since 2026-10-12). A track's first visible device is plugin 1, so `Session`'s `slot` arguments, `DeviceTarget::slot` and `DeviceSlot::pluginIndex` are one higher than before, and the scenarios that named a track's first device as slot 0 moved to 1 (`EditingAndAutomation.inc`, `Persistence.inc`, `AudioClipEditing.inc`, `TempoChange.inc`). Find a device through `deviceSlots` and its `pluginIndex`, or a hidden one by its type, rather than by counting.

## Pitfall: starter and added tracks order their channel strips differently

The four starter tracks are made by `Edit::ensureNumberOfAudioTracks` (`Session::buildStarterEdit`), which gives each Tracktion's default Volume & Pan and level meter at the front of an empty chain, and the Utility is appended after them: the strip reads Volume & Pan, meter, Utility. A track added later (`appendTrack`) gets the Utility first, then `ensureTrackMixers` appends Volume & Pan and puts a meter straight behind it: Utility, Volume & Pan, meter (until 2026-10-15 such a track had no meter). The meter is behind the fader on both, so a card meter reads post-fader everywhere; the Utility is not, so its trim shows on an added track's meter and not on a starter track's. Every add path puts a device ahead of either strip, but nothing may find a strip plugin by its position or by its neighbour. The DJ output was first placed "before the Volume & Pan", which put it ahead of the Utility on one kind of track and after it on the other, and a failing check caught it (2026-10-12); `ensureDjPortDevices` now puts it before the first infrastructure plugin after the DJ input, whatever the order, so a deck takes every track's instrument and effects and the song keeps its trim, fader, pan and mute. `AutomationLanes.inc` finds a fresh track's Utility by its type for the same reason.

## Moving a device

The [Device rack and device editors](device-rack.md) starts the drag and `Session::moveDevice` does the move. It is the one device call that speaks in **positions within `deviceSlots`** rather than plugin indices, because the plugin list also holds the hidden DJ ends and channel strip and what is dragged is what is on screen. The destination plugin index is taken from the neighbour the device lands against, never counted, and the plugin leaves the list before rejoining it, so everything behind it has shifted down one. A move that would break the order is refused with a reason ("MIDI FX run before the instrument."). The rule is `chainRank` in `SessionDevices.cpp`, in the model, so every path inherits the refusal. Automation lanes name their device by key, not by position, so they follow it through a move ([Track automation](automation.md)).

## History: a MIDI effect added before the instrument

Until commit `23de20c` (2026-10-05), a MIDI effect added to a track with no instrument was appended at the end of the list, behind the channel strip, so an instrument added afterwards landed in front of it, where the effect had no notes left to rewrite. `DeviceRackTest.cpp` now adds a MIDI effect and then an instrument to an empty MIDI track and checks the order.

## Clip effects

An audio effect dropped on an audio clip goes into that clip's own plugin list (`Session::addClipDevice`), not the track's. A warped or reversed clip cannot host effects, and the drop then falls back to the track. Clip effects have no editor yet; the clip shows only an `FX<n>` badge.

## Related

- [Device rack and device editors](device-rack.md)
- [One instrument per track](one-instrument-per-track.md)
- [Rhino Vocoder and sidechains](rhino-vocoder.md)
- [Track kinds: audio and MIDI](track-kinds.md)
- [The main track](main-track.md)
- [DJ decks play live through the track's chain](dj-decks-play-live-through-the-track.md)
- [Track level meters](level-meters.md)
- [Return tracks and sends](return-tracks.md)
