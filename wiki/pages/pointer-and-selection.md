---
title: Pointer and selection rules
type: concept
summary: One pointer vocabulary for the arrangement and the note editor, written once as shared predicates in SelectionInput.h.
tags: [rhino, ui, interaction, selection]
sources: []
updated: 2026-10-03
---

# Pointer and selection rules

The arrangement and the note editor answer the pointer the same way, because a clip and a note are the same kind of thing to select. `native/src/SelectionInput.h` writes the rule down as shared predicates rather than a shared gesture; each panel still decides what its marquee catches.

## The rules

- **A plain press on an item** takes it and drops everything else.
- **A plain press on empty space** sweeps out a region or a marquee, and clears the old selection if it never travels.
- **Ctrl** (`isToggleSelectionModifier`, Ctrl or Command) adds one item or takes it back out. **Shift** extends, which for a note grid or a timeline rectangle degrades to adding, and never removes. Track cards are the exception: they are a list, so Shift selects the range between two cards.
- **A modifier press is about the selection only** (`isMultiSelectModifier`). It must never also start carrying, or adding one clip to a group would nudge the whole group.
- **A press inside a multi-selection keeps it** so the drag can carry all of it, and collapses onto the item under the pointer on release if no drag happened (`collapseSelectionOnRelease`). That cannot be decided on the press.
- **Creating is a double-click** in both editors: an empty lane makes a one-bar clip, an empty cell a note. The single click is then free to mean "select" everywhere.
- **Drawing is a mode, not a modifier**: B (or the footer button) toggles `StepGrid::setDrawMode`, and the right button erases in either mode.
- **Alt** during a drag bypasses snapping; **Escape** cancels a drag and clears the time selection; the **middle button** pans the arrangement from anywhere, headers included.

## Autoscroll

A drag reaching a panel's edge pulls the view after it, so a selection can sweep past the screen. `autoScrollPush` is the shared ramp: it starts 30 px inside the edge and grows to a cap of three scroll steps. It is driven by each panel's frame clock (the arrangement's `VBlankAttachment`, the note editor's gesture timer), never by mouse moves, because a pointer held still outside the panel sends no more drag events. Each panel scrolls in exactly one place: scrolling in the drag handler as well made a moving pointer travel at double speed. For the same reason the note editor's marquee anchors in steps and pitch, not pixels, and asks the clip rather than the visible rows what it caught.

## History

These rules retired the held-key marquee, **S**, in both panels.

## Related

- [Region editing](region-editing.md)
- [Note editor (StepGrid)](note-editor.md)
- [Arrangement view](arrangement-view.md)
- [Clips never overlap](clip-placement.md)
