---
title: Pointer and selection rules
type: concept
summary: One pointer vocabulary for the arrangement and the note editor, written once as shared predicates in SelectionInput.h.
tags: [rhino, ui, interaction, selection]
sources: []
updated: 2026-10-23
---

# Pointer and selection rules

The arrangement and the note editor answer the pointer the same way, because a clip and a note are the same kind of thing to select. `native/src/ui/shared/SelectionInput.h` writes the rule down as shared predicates rather than a shared gesture; each panel still decides what its marquee catches.

## The rules

- **A plain press on an item** takes it and drops everything else. In the arrangement, since 2026-10-23, that is the clip's own span: a press on a header the selection does not touch, or on either trim handle, selects exactly that clip, because a trim edits one clip, never a selection ([Region editing](region-editing.md#one-selection)).
- **A plain press on empty space** sweeps out a region or a marquee, and clears the old selection if it never travels. In the arrangement a clip's lower row is empty space too; a region swept across part of a clip selects that part (the clip counts as selected), and dragging any header inside the region carries all of it ([Region editing](region-editing.md#moving-a-selection)).
- **Ctrl** (`isToggleSelectionModifier`, Ctrl or Command) adds one item or takes it back out. **Shift** extends, which for a note grid degrades to adding, and never removes. On a clip header the two mean the same thing since 2026-10-23: both widen the rectangle to take the clip in (`extendSelectionToClip`) and make it the primary clip, because a rectangle has nothing to toggle out and Live's arrangement has only Shift-extend for clips; a clip already inside stays inside. A Shift press on a lane widens from the corner of the rectangle farthest from the pointer, so extending works after a clip click too, and `regionExtending` keeps it a span before it travels: it used to widen on the press and collapse back to a line on release, because the release path treated an untravelled press as a line, found while writing the Shift-click check in `RegionClipboard.inc`. Track cards are the exception: they are a list, so Shift selects the range between two cards.
- **A modifier press is about the selection only** (`isMultiSelectModifier`). It must never also start carrying, or adding one clip to a group would nudge the whole group.
- **A press inside a multi-selection keeps it** so the drag can carry all of it, and collapses onto the item under the pointer on release if no drag happened (`collapseSelectionOnRelease`). That cannot be decided on the press. In the arrangement overlap decides, not the press point: a press on the header of a clip the selection touches, even outside the covered part of a partially covered clip, keeps the selection and carries it, and collapses onto the clip on an untravelled release only when the selection was wider than the clip (`TimeSelection::isExactly`). A move drag is clamped so the rectangle's first row stays at or below track 0 and its start at or after the song's start; the carried rows show Live's circle-with-slash where their material could not land; on release one `Session::moveClipRegion` moves everything, the selection follows it (`TimeSelection::shifted`) and the primary clip is kept ([Arrangement view](arrangement-view.md#rules-worth-knowing)). After any change the selection follows the clip it is exactly - a nudged, trimmed or undone clip takes its own-span selection with it, and a deleted one clears it - while a wider rectangle stays where it was put ([Region editing](region-editing.md#one-selection)).
- **Left and Right nudge the selection's material** by a grid step, Shift by a bar, whenever the selection is a range ([Region editing](region-editing.md#the-operations)).
- **Creating is a double-click** in both editors: an empty lane makes a clip filling the grid cell the pointer is inside (`snappedDown`, as any pointer position takes), never shorter than a bar ([Arrangement view](arrangement-view.md); always one bar until commit `3a0933c`), an empty cell a note. The single click is then free to mean "select" everywhere.
- **Drawing is a mode, not a modifier**: B (or the footer button) toggles `StepGrid::setDrawMode`, and the right button erases in either mode.
- **Alt** during a drag bypasses snapping; **Escape** cancels a drag and clears the time selection; the **middle button** pans the arrangement from anywhere, headers included.

The note editor's [velocity lane](note-velocity-lane.md) (2026-10-16) applies the same rules to its markers. A press takes a marker's note, Ctrl or Shift adds to the selection, a click inside a group collapses on release, and a press on empty lane starts a marquee that catches markers by their dot. Three things differ there. A drag arms only after 3 px, and arming moves nothing. A click is a press that never armed, not one that changed nothing: a group dragged against a rail moves no value but keeps its selection on release. B paints values rather than notes. The right button and double-click do nothing, because a marker can stand for a note scrolled out of sight. A second press while a lane gesture is held (a second finger, or a pen beside the mouse) ends that gesture before it starts another.

## Autoscroll

A drag reaching a panel's edge pulls the view after it, so a selection can sweep past the screen. `autoScrollPush` is the shared ramp: it starts 30 px inside the edge and grows to a cap of three scroll steps. It is driven by each panel's frame clock (the arrangement's `VBlankAttachment`, the note editor's gesture timer), never by mouse moves, because a pointer held still outside the panel sends no more drag events. Each panel scrolls in exactly one place: scrolling in the drag handler as well made a moving pointer travel at double speed. For the same reason the note editor's marquee anchors in steps and pitch, not pixels, and asks the clip rather than the visible rows what it caught. Gestures in the velocity lane autoscroll in time only, never in pitch. A lane stroke is held to the visible steps, so a pointer parked past the edge paints the columns autoscroll brings in.

## History

These rules retired the held-key marquee, **S**, in both panels.

## Related

- [Region editing](region-editing.md)
- [Note editor (StepGrid)](note-editor.md)
- [Note editor velocity lane](note-velocity-lane.md)
- [Arrangement view](arrangement-view.md)
- [Clips never overlap](clip-placement.md)
