---
title: Return tracks and sends
type: component
summary: Live's return tracks - an ordinary track fed by an engine aux bus, kept with the other returns at the foot of the stack - and one post-fader send per return at the end of every other track's chain, set from -inf to 0 dB on a knob under the card's name; a new document has A Delay and B Reverb, wholly wet, fed nothing.
tags: [rhino, mixer, returns, sends, tracks, arrangement]
sources: []
updated: 2026-10-25
---

# Return tracks and sends

Added in commit `b7bd73c` (2026-10-25), modelled on Ableton Live. The model side is all in `native/src/model/SessionReturns.cpp`, declared under "Return tracks, as Live has them" in `model/Session.h`; `trackReturnBusID` (`rhinoReturnBus`) and the engine hook are in `SessionInternal.h`. The UI is `ui/arrangement/SendKnob.*`, `Arrangement::syncSendKnobs` (`ArrangementSync.cpp`), and the knob layout in `resized` and `insertReturnTrack` in `Arrangement.cpp`. Why a return is a track: [A return is an ordinary track at the foot of the stack](return-track-at-the-foot-of-the-stack.md).

## How it is built

- **A return** is a `te::AudioTrack` whose state carries `rhinoReturnBus`, the engine aux bus it returns (the lowest unused, from 0). Its chain is the hidden DJ input, a `te::AuxReturnPlugin` on that bus, its audio effects, the DJ output and the channel strip. `chainHeadEnd` (`SessionInternal.cpp`) skips the DJ input and the aux return, so an effect added later lands after the way in ([Device chain order](device-chain-order.md)).
- **A send** is a `te::AuxSendPlugin` naming the bus: one per return on every other track, group buses included, at the very end of the chain after the fader and meter, so post-fader as Live's are. Both engine types count as channel-strip infrastructure (`isTrackInfrastructure`): hidden from the Device View, never moved or deleted.
- **`ensureReturnRouting`** runs at the end of `ensureTrackMixers`, so on every path that makes a track and as a document opens, inside the caller's transaction. It gives each return its aux return, removes any send on a return (it could feed itself), a send to a bus no return uses and a second send to one bus, adds missing sends and solo-isolates the returns. A new send is written with `auxSendSliderPos` 0, because the engine's default for a new send is 0 dB, which would put every track into a new return at full level.
- A send is the track's own state, so it travels with a copied track and undo restores it. It names its return by bus number, not id, so by the code a track pasted into another document feeds the return that has the same bus there (not tested).

## Rules

- **Levels.** `trackSendDb`/`setTrackSendDb` clamp between `minimumSendDb` (-100, which the engine's fader law maps to position 0, true silence) and `maximumSendDb` (0 dB): the track's own level and no further, as Live's knob. A drag is bracketed by `beginTrackSendGesture`/`endTrackSendGesture` and announced once at its end, as a fader's is, and like a fader's it cannot be undone yet ([Mixer](mixer.md#open-issues)).
- **Starter returns.** `buildStarterEdit` calls `addStarterReturns`, which puts A "Delay" (`te::DelayPlugin`, mix 1.0, 375 ms, a dotted eighth at the starter tempo) and B "Reverb" (`te::ReverbPlugin`, dry 0, room size 0.6) under the four starter tracks. Both are wholly wet, because a return is added to the dry track. Every send starts at nothing. Older documents get no returns ([No backward compatibility for .rhinoedit](no-rhinoedit-back-compat.md)).
- **Adding and deleting.** `addReturnTrack` (the `+` menu's *Return Track*, or Ctrl+Alt+T in `app/MainComponentCommands.cpp`) adds a return named "Return" at the foot of the returns as one undo step, and every track gains a send to it at nothing. A song has at most 12 (A-L, Live's limit) and may have none. Deleting a return (`removeAudioTrack`, `removeTracks`) removes every send to it in the same step, and undo restores them at their levels. "Keep at least one track" counts only tracks that are not returns.
- **The foot of the stack.** The returns stay together under every other track and over the main row. Each is lettered by its place among them (`returnTracks()`). `firstReturnTrack()` is the first return, or `trackCount()` when there is none, so the newest playing track is `firstReturnTrack() - 1`, not `trackCount() - 1`. `appendTrack` inserts above the returns. `addTrack` and `pasteTracks` asked to land below a return land at the foot, above the returns. `moveTrack` keeps a track above the returns and moves a return only among the returns. As a safety net, `reconcileTrackGroups` carries any return it finds above another track back down.
- **Past the last lane.** A return's row, and the space under it, count as past the last lane. A clip dragged, moved or pasted there gets a new lane made above the returns: `editClip`, `moveClipRegion` and `pasteClipRegion` clamp the destination to `firstReturnTrack()`, and the arrangement's drag preview clamps and marks no refusal there. `importAudio` with no free lane imports onto the lane it made. A test found it still importing onto `trackCount() - 1`, which is now a return.
- **What a return refuses** is what a group bus refuses, through `carriesOtherTracks`, `noClipsReason` and `audioEffectsOnlyReason`: instruments, MIDI effects, drum kits and sounds, clips, samples, patterns, clip slots and cells, arming and input (`trackRecordInput` none), and DJ decks (the deck's track menu lists no returns). It cannot be grouped (`groupTracks`, `addTracksToGroup`). It is not copied, duplicated or bounced (`copyTracks` leaves it out, and a cut leaves it in place), but it can be deleted.

## Mute and solo

Rhino processes muted tracks, and with the engine's default a muted track went on feeding its returns ([A send ignores mute while muted tracks are processed](send-ignores-mute-when-muted-tracks-process.md)). Rhino's override makes a send follow its track: a muted track, or one another track's solo leaves out, sends nothing. The returns are solo-isolated (`te::AudioTrack::setSoloIsolate`, written outside undo), so a soloed track is heard with its echo, as Live's solo in place does. While a return is soloed, the isolation comes off and every unmuted track's sends go on feeding, so the soloed return plays alone, fed by the tracks. `Session::followReturnSolo` keeps all this in step and rebuilds the graph when the answer changes.

## On the cards

Every card except a return's shows a row of `SendKnob`s under its name, starting after the fold arrow and level with the faders (`sendLineTop` 21). A `SendKnob` is a 20 px `juce::Slider` that draws a dark well, the return's letter and an arc in `palette::send` (violet). The knobs wrap onto further lines while the row is tall enough and hide on a row too short for one line. The travel is the engine's fader law below unity, dB = 20 ln(position), so half a turn is about -13.9 dB. A drag writes `<track> to <letter> <return>: -12.0 dB` in the status line, and a double-click sends nothing. A return's card shows its letter where other cards show their number (`A  Delay`, from `TrackFacts::returnLetter`), and only mute, solo, fader, pan and meter. Its card menu has no group items and greys out Cut, Copy, Duplicate and Bounce.

## Tests

`tests/Arrangement/scenarios/ReturnTracks.inc` sits after `GroupBusRouting.inc`, because it renders, and every scenario in it builds a document of its own. It covers the starter returns, the refusals, the foot of the stack, adding and deleting, save and reopen, and the cards. It also checks the sound in rendered audio:

- a send at nothing renders the dry burst;
- a send turned up adds silence until the delay time, then the echo;
- a muted track, or one left out by a solo, sends nothing;
- a soloed track keeps its echo;
- a soloed return plays alone, fed by the tracks.

`RHINO_RETURN_TRACKS_SNAPSHOT` writes the cards to a PNG ([Seeing the UI without taking the screen](headless-ui-snapshots.md)).

## Open

- The Delay's time is `te::DelayPlugin::lengthMs`, a `CachedValue` rather than a parameter. The rack cannot change it, and it does not follow the tempo: it stays at 375 ms.
- Send drags, like fader drags, cannot be undone ([Mixer](mixer.md#open-issues)).

## Related

- [A return is an ordinary track at the foot of the stack](return-track-at-the-foot-of-the-stack.md)
- [A send ignores mute while muted tracks are processed](send-ignores-mute-when-muted-tracks-process.md)
- [Mixer](mixer.md)
- [Device chain order](device-chain-order.md)
- [Track groups (bus tracks)](track-groups.md)
- [Arrangement view](arrangement-view.md)
- [The main track](main-track.md)
