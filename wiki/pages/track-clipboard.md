---
title: Track clipboard (cards)
type: component
summary: Cut, copy, paste, duplicate and delete on whole track cards; a copy is the track's own state, pasted with new engine ids and new device keys, a bus standing for its whole group.
tags: [rhino, arrangement, editing, clipboard, groups]
sources: []
updated: 2026-10-16
---

# Track clipboard (cards)

Since commit `7f546da` (2026-10-16) the clipboard commands act on whole tracks when the last click landed on a card (`Focus::track`), and from the card menu; in the lanes they act on a region ([Region editing](region-editing.md)). The rules are `Session::copyTracks`, `pasteTracks`, `duplicateTracks` and `removeTracks` in `native/src/model/SessionTrackClipboard.cpp`; the clipboard, selection and status line are `native/src/ui/arrangement/ArrangementTrackClipboard.cpp`.

## What a copy is

`Session::TrackClipboard` holds copies of the tracks' ValueTrees in stack order, each bus directly before its members, taken after `flushStateToValueTree` so a hosted plugin such as Forge writes its settings first. It is a value, so a cut or an undo between copy and paste cannot take it away ([Hold ids, not pointers](ids-not-pointers.md)).

Paste and duplicate share the private `insertTrackCopies` and differ only in the undo step's name. It does what Tracktion's `Edit::copyTrack` does: `createCopy`, `te::EditItemID::remapIDs(state, nullptr, *edit)` for new ids throughout (track, clips, cells, plugins), then `Edit::insertTrack`. At the pinned commit `4536d8a`, `remapIDs` rewrites only references whose target is inside the copied tree, so a sidechain naming another track still names the original.

Rhino's own names are renewed too:

- Every `rhinoDeviceKey` gets a fresh UUID and the copy's `rhinoTrackAutomation` lanes' `device` follows it. Knob overrides are kept by device key ([Track automation](automation.md)), so a shared key would let taking over a knob on one track take it over on the other.
- `rhinoArmed` is stripped: two tracks armed on one input record the same take twice.
- Name, colour, kind, fold and height are kept, as Live keeps them on a duplicate.

## Groups and placement

A bus stands for its whole group in all five commands, as for Bounce. A pasted bus takes a new `rhinoGroupBus` id, one past the highest in the document, and its copied members follow it. Placement is `addTrack`'s rule ([Track groups](track-groups.md)): under an open group's bus or member, plain copies join it by an explicit `trackGroupMemberID`; under a collapsed group they land below all of it; a clipboard carrying a bus always lands below the whole target group, because groups do not nest. `reconcileTrackGroups` then rewrites routing, which the copies carry by position.

## Delete

`removeTracks` deletes the tracks and every member of a bus among them as one undo step, in `removeAudioTrack`'s order: clear sidechain sources naming them, `dissolveGroupInEdit` while the bus still exists, then delete by id. It refuses the main row and removing every track. **Delete on a bus card now deletes the whole group**, as in Live; it used to call `removeAudioTrack`, which kept the members. Ctrl+Shift+G is the way to drop only the bus. Delete now acts on every selected card, not only the working one.

## In the arrangement

- `copySelection`, `cutSelection` and `duplicateSelected` dispatch to the card commands on `Focus::track`, as Delete does. `pasteSelection` pastes tracks whenever the track clipboard is full, under the working card, or at the foot with the main row selected.
- **One clipboard at a time.** `trackClipboard` sits beside the clip `clipboard`; copying either empties the other, so Ctrl+V puts down what was copied last.
- **The card methods take the list by value**, because the shortcuts pass `selectedTracks`, which the session's synchronous announcement rebuilds mid-call ([What a change costs the interface](ui-cost-of-a-change.md)).
- Pasted cards become the selection, the last the working track, so a second paste lands under the first. After a delete the card that moved into the first gap keeps the selection and focus, so Delete again takes the next.

`tests/Arrangement/scenarios/TrackClipboard.inc` covers the model, groups and the shortcuts. Untested: a track hosting Forge, and the card menu's async callback (it calls the same methods).

## Related

- [Arrangement view](arrangement-view.md)
- [Region editing](region-editing.md)
- [Track groups (bus tracks)](track-groups.md)
- [Session, the model](session-model.md)
