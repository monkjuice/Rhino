---
title: A layout that reads a child's preferred size must sync the child first
type: gotcha
summary: DjView laid itself out before syncing its mixer, so the layout read the mixer's width from the old strip count and a new deck's strip sat under the master section until the next resize; sync the children a layout measures first, and re-lay out when the measured size changes.
tags: [rhino, ui, layout]
sources: []
updated: 2026-10-09
---

# A layout that reads a child's preferred size must sync the child first

A composite view that syncs from the model and lays its children out by their preferred sizes has an ordering to get right: a child whose preferred size depends on the model must be synced before the layout that measures it runs.

## What it broke

`DjView::sync` (`native/src/DjView.cpp`) rebuilt its deck panels, called `resized()`, and only then called `mixer.sync()`. `DjView::resized` sizes the mixer by `mixer.preferredWidth()`, which follows the number of channel strips, and the strips are made in `mixer.sync()`. So after a fourth deck was added the layout gave the mixer the width of three strips, the mixer then grew a fourth strip into that width, and it was drawn under the master section until the window was next resized and the layout ran again on the right count (reported by the user on 2026-10-09).

## The fix

`mixer.sync()` runs first. The view then re-lays out when the deck count or `mixer.preferredWidth()` differs from what the last layout used (`laidOutMixerWidth`, written in `resized`), rather than only when the deck count changed, so a mixer that widens for any other reason is placed again too.

## The rule

In a view's `sync`, sync every child whose preferred size the layout reads before calling `resized()`, and keep the measured value the layout used so a later change to it re-lays out. The same order holds for a hidden panel catching up in `visibilityChanged` ([What a change costs the interface](ui-cost-of-a-change.md)). The view scenario in `DjBooth.inc` did not see this at first: it built the view with its two decks already on the session and sized it once, so the first layout measured the finished mixer. It now adds a third deck while the view stands, calls `view->sync()` with no resize between, and requires every strip's fader and side key to end left of the master section (`mixer.getWidth() - DjMixerPanel::masterWidth - 4`) and inside the mixer, before removing the deck again. A check of a sync-order bug has to change the model under a view that is already laid out.

## Related

- [DJ view and the booth](dj-view.md)
- [What a change costs the interface](ui-cost-of-a-change.md)
- [Writing Rhino tests](writing-rhino-tests.md)
