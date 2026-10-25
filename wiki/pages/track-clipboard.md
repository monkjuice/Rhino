---
title: Track clipboard (cards)
type: component
summary: Cut, copy, paste, duplicate and delete on whole track cards; a copy is the track's own state, pasted with new engine ids and new device keys, a bus standing for its whole group; pasted into another opened document it keeps its audio, its beats and only the sidechains that came along.
tags: [rhino, arrangement, editing, clipboard, groups, tempo]
sources: []
updated: 2026-10-25
---

# Track clipboard (cards)

Since commit `7f546da` (2026-10-16) the clipboard commands act on whole tracks when the last click landed on a card (`Focus::track`), and from the card menu; in the lanes they act on a region ([Region editing](region-editing.md)). The rules are `Session::copyTracks`, `pasteTracks`, `duplicateTracks` and `removeTracks` in `native/src/model/SessionTrackClipboard.cpp`; the clipboard, selection and status line are `native/src/ui/arrangement/ArrangementTrackClipboard.cpp`.

## What a copy is

`Session::TrackClipboard` holds copies of the tracks' ValueTrees in stack order, each bus directly before its members, taken after `flushStateToValueTree` so a hosted plugin such as Forge writes its settings first. It is a value, so a cut or an undo between copy and paste cannot take it away ([Hold ids, not pointers](ids-not-pointers.md)). Since `c790287` it also records where and when it was taken, `document` (the `Session::documentSerial`) and `tempo`, and names every audio source by its full path (`nameSourcesFor(state, *edit, {})`).

Paste and duplicate share the private `insertTrackCopies` and differ only in the undo step's name. It does what Tracktion's `Edit::copyTrack` does: `createCopy`, `te::EditItemID::remapIDs(state, nullptr, *edit)` for new ids throughout (track, clips, cells, plugins), then `Edit::insertTrack`, except that every copy is prepared before any is inserted (below). At the pinned commit `4536d8a`, `remapIDs` rewrites only references whose target is inside the copied tree, so within one document a sidechain naming a track left out of the copy still names the original.

Rhino's own names are renewed too:

- Every `rhinoDeviceKey` gets a fresh UUID and the copy's `rhinoTrackAutomation` lanes' `device` follows it. Knob overrides are kept by device key ([Track automation](automation.md)), so a shared key would let taking over a knob on one track take it over on the other.
- `rhinoArmed` is stripped: two tracks armed on one input record the same take twice.
- Name, colour, kind, fold and height are kept, as Live keeps them on a duplicate.

## Groups and placement

A bus stands for its whole group in all five commands, as for Bounce. A pasted bus takes a new `rhinoGroupBus` id, one past the highest in the document, and its copied members follow it. Placement is `addTrack`'s rule ([Track groups](track-groups.md)): under an open group's bus or member, plain copies join it by an explicit `trackGroupMemberID`; under a collapsed group they land below all of it; a clipboard carrying a bus always lands below the whole target group, because groups do not nest. `reconcileTrackGroups` then rewrites routing, which the copies carry by position.

## Into another document

The clipboard lives in the `Arrangement`, and File > Open and New replace the edit inside the same `Session` (`restoreProject`, `newProject`) without clearing it (`Arrangement::editWillChange` keeps both clipboards). So copy, open another project, paste always ran; until `c790287` (2026-10-24) it broke three ways. "Another document" is `clipboard.document != documentSerial`; the serial comes from a process-wide atomic counter (`nextDocumentSerial` in `Session.cpp`), set in `buildStarterEdit` and `restoreProject`, so two `Session`s in one test never share one. Reopening the same file counts as another document, deliberately. In the same document at the same tempo a paste behaves exactly as before.

- **Audio.** A source stored relative to the first document resolved beside the second and played nothing (and a missing file is what keeps an offline render from returning, [Offline renders that never return](renders-that-never-return.md)). The copy names sources in full; the paste names them for the destination's file (`edit->editFileRetriever()`), as an import would.
- **Sidechains.** `sidechainSourceID` names a track by `EditItemID`, and two documents built from the starter stack share their track ids, so a vocoder's carrier silently became an unrelated track. From another document a sidechain is pointed at the copy of its source when that track came along, and removed otherwise.
- **Tempo.** Clips and Rhino's automation points are stored in seconds, so at another tempo they left their beats. Points are rescaled by copied BPM over current BPM (`rescaleAutomationPoints`); each landed track's timeline and slot clips go through `keepOnItsBeats`, which restates the engine's tempo-change rule (`EditTimecodeRemapperSnapshot::remapEdit` at `4536d8a`, run by `TempoSetting::setBpm`): `syncAbsolute` stays put, a `WaveAudioClip` with auto-tempo off moves only its start (`setStart(start, false, true)`, length in seconds and offset kept), anything else scales start, end and offset. It is restated because no public engine call remaps a subset of clips. `updateRepitchedClips()` follows, as in `setTempo` ([Transport, tempo and loop](transport.md#one-tempo-and-what-follows-it-by-hand)).
- **Scenes** grow to the highest copied slot holding a clip (`ensureSceneSlots`), so no cell lands out of sight.

**Every copy is prepared before any lands**: sidechains collected, `remapIDs`, old id to new recorded, keys renewed, sources named, points rescaled, arm and groups set. A sidechain on one copy may name another copy whose new id exists only after its remap. Remapping all first is safe because `te::Edit::createNewItemID` is a monotonic counter (`nextID` in `tracktion_Edit.h`). `nameSourcesFor` and `rescaleAutomationPoints` left their anonymous namespaces for `SessionInternal.h` for this.

Open: the clipboard is in-process, so two RhinoDAW instances cannot share it (that needs the system clipboard); the region clipboard has the same tempo problem ([Region editing](region-editing.md#why-snapshots-not-ids)); a copied starter MIDI track still carries its hidden, empty "Pattern 1" clip, as a duplicate always did.

## Delete

`removeTracks` deletes the tracks and every member of a bus among them as one undo step, in `removeAudioTrack`'s order: clear sidechain sources naming them, `dissolveGroupInEdit` while the bus still exists, then delete by id. It refuses the main row and removing every track. **Delete on a bus card now deletes the whole group**, as in Live; it used to call `removeAudioTrack`, which kept the members. Ctrl+Shift+G is the way to drop only the bus. Delete now acts on every selected card, not only the working one.

**Return tracks** (since `b7bd73c`; [Return tracks and sends](return-tracks.md)) are never copied: `copyTracks` and `duplicateTracks` leave them out, and a cut leaves them in place, since a cut deletes only what it could copy. Delete does take a return, along with every send to it, in the same undo step. "Keep at least one track" counts only tracks that are not returns. Pastes and duplicates land above the returns, and a pasted track keeps its sends.

## In the arrangement

- `copySelection`, `cutSelection` and `duplicateSelected` dispatch to the card commands on `Focus::track`, as Delete does. `pasteSelection` pastes tracks whenever the track clipboard is full, under the working card, or at the foot with the main row selected.
- **One clipboard at a time.** `trackClipboard` sits beside the clip `clipboard`; copying either empties the other, so Ctrl+V puts down what was copied last.
- **The card methods take the list by value**, because the shortcuts pass `selectedTracks`, which the session's synchronous announcement rebuilds mid-call ([What a change costs the interface](ui-cost-of-a-change.md)).
- Pasted cards become the selection, the last the working track, so a second paste lands under the first. After a delete the card that moved into the first gap keeps the selection and focus, so Delete again takes the next.

`tests/Arrangement/scenarios/TrackClipboard.inc` covers the model, groups and the shortcuts, and since `c790287` a paste of two tracks from a 120 BPM document into a 90 BPM one, both written to disk and opened: a MIDI clip and a relative sample at beat 4, a lane from beat 4 to 8, two vocoders sidechained from a copied and an uncopied track, and one undo. Untested: a track hosting Forge, and the card menu's async callback (it calls the same methods).

## Related

- [Arrangement view](arrangement-view.md)
- [Region editing](region-editing.md)
- [Track groups (bus tracks)](track-groups.md)
- [Session, the model](session-model.md)
- [Transport, tempo and loop](transport.md)
- [Project files (.rhinoedit)](project-files.md)
